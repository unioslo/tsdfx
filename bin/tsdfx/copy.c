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

#include <sys/stat.h>

#if HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#else
#undef HAVE_STATVFS
#endif

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if HAVE_BSD_UNISTD_H
#include <bsd/unistd.h>
#endif

#include <tsd/log.h>
#include <tsd/sha1.h>
#include <tsd/strutil.h>
#include <tsd/task.h>

#include "tsdfx.h"
#include "tsdfx_copy.h"

#define TSDFX_COPY_UMASK 007

int tsdfx_dryrun = 0;

/*
 * Private data for a copy task
 */
struct tsdfx_copy_task_data {
	/* what to copy */
	char src[PATH_MAX];
	char dst[PATH_MAX];
};

/*
 * Task set and queues
 */
static struct tsd_tset *tsdfx_copy_tasks;
// static struct tsd_tqueue *tsdfx_copy_queue[6];

/* max concurrent tasks per queue */
unsigned int tsdfx_copy_max_tasks = 8;

/* full path to copier binary */
const char *tsdfx_copier;

static void tsdfx_copy_name(char *, const char *, const char *);
static struct tsd_task *tsdfx_copy_find(const char *, const char *);
static int tsdfx_copy_poll(struct tsd_task *);
static void tsdfx_copy_child(void *);

static int tsdfx_copy_add(struct tsd_task *);
static int tsdfx_copy_remove(struct tsd_task *);
static void tsdfx_copy_delete(struct tsd_task *);
static int tsdfx_copy_start(struct tsd_task *);
static int tsdfx_copy_stop(struct tsd_task *);

/*
 * Generate a unique name for a copy task.
 */
static void
tsdfx_copy_name(char *name, const char *src, const char *dst)
{
	uint8_t digest[SHA1_DIGEST_LEN];
	sha1_ctx ctx;
	unsigned int i;

	sha1_init(&ctx);
	sha1_update(&ctx, "copy", sizeof "copy");
	sha1_update(&ctx, src, strlen(src) + 1);
	sha1_update(&ctx, dst, strlen(dst) + 1);
	sha1_final(&ctx, digest);
	for (i = 0; i < SHA1_DIGEST_LEN; ++i) {
		name[i * 2] = "0123456789abcdef"[digest[i] / 16];
		name[i * 2 + 1] = "0123456789abcdef"[digest[i] % 16];
	}
	name[i * 2] = '\0';
}

/*
 * Return the first copy task that matches the specified source and / or
 * destination.
 */
static struct tsd_task *
tsdfx_copy_find(const char *src, const char *dst)
{
	char name[NAME_MAX];

	tsdfx_copy_name(name, src, dst);
	return (tsd_tset_find(tsdfx_copy_tasks, name));
}

/*
 * Add a task to the task list.
 */
static int
tsdfx_copy_add(struct tsd_task *t)
{
	struct tsdfx_copy_task_data *ctd = t->ud;

	VERBOSE("%s -> %s", ctd->src, ctd->dst);
	if (tsd_tset_insert(tsdfx_copy_tasks, t) != 0)
		return (-1);
	VERBOSE("%d jobs, %d running", tsdfx_copy_tasks->ntasks,
	    tsdfx_copy_tasks->nrunning);
	return (0);
}

/*
 * Remove a task from the task list.
 */
static int
tsdfx_copy_remove(struct tsd_task *t)
{
	struct tsdfx_copy_task_data *ctd = t->ud;

	VERBOSE("%s -> %s", ctd->src, ctd->dst);
	if (tsd_tset_remove(tsdfx_copy_tasks, t) != 0)
		return (-1);
	VERBOSE("%d jobs, %d running", tsdfx_copy_tasks->ntasks,
	    tsdfx_copy_tasks->nrunning);
	return (0);
}

/*
 * Prepare a copy task.
 */
struct tsd_task *
tsdfx_copy_new(const char *src, const char *dst)
{
	char name[NAME_MAX];
	struct tsdfx_copy_task_data *ctd = NULL;
	struct tsd_task *t = NULL;
	struct stat st;
	struct passwd *pw;
	int serrno;

	/* check that the source exists */
	if (lstat(src, &st) != 0)
		return (NULL);

	/* check for existing task */
	tsdfx_copy_name(name, src, dst);
	if (tsd_tset_find(tsdfx_copy_tasks, name) != NULL) {
		errno = EEXIST;
		return (NULL);
	}

	/* create task data */
	if ((ctd = calloc(1, sizeof *ctd)) == NULL)
		goto fail;
	if (strlcpy(ctd->src, src, sizeof ctd->src) >= sizeof ctd->src ||
	    strlcpy(ctd->dst, dst, sizeof ctd->dst) >= sizeof ctd->dst) {
		errno = ENAMETOOLONG;
		goto fail;
	}

	/* create task and set credentials */
	if ((t = tsd_task_create(name, tsdfx_copy_child, ctd)) == NULL)
		goto fail;
	if ((pw = getpwuid(st.st_uid)) != NULL) {
		VERBOSE("setuser(\"%s\") for %s", pw->pw_name, dst);
		if (tsd_task_setuser(t, pw->pw_name) != 0)
			goto fail;
	} else {
		VERBOSE("getpwuid(%lu) failed; setcred(%lu, %lu) for %s",
		    (unsigned long)st.st_uid, (unsigned long)st.st_uid,
		    (unsigned long)st.st_gid, dst);
		if (tsd_task_setcred(t, st.st_uid, &st.st_gid, 1) != 0)
			goto fail;
	}
	if (tsdfx_copy_add(t) != 0)
		goto fail;
	return (t);
fail:
	serrno = errno;
	if (ctd != NULL)
		free(ctd);
	if (t != NULL)
		tsd_task_destroy(t);
	errno = serrno;
	return (NULL);
}

/*
 * Delete a copy task.
 */
static void
tsdfx_copy_delete(struct tsd_task *t)
{
	struct tsdfx_copy_task_data *ctd;

	if (t == NULL)
		return;

	ctd = t->ud;

	VERBOSE("%s -> %s", ctd->src, ctd->dst);
	tsdfx_copy_stop(t);
	tsdfx_copy_remove(t);
	tsd_task_destroy(t);
	free(ctd);
}

/*
 * Copy task child: execute the copier program.
 */
static void
tsdfx_copy_child(void *ud)
{
	struct tsdfx_copy_task_data *ctd = ud;
	const char *argv[6];
	int argc;

	/* check credentials */
	if (geteuid() == 0)
		WARNING("copying %s with uid 0", ctd->src);
	if (getegid() == 0)
		WARNING("copying %s with gid 0", ctd->src);

	/* set safe umask */
	umask(TSDFX_COPY_UMASK);

	/* run the copy task */
	argc = 0;
	argv[argc++] = tsdfx_copier;
	if (tsdfx_dryrun)
		argv[argc++] = "-n";
	if (tsdfx_verbose)
		argv[argc++] = "-v";
	argv[argc++] = ctd->src;
	argv[argc++] = ctd->dst;
	argv[argc] = NULL;
	/* XXX should clean the environment */
	execv(tsdfx_copier, (char *const *)argv);
	ERROR("failed to execute copier process");
}

/*
 * Start a copy task.
 */
static int
tsdfx_copy_start(struct tsd_task *t)
{
	struct tsdfx_copy_task_data *ctd = t->ud;

	VERBOSE("%s -> %s", ctd->src, ctd->dst);
	if (t->state == TASK_RUNNING)
		return (0);
	if (tsd_task_start(t) != 0)
		return (-1);
	VERBOSE("%d jobs, %d running", tsdfx_copy_tasks->ntasks,
	    tsdfx_copy_tasks->nrunning);
	return (0);
}

/*
 * Poll the state of a child process.
 */
static int
tsdfx_copy_poll(struct tsd_task *t)
{

	if (tsd_task_poll(t) != 0)
		return (-1);
	VERBOSE("%d jobs, %d running", tsdfx_copy_tasks->ntasks,
	    tsdfx_copy_tasks->nrunning);
	return (0);
}

/*
 * Stop a copy task.
 */
static int
tsdfx_copy_stop(struct tsd_task *t)
{
	struct tsdfx_copy_task_data *ctd = t->ud;

	VERBOSE("%s -> %s", ctd->src, ctd->dst);
	if (tsd_task_stop(t) != 0)
		return (-1);
	return (0);
}

/*
 * Given source and destination directories and a list of files to copy,
 * start copy tasks for each file.
 */
int
tsdfx_copy_wrap(const char *srcdir, const char *dstdir, const char *files)
{
#if HAVE_STATVFS
	struct statvfs st;
#endif
	struct stat srcst, dstst;
	char srcpath[PATH_MAX], *sf, dstpath[PATH_MAX], *df;
	size_t slen, dlen, maxlen;
	const char *p, *q;
	unsigned int mode;

	/* prime the source and destination paths */
	slen = strlcpy(srcpath, srcdir, sizeof srcpath);
	dlen = strlcpy(dstpath, dstdir, sizeof dstpath);
	if (slen >= sizeof srcpath || dlen >= sizeof dstpath) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	/* sf and df point to the terminating NULS */
	sf = srcpath + slen;
	df = dstpath + dlen;
	/* maxlen is the maximum acceptable file name length */
	slen = sizeof srcpath - slen - 1;
	dlen = sizeof dstpath - dlen - 1;
	maxlen = slen > dlen ? dlen : slen;

	for (p = q = files; *p != '\0'; p = ++q) {
		while (*q != '\0' && *q != '\n')
			++q;
		if (q == p) {
			/* empty line, XXX should warn */
			continue;
		}
		if ((size_t)(q - p) >= maxlen) {
			/* too long, XXX should warn */
			continue;
		}
		memcpy(sf, p, q - p);
		sf[q - p] = '\0';
		memcpy(df, p, q - p);
		df[q - p] = '\0';

		/* log and check for duplicate */
		VERBOSE("%s -> %s", srcpath, dstpath);
		if (tsdfx_copy_find(srcpath, dstpath) != NULL)
			continue;

		/* source must exist */
		if (lstat(srcpath, &srcst) != 0) {
			WARNING("%s: %s", srcpath, strerror(errno));
			continue;
		}

		/* ignore everything except files and directories */
		if (!S_ISREG(srcst.st_mode) && !S_ISDIR(srcst.st_mode)) {
			WARNING("%s: neither file nor directory", srcpath);
			continue;
		}

		/*
		 * Some SFTP clients seem to mangle file permissions so we
		 * sometimes end up with files or directories on the
		 * import side with weird permissions, or even none at
		 * all.  Try to force a sane minimum set of permissions.
		 */
		mode = srcst.st_mode;
		/* writeable for user, readable for group */
		if ((mode & 0640) != 0640)
			mode |= 0640;
		/* directories must also be searchable */
		if (S_ISDIR(mode) && (mode & 0110) != 0110)
			mode |= 0110;
		/* apply changes */
		if (mode != srcst.st_mode) {
			NOTICE("%s: changing permissions from %o to %o",
			    srcpath, srcst.st_mode & 07777, mode & 07777);
			if (chmod(srcpath, mode & 07777) != 0)
				WARNING("%s: %s", srcpath, strerror(errno));
			else
				srcst.st_mode = mode;
		}

		/* check destination */
		if (lstat(dstpath, &dstst) == 0) {
			if ((srcst.st_mode & S_IFMT) != (dstst.st_mode & S_IFMT)) {
				WARNING("%s and %s both exist with different types",
				    srcpath, dstpath);
				continue;
			}
			/*
			 * Attempt to avoid unnecessarily starting a
			 * copier child for a file that's already been
			 * copied.
			 * XXX hack
			 */
			srcst.st_mode &= ~TSDFX_COPY_UMASK;
			if (S_ISREG(srcst.st_mode) &&
			    srcst.st_size == dstst.st_size &&
			    srcst.st_mode == dstst.st_mode &&
			    srcst.st_mtime == dstst.st_mtime)
				continue;
			if (S_ISDIR(srcst.st_mode) &&
			    srcst.st_mode == dstst.st_mode)
				continue;
		} else {
			memset(&dstst, 0, sizeof dstst);
		}

#if HAVE_STATVFS
		/* check for available space */
		if (!S_ISDIR(srcst.st_mode) &&
		    srcst.st_size > dstst.st_size && statvfs(dstdir, &st) == 0 &&
		    (off_t)(st.f_bavail * st.f_bsize) < srcst.st_size - dstst.st_size)
			continue;
#endif

		/* create task */
		tsdfx_copy_new(srcpath, dstpath);
	}
	return (0);
}

/*
 * Monitor running tasks and start any scheduled tasks if possible.
 */
int
tsdfx_copy_sched(void)
{
	struct tsd_task *t, *tn;

	t = tsd_tset_first(tsdfx_copy_tasks);
	while (t != NULL) {
		/* look ahead so we can safely delete dead tasks */
		tn = tsd_tset_next(tsdfx_copy_tasks, t);
		switch (t->state) {
		case TASK_IDLE:
			if (tsdfx_copy_tasks->nrunning < tsdfx_copy_max_tasks &&
			    tsdfx_copy_start(t) != 0)
				WARNING("failed to start task: %s",
				    strerror(errno));
			break;
		case TASK_RUNNING:
			tsdfx_copy_poll(t);
			break;
		case TASK_STOPPED:
		case TASK_DEAD:
		case TASK_FINISHED:
		case TASK_FAILED:
			tsdfx_copy_delete(t);
			break;
		default:
			/* unreachable */
			break;
		}
		t = tn;
	}
	return (tsdfx_copy_tasks->nrunning);
}

/*
 * Initialize the copier subsystem
 */
int
tsdfx_copy_init(void)
{

	if (tsdfx_copier == NULL &&
	    (tsdfx_copier = getenv("TSDFX_COPIER")) == NULL &&
	    access(tsdfx_copier = "/usr/libexec/tsdfx-copier", R_OK|X_OK) != 0 &&
	    access(tsdfx_copier = "/usr/local/libexec/tsdfx-copier", R_OK|X_OK) != 0 &&
	    access(tsdfx_copier = "/opt/tsd/libexec/tsdfx-copier", R_OK|X_OK) != 0) {
		ERROR("failed to locate copier child");
		return (-1);
	}
	if ((tsdfx_copy_tasks = tsd_tset_create("tsdfx copier")) == NULL)
		return (-1);
	return (0);
}
