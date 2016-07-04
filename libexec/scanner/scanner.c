/*-
 * Copyright (c) 2014-2015 The University of Oslo
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <sys/types.h>
#include <sys/stat.h>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <tsd/assert.h>
#include <tsd/ctype.h>
#include <tsd/log.h>
#include <tsd/sbuf.h>
#include <tsd/strutil.h>
#include <tsd/percent.h>

static long maxfiles = 10000;

struct scan_entry {
	struct sbuf *path;
	struct scan_entry *next;
};

struct scanpath {
	/*
	 * Worklist
	 */
	struct scan_entry *todo, *tail;

	/*
	 * Track number of entries found and when to stop.
	 */
	long processed;
};

/*
 * Free a worklist entry.  Save and restore errno to facilitate use in
 * error handling code.
 */
static void
tsdfx_scan_free(struct scan_entry *se)
{
	int serrno;

	if (se != NULL) {
		serrno = errno;
		if (se->path != NULL)
			sbuf_delete(se->path);
		free(se);
		errno = serrno;
	}
}

/*
 * Append a directory to the worklist.
 */
static struct scan_entry *
tsdfx_scan_append(struct scanpath *sp, const struct sbuf *path)
{
	struct scan_entry *se;

	if ((se = calloc(1, sizeof *se)) == NULL)
		return (NULL);
	if ((se->path = sbuf_new_auto()) == NULL ||
	    sbuf_cpy(se->path, sbuf_data(path)) == -1 ||
	    sbuf_finish(se->path) == -1)
		goto fail;
	if (sp->todo == NULL)
		sp->todo = sp->tail = se;
	else
		sp->tail = sp->tail->next = se;
	return (se);
fail:
	tsdfx_scan_free(se);
	return (NULL);
}

/*
 * Remove and return the next entry from the worklist.
 */
static struct scan_entry *
tsdfx_scan_next(struct scanpath *sp)
{
	struct scan_entry *se;

	if ((se = sp->todo) != NULL) {
		if ((sp->todo = se->next) == NULL) {
			ASSERT(sp->tail == se);
			sp->tail = NULL;
		}
	}
	return (se);
}

/*
 * Initialize the worklist and file list.
 */
static struct scanpath *
tsdfx_scan_init(const char *root)
{
	struct scan_entry *se;
	struct scanpath *sp;

	if ((sp = calloc(1, sizeof *sp)) == NULL)
		return (NULL);
	if ((se = calloc(1, sizeof *se)) == NULL)
		goto fail;
	if ((se->path = sbuf_new_auto()) == NULL ||
	    sbuf_cpy(se->path, root) != 0 ||
	    sbuf_finish(se->path) != 0)
		goto fail;
	sp->todo = sp->tail = se;
	sp->processed = 0;
	return (sp);
fail:
	tsdfx_scan_free(se);
	se = NULL;
	free(sp);
	sp = NULL;
	return (NULL);
}

/*
 * Empty the worklist and file list.
 */
static void
tsdfx_scan_cleanup(struct scanpath *sp)
{
	struct scan_entry *se;

	while ((se = tsdfx_scan_next(sp)) != NULL)
		tsdfx_scan_free(se);
	free(sp);
	sp = NULL;
}

/*
 * Process a directory entry.
 */
static int
tsdfx_process_dirent(struct scanpath *sp, const struct sbuf *parent,
		     int dd, const struct dirent *de)
{
	const char *p;
	struct sbuf *path;
	struct stat st;
	int ret, serrno;

	/* validate file name */
	for (p = de->d_name; *p; ++p) {
		if (!is_pfcs(*p) && *p != ' ') { /* XXX allow spaces for now */
			/* soft error */
			size_t len = strlen(de->d_name);
			size_t olen = percent_enclen(len);
			char *encpath = calloc(1, olen);
			if (0 == percent_encode(de->d_name, len, encpath, &olen)) {
				USERERROR("invalid character in file '%s/%s' [inode %lu]\n",
				       sbuf_data(parent), encpath,
				       (unsigned long)de->d_ino);
			} else {
				USERERROR("invalid character in file '%s/[inode %lu]'",
				       sbuf_data(parent), (unsigned long)de->d_ino);
			}
			free(encpath);
			return (0);
		}
	}
	/*
	 * XXX insufficient, the master process will reject names that
	 * start or end with a space
	 */

	/* check file type */
	if (fstatat(dd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
		if (errno == EACCES || errno == EPERM) {
			USERERROR("%s/%s inaccessible", sbuf_data(parent),
			    de->d_name);
			return (0);
		} else if (errno == ENOENT) {
			VERBOSE("%s/%s disappeared", sbuf_data(parent),
			    de->d_name);
			return (0);
		}
		/* hard error */
		ERROR("fstat(%s/%s): %s", sbuf_data(parent), de->d_name,
		    strerror(errno));
		return (-1);
	}

	/* full path */
	if ((path = sbuf_new_auto()) == NULL ||
	    sbuf_printf(path, "%s/%s", sbuf_data(parent), de->d_name) != 0 ||
	    sbuf_finish(path) != 0) {
		serrno = errno;
		sbuf_delete(path);
		errno = serrno;
		return (-1);
	}

	ret = 0;
	p = sbuf_data(path);
	if ((p[0] == '.' || p[0] == '/') && p[1] == '/')
		++p;
	switch (st.st_mode & S_IFMT) {
	case S_IFDIR:
		printf("%s/\n", p);
		if (tsdfx_scan_append(sp, path) == NULL) {
			/* hard error */
			ERROR("failed to append %s to scan list", p);
			ret = -1;
		}
		break;
	case S_IFREG:
		printf("%s\n", p);
		break;
	case S_IFLNK:
		/* soft error */
		USERERROR("ignoring symlink %s", p);
		break;
	default:
		/* soft error */
		USERERROR("found strange file: %s (%#o)", p,
		    st.st_mode & S_IFMT);
		break;
	}
	sbuf_delete(path);
	return (ret);
}

/*
 * Process a single worklist entry (directory).
 */
static int
tsdfx_scan_process_directory(struct scanpath *sp, const struct sbuf *path)
{
	DIR *dir;
	struct dirent *de;
	int dd, ret, serrno;

	ret = 0;
	if ((dd = open(sbuf_data(path), O_RDONLY)) < 0) {
		if (errno == ENOENT) {
			VERBOSE("%s disappeared", sbuf_data(path));
			return (0);
		} else if (errno == EACCES || errno == EPERM) {
			USERERROR("%s inaccessible", sbuf_data(path));
			return (0);
		}
		ERROR("%s: %s", sbuf_data(path), strerror(errno));
		return (-1);
	}
	if ((dir = fdopendir(dd)) == NULL) {
		ERROR("%s: %s", sbuf_data(path), strerror(errno));
		return (-1);
	}
	while (ret == 0 && (de = readdir(dir)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		/* ignore all entries that start with a period */
		if (de->d_name[0] == '.') {
			size_t len = strlen(de->d_name);
			size_t olen = percent_enclen(len);
			char *encpath = calloc(1, olen);
			if (0 == percent_encode(de->d_name, len, encpath, &olen)) {
				USERERROR("ignoring dot file '%s/%s' [inode %lu]\n",
				    sbuf_data(path), encpath,
				       (unsigned long)de->d_ino);
			} else {
				USERERROR("ignoring dot file '%s/[inode %lu]'",
				       sbuf_data(path), (unsigned long)de->d_ino);
			}
			free(encpath);
			continue;
		}
		if (tsdfx_process_dirent(sp, path, dd, de) != 0)
			ret = -1;
		else {
			sp->processed++;
			if (0 != maxfiles && sp->processed >= maxfiles) {
				USERERROR("too many files in source, please reduce file count using zip/tar.");
				ret = -1;
			}
		}
	}
	serrno = errno;
	closedir(dir);
	errno = serrno;
	return (ret);
}

/*
 * Entry point for the directory scanner child process.
 *
 * This process scans through the specified directory and all its
 * subdirectories and prints the name of every regular file it finds.  It
 * ignores symlinks and files or directories whose names contain
 * characters outside the POSIX portable filename character set.
 */
int
tsdfx_scanner(const char *path)
{
	struct scan_entry *se;
	struct scanpath *sp;
	int serrno;
	struct timespec timer_end, timer_start;

	if ((sp = tsdfx_scan_init(path)) == NULL)
		return (-1);

#define ELAPSED(start, end) ((double)(end.tv_sec - start.tv_sec + (end.tv_nsec - start.tv_nsec)/(double)1e9))
	clock_gettime(CLOCK_MONOTONIC, &timer_start);

	while ((se = tsdfx_scan_next(sp)) != NULL) {
		if (tsdfx_scan_process_directory(sp, se->path) != 0) {
			serrno = errno;
			tsdfx_scan_free(se);
			tsdfx_scan_cleanup(sp);
			errno = serrno;
			clock_gettime(CLOCK_MONOTONIC, &timer_end);
			VERBOSE("FAILED scanning directory '%s', measured time: %.3lf s", se->path, ELAPSED(timer_start, timer_end));
			return (-1);
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &timer_end);
	ASSERT(sp->todo == NULL && sp->tail == NULL);
	VERBOSE("found %li dir entries, measured time: %.3lf s",
	       sp->processed, path, ELAPSED(timer_start, timer_end));
	tsdfx_scan_cleanup(sp);
	sp = NULL;
	return (0);
}

static void
usage(void)
{

	fprintf(stderr, "usage: tsdfx-scanner [-v] [-l logname] [-m maxfiles] path\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	char *end;
	const char *logfile, *userlog;
	int opt;

	logfile = userlog = NULL;
	while ((opt = getopt(argc, argv, "hl:m:v")) != -1)
		switch (opt) {
		case 'l':
			if (strncmp(optarg, ":user=", 6) == 0)
				userlog = optarg + 6;
			else
				logfile = optarg;
			break;
		case 'm':
			maxfiles = strtol(optarg, &end, 10);
			if (end == optarg || *end != '\0' || maxfiles < 0) {
				fprintf(stderr, "unable to parse scan limit");
				usage();
			}
			break;
		case 'v':
			++tsd_log_verbose;
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage();

	tsd_log_init("tsdfx-scanner", logfile);
	tsd_log_userlog(userlog);

	if (getuid() == 0 || geteuid() == 0)
		WARNING("running as root for %s", argv[0]);

	if (tsdfx_scanner(argv[0]) != 0)
		exit(1);
	exit(0);
}
