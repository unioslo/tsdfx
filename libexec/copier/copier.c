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
#include <sys/time.h>

#if HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#else
#undef HAVE_STATVFS
#endif

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tsd/log.h>
#include <tsd/sha1.h>
#include <tsd/strutil.h>

static int tsdfx_dryrun;
static int tsdfx_force;

static mode_t mumask;

/* XXX make these configurable */
/* how much to attempt to copy at a time */
#define BLOCKSIZE	1024576
/* how many tenths of a second to wait for more data after EOF */
#define MAX_RETRIES	100

struct copyfile {
	char		 name[PATH_MAX];
	int		 fd;
	int		 mode;
	struct stat	 st;
	struct timeval	 tvo, tvf, tve;
	sha1_ctx	 sha_ctx;
	uint8_t		 digest[SHA1_DIGEST_LEN];
	off_t		 offset;
	size_t		 bufsize, buflen;
	char		 buf[];
};

static struct copyfile *copyfile_open(const char *, int, int);
static int copyfile_refresh(struct copyfile *);
static int copyfile_read(struct copyfile *);
static int copyfile_compare(struct copyfile *, struct copyfile *);
static int copyfile_comparestat(struct copyfile *, struct copyfile *);
static void copyfile_copy(struct copyfile *, struct copyfile *);
static void copyfile_copystat(struct copyfile *, struct copyfile *);
static int copyfile_write(struct copyfile *);
static void copyfile_advance(struct copyfile *);
static int copyfile_finish(struct copyfile *);
static void copyfile_close(struct copyfile *);

/* open a file and populate the state structure */
static struct copyfile *
copyfile_open(const char *fn, int mode, int perm)
{
	struct copyfile *cf;
	size_t len;
	int isdir;

	/* allocate state structure */
	if ((cf = calloc(1, sizeof *cf + BLOCKSIZE)) == NULL)
		goto fail;
	sha1_init(&cf->sha_ctx);
	cf->bufsize = BLOCKSIZE;

	/* copy name, check for trailing /, then strip it off */
	if ((len = strlcpy(cf->name, fn, sizeof cf->name)) >= sizeof cf->name) {
		errno = ENAMETOOLONG;
		goto fail;
	}
	if ((isdir = (cf->name[len - 1] == '/')))
		cf->name[len - 1] = '\0';

	/* sanitize permissions and mode */
	perm &= 0777;
	errno = EINVAL;
	switch (mode & (O_RDONLY|O_RDWR|O_WRONLY)) {
	case O_WRONLY:
		if (isdir)
			goto fail;
	case O_RDONLY:
	case O_RDWR:
		break;
	default:
		goto fail;
	}
	if ((mode & O_RDONLY) && (mode & (O_APPEND|O_CREAT|O_TRUNC)) != 0)
		goto fail;
	/* remember the requested mode */
	cf->mode = mode & (O_RDONLY|O_RDWR|O_WRONLY);
	/* directories aren't writeable */
	if (isdir && (mode & O_RDWR))
		mode = (mode & ~O_RDWR) | O_RDONLY;

	/* record start time */
	if ((gettimeofday(&cf->tvo, NULL)) != 0)
		goto fail;

	/* first, try to open existing file or directory */
	if ((cf->fd = open(fn, mode & ~O_CREAT)) < 0) {
		/* if the caller did not request creation, fail */
		if (!(errno == ENOENT && (mode & O_CREAT)))
			goto fail;
		/* otherwise, create it */
		if (isdir) {
			if (mkdir(cf->name, perm) != 0) {
				ERROR("%s: mkdir(): %s", cf->name, strerror(errno));
				goto fail;
			}
			NOTICE("created directory %s", cf->name);
			if ((cf->fd = open(fn, cf->mode)) < 0)
				goto fail;
		} else {
			if ((cf->fd = open(cf->name, mode, perm)) < 0) {
				ERROR("%s: open(): %s", cf->name, strerror(errno));
				goto fail;
			}
			NOTICE("created file %s", cf->name);
		}
	}
	if (fstat(cf->fd, &cf->st) != 0)
		goto fail;

	/* did we expect a file but find a directory, or the reverse ? */
	if (isdir && !S_ISDIR(cf->st.st_mode)) {
		errno = ENOTDIR;
		goto fail;
	}
	if (!isdir && S_ISDIR(cf->st.st_mode)) {
		errno = EISDIR;
		goto fail;
	}

	/* good */
	return (cf);
fail:
	if (cf != NULL)
		copyfile_close(cf);
	return (NULL);
}

/* re-stat the file, see if someone's pulled the rug under our feet */
static int
copyfile_refresh(struct copyfile *cf)
{
	struct stat st;

	if (stat(cf->name, &st) != 0) {
		ERROR("%s: %s", cf->name, strerror(errno));
		return (-1);
	}
	if (st.st_dev != cf->st.st_dev ||
	    st.st_ino != cf->st.st_ino) {
		ERROR("%s has moved", cf->name);
		errno = ESTALE;
		return (-1);
	}
	if (st.st_uid != cf->st.st_uid || st.st_gid != cf->st.st_gid)
		WARNING("%s: owner changed from %lu:%lu to %lu:%lu", cf->name,
		    (unsigned long)cf->st.st_uid, (unsigned long)cf->st.st_gid,
		    (unsigned long)st.st_uid, (unsigned long)st.st_gid);
	if (st.st_mode != cf->st.st_mode)
		WARNING("%s: mode has changed from %04o to %04o", cf->name,
		    (int)cf->st.st_mode, (int)st.st_mode);
	if (st.st_mtime < cf->st.st_mtime)
		WARNING("%s: mtime went backwards", cf->name);
	if (st.st_size < cf->st.st_size)
		WARNING("%s: truncated", cf->name);
	cf->st = st;
	return (0);
}

/* return 1 if file a directory, 0 otherwise; also sets errno */
static int
copyfile_isdir(const struct copyfile *cf)
{

	if (S_ISDIR(cf->st.st_mode)) {
		errno = EISDIR;
		return (1);
	} else {
		errno = ENOTDIR;
		return (0);
	}
}

/* read a block */
static int
copyfile_read(struct copyfile *cf)
{
	ssize_t rlen;

	if (copyfile_isdir(cf))
		return (-1);
	if ((rlen = read(cf->fd, cf->buf, cf->bufsize)) < 0) {
		ERROR("%s: read(): %s", cf->name, strerror(errno));
		return (-1);
	}
	if ((cf->buflen = (size_t)rlen) < cf->bufsize)
		memset(cf->buf + cf->buflen, 0, cf->bufsize - cf->buflen);
	return (0);
}

/* compare buffer length and content */
static int
copyfile_compare(struct copyfile *src, struct copyfile *dst)
{

	if (src->buflen != dst->buflen)
		return (-1);
	if (memcmp(src->buf, dst->buf, src->buflen) != 0)
		return (-1);
	return (0);
}

/* compare file permissions, size and times */
static int
copyfile_comparestat(struct copyfile *src, struct copyfile *dst)
{

	/* either both directories or both files */
	if (copyfile_isdir(src) != copyfile_isdir(dst))
		return (-1);
	/* same permissions, modulo umask */
	if ((src->st.st_mode & ~mumask) != dst->st.st_mode)
		return (-1);
	/* don't check size & mtime on directories */
	if (copyfile_isdir(src))
		return (0);
	/* same size, unless they're directories */
	if (src->st.st_size != dst->st.st_size)
		return (-1);
	/* same modification time */
	if (src->st.st_mtime != dst->st.st_mtime)
		return (-1);
	return (0);
}

/* copy buffer from one state structure to another */
static void
copyfile_copy(struct copyfile *src, struct copyfile *dst)
{

	assert(dst->bufsize >= src->bufsize);
	memcpy(dst->buf, src->buf, src->buflen);
	if ((dst->buflen = src->buflen) < dst->bufsize)
		memset(dst->buf + dst->buflen, 0, dst->bufsize - dst->buflen);
}

/* copy mode + times from one state structure to another */
static void
copyfile_copystat(struct copyfile *src, struct copyfile *dst)
{

	dst->st.st_mode = src->st.st_mode;
	dst->st.st_atim = src->st.st_atim;
	dst->st.st_mtim = src->st.st_mtim;
}

/* write a block at the previous offset */
static int
copyfile_write(struct copyfile *cf)
{
	ssize_t wlen;

	if (copyfile_isdir(cf))
		return (-1);
	if (lseek(cf->fd, cf->offset, SEEK_SET) != cf->offset) {
		ERROR("%s: lseek(): %s", cf->name, strerror(errno));
		return (-1);
	}
	if ((wlen = write(cf->fd, cf->buf, cf->buflen)) != (ssize_t)cf->buflen) {
		ERROR("%s: write(): %s", cf->name, strerror(errno));
		return (-1);
	}
	return (0);
}

/* update the running digest */
static void
copyfile_advance(struct copyfile *cf)
{

	sha1_update(&cf->sha_ctx, cf->buf, cf->buflen);
	cf->offset += cf->buflen;
}


/* truncate at current offset, finalize digest */
static int
copyfile_finish(struct copyfile *cf)
{
	struct timeval times[2];
	int mode;

	gettimeofday(&cf->tvf, NULL);
	timersub(&cf->tvf, &cf->tvo, &cf->tve);
	if (cf->mode & O_RDWR) {
		if (!copyfile_isdir(cf) && ftruncate(cf->fd, cf->offset) != 0) {
			ERROR("%s: ftruncate(): %s", cf->name, strerror(errno));
			return (-1);
		}
		mode = (cf->st.st_mode & 07777) | 0600; // force u+rw
		mode &= ~mumask; // apply umask
		if (fchmod(cf->fd, mode) != 0) {
			ERROR("%s: fchmod(%04o): %s", cf->name, mode, strerror(errno));
			return (-1);
		}
		times[0].tv_sec = cf->st.st_atim.tv_sec;
		times[0].tv_usec = cf->st.st_atim.tv_nsec / 1000;
		times[1].tv_sec = cf->st.st_mtim.tv_sec;
		times[1].tv_usec = cf->st.st_mtim.tv_nsec / 1000;
		if (futimes(cf->fd, times) != 0) {
			ERROR("%s: futimes(): %s", cf->name, strerror(errno));
			return (-1);
		}
	}
	sha1_final(&cf->sha_ctx, cf->digest);
	memset(&cf->sha_ctx, 0, sizeof cf->sha_ctx);
	return (0);
}

/* close */
static void
copyfile_close(struct copyfile *cf)
{

	if (cf->fd >= 0)
		close(cf->fd);
	memset(cf, 0, sizeof *cf + cf->bufsize);
	free(cf);
}

/* log a completed transfer */
void
tsdfx_log_complete(const struct copyfile *src, const struct copyfile *dst)
{
	char hex[SHA1_DIGEST_LEN * 2 + 1];
	int i;

	for (i = 0; i < SHA1_DIGEST_LEN; ++i) {
		hex[i * 2] = "0123456789abcdef"[dst->digest[i] >> 4];
		hex[i * 2 + 1] = "0123456789abcdef"[dst->digest[i] & 0xf];
	}
	hex[i * 2] = 0;
	NOTICE("copied %s to %s len %zu bytes sha1 %s in %lu.%03lu s",
	    src->name, dst->name, (size_t)dst->st.st_size, hex,
	    (unsigned long)dst->tve.tv_sec,
	    (unsigned long)dst->tve.tv_usec / 1000);
}

/* read from both files, compare and write if necessary */
int
tsdfx_copier(const char *srcfn, const char *dstfn)
{
#if HAVE_STATVFS
	struct statvfs st;
	off_t needbytes;
#endif
	struct copyfile *src, *dst;
	int retries, serrno;

	/* check file names */
	/* XXX should also compare type (trailing /) */
	if (!srcfn || !dstfn || !*srcfn || !*dstfn) {
		errno = EINVAL;
		return (-1);
	}
	VERBOSE("%s to %s", srcfn, dstfn);

	/*
	 * It would be nice to have a fully implemented dry-run mode:
	 *
	 * - Check that we have permission to create or write to the
         *   destination file.
	 * - If the destination file exists, open it read-only, otherwise
         *   open /dev/null.
	 * - Read and compare as usual, but do not write.
	 * - Do not set mode and times at the end.
	 *
	 * Until we have time to implement this, just sleep and return.
	 */
	if (tsdfx_dryrun) {
		sleep(5);
		return (0);
	}

	/* what's my umask? */
	umask(mumask = umask(0));

	/* open source and destination files */
	src = dst = NULL;
	if ((src = copyfile_open(srcfn, O_RDONLY, 0)) == NULL ||
	    (dst = copyfile_open(dstfn, O_RDWR|O_CREAT, 0600)) == NULL)
		goto fail;

	/* check that they are both the same type */
	if (copyfile_isdir(src) != copyfile_isdir(dst))
		goto fail;

	/* compare size and times */
	if (!tsdfx_force && copyfile_comparestat(src, dst) == 0) {
		VERBOSE("mode, size and mtime match");
		copyfile_close(src);
		copyfile_close(dst);
		return (0);
	}

	/* directories? */
	if (copyfile_isdir(src)) {
		copyfile_copystat(src, dst);
		if (copyfile_finish(src) != 0 ||
		    copyfile_finish(dst) != 0)
			goto fail;
		copyfile_close(src);
		copyfile_close(dst);
		return (0);
	}

#if HAVE_STATVFS
	/* check for available space */
	if (src->st.st_size > dst->st.st_size && fstatvfs(dst->fd, &st) == 0) {
		needbytes = src->st.st_size - dst->st.st_size;
		if (st.f_bavail < (needbytes + st.f_bsize - 1) / st.f_bsize) {
			WARNING("insufficient free space (need %zu bytes)",
			    (size_t)needbytes);
			/* don't leave an empty file */
			if (dst->st.st_size == 0)
				unlink(dstfn);
			goto fail;
		}
	}
#endif

	/* resumed? */
	if (dst->st.st_size > 0)
		NOTICE("resuming %s at %zu bytes", dst->name,
		    (size_t)dst->st.st_size);

	/* loop over the input and compare with the destination */
	retries = 0;
	for (;;) {
		if (copyfile_refresh(src) != 0 || copyfile_read(src) != 0 ||
		    copyfile_refresh(dst) != 0 || copyfile_read(dst) != 0)
			goto fail;
		if (src->buflen == 0) {
			/* wait a bit to see if it keeps growing */
			if (retries++ < MAX_RETRIES) {
				usleep(100 * 1000);
				continue;
			}
			/* end of source file */
			copyfile_copystat(src, dst);
			if (copyfile_finish(src) != 0 ||
			    copyfile_finish(dst) != 0)
				goto fail;
			if (memcmp(src->digest, dst->digest, SHA1_DIGEST_LEN) != 0) {
				ERROR("digest differs after copy");
				goto fail;
			}
			tsdfx_log_complete(src, dst);
			copyfile_close(src);
			copyfile_close(dst);
			return (0);
		}
		retries = 0;
		if (copyfile_compare(src, dst) != 0) {
			/* input and output differ */
			copyfile_copy(src, dst);
			if (copyfile_write(dst) != 0)
				goto fail;
		}
		copyfile_advance(src);
		copyfile_advance(dst);
	}
	/* not reached */
fail:
	serrno = errno;
	ERROR("failed to copy %s to %s", srcfn, dstfn);
	/* if we copied anything at all, we should log it here */
	if (src != NULL)
		copyfile_close(src);
	if (dst != NULL)
		copyfile_close(dst);
	errno = serrno;
	return (-1);
}

static void
usage(void)
{

	fprintf(stderr, "usage: tsdfx-copier [-nv] [-l logname] src dst\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	const char *logfile;
	int opt;

	while ((opt = getopt(argc, argv, "fhl:nv")) != -1)
		switch (opt) {
		case 'f':
			++tsdfx_force;
			break;
		case 'l':
			logfile = optarg;
			break;
		case 'n':
			++tsdfx_dryrun;
			break;
		case 'v':
			++tsd_log_verbose;
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();

	tsd_log_init("tsdfx-copier", logfile);

	if (getuid() == 0 || geteuid() == 0)
		WARNING("running as root");

	if (tsdfx_copier(argv[0], argv[1]) != 0)
		exit(1);
	exit(0);
}
