/*-
 * Copyright (c) 2014 Universitetet i Oslo
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

#include <assert.h>
#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <tsd/ctype.h>
#include <tsd/sbuf.h>
#include <tsd/strutil.h>

#include <tsdfx.h>

struct scan_entry {
	struct sbuf *path;
	struct scan_entry *next;
};

/*
 * Worklist
 */
static struct scan_entry *scan_todo, *scan_tail;

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
tsdfx_scan_append(const struct sbuf *path)
{
	struct scan_entry *se;

	// fprintf(stderr, "%s(\"%s\")\n", __func__, sbuf_data(path));
	if ((se = calloc(1, sizeof *se)) == NULL)
		return (NULL);
	if ((se->path = sbuf_new_auto()) == NULL ||
	    sbuf_cpy(se->path, sbuf_data(path)) == -1 ||
	    sbuf_finish(se->path) == -1)
		goto fail;
	if (scan_todo == NULL)
		scan_todo = scan_tail = se;
	else
		scan_tail = scan_tail->next = se;
	return (se);
fail:
	tsdfx_scan_free(se);
	return (NULL);
}

/*
 * Remove and return the next entry from the worklist.
 */
static struct scan_entry *
tsdfx_scan_next(void)
{
	struct scan_entry *se;

	if ((se = scan_todo) != NULL) {
		if ((scan_todo = se->next) == NULL) {
			assert(scan_tail == se);
			scan_tail = NULL;
		}
	}
	return (se);
}

/*
 * Initialize the worklist and file list.
 */
static int
tsdfx_scan_init(const char *root)
{
	struct scan_entry *se;

	if ((se = calloc(1, sizeof *se)) == NULL)
		return (-1);
	if ((se->path = sbuf_new_auto()) == NULL ||
	    sbuf_cpy(se->path, root) != 0)
		goto fail;
	scan_todo = scan_tail = se;
	return (0);
fail:
	tsdfx_scan_free(se);
	return (-1);
}

/*
 * Empty the worklist and file list.
 */
static void
tsdfx_scan_cleanup(void)
{
	struct scan_entry *se;

	while ((se = tsdfx_scan_next()) != NULL)
		tsdfx_scan_free(se);
}

/*
 * Process a directory entry.
 */
static int
tsdfx_process_dirent(const struct sbuf *parent, int dd, const struct dirent *de)
{
	const char *p;
	struct sbuf *path;
	struct stat st;
	int ret, serrno;

	/* validate file name */
	for (p = de->d_name; *p; ++p) {
		if (!is_pfcs(*p)) {
			warnx("invalid character in file %s/[%lu]",
			    sbuf_data(parent), (unsigned long)de->d_ino);
			/* soft error */
			return (0);
		}
	}

	/* check file type */
	if (fstatat(dd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) != 0) {
		warn("fstat(%s/%s)", sbuf_data(parent), de->d_name);
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
	switch (st.st_mode & S_IFMT) {
	case S_IFDIR:
		if (tsdfx_scan_append(path) == NULL) {
			warn("failed to append %s to scan list",
			    sbuf_data(path));
			ret = -1;
		}
		break;
	case S_IFREG:
		printf("%s\n", sbuf_data(path));
		break;
	case S_IFLNK:
		/* soft error */
		warnx("ignoring symlink %s", sbuf_data(path));
		break;
	default:
		/* soft error */
		warnx("found strange file: %s (%#o)", sbuf_data(path),
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
tsdfx_scan_process_directory(const struct sbuf *path)
{
	DIR *dir;
	struct dirent *de;
	int dd, serrno;

	if ((dd = open(sbuf_data(path), O_RDONLY)) < 0)
		return (-1);
	if ((dir = fdopendir(dd)) == NULL)
		return (-1);
	while ((de = readdir(dir)) != NULL) {
		if (strcmp(de->d_name, ".") == 0 ||
		    strcmp(de->d_name, "..") == 0)
			continue;
		if (tsdfx_process_dirent(path, dd, de) != 0)
			break;
	}
	serrno = errno;
	closedir(dir);
	return ((errno = serrno) ? -1 : 0);
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
	int serrno;

	if (tsdfx_scan_init(path) != 0)
		return (-1);
	while ((se = tsdfx_scan_next()) != NULL) {
		if (tsdfx_scan_process_directory(se->path) != 0) {
			serrno = errno;
			tsdfx_scan_free(se);
			tsdfx_scan_cleanup();
			errno = serrno;
			return (-1);
		}
	}
	assert(scan_todo == NULL && scan_tail == NULL);
	tsdfx_scan_cleanup();
	return (0);
}
