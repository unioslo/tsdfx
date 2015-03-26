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
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if HAVE_BSD_UNISTD_H
#include <bsd/unistd.h>
#endif

#include <tsd/ctype.h>
#include <tsd/log.h>
#include <tsd/sha1.h>
#include <tsd/strutil.h>
#include <tsd/task.h>

#include "tsdfx.h"
#include "tsdfx_scan.h"

#define INITIAL_BUFFER_SIZE	(PATH_MAX * 2)
#define MAXIMUM_BUFFER_SIZE	(PATH_MAX * 1024)

/* XXX this needs to be configurable */
#define SCAN_INTERVAL		300

/*
 * Private data for a scan task
 */
struct tsdfx_scan_task_data {
	/* what to scan */
	char path[PATH_MAX];
	struct stat st;

	/* when to scan */
	time_t lastran, nextrun;
	int interval;

	/* scanned files */
	char *buf;
	size_t bufsz;
	int buflen;
};

/*
 * Task set and queues
 */
static struct tsd_tset *tsdfx_scan_tasks;

/* max concurrent scan tasks */
unsigned int tsdfx_scan_max_tasks = 8;

/* full path to scanner binary */
const char *tsdfx_scanner;

static void tsdfx_scan_name(char *, const char *);
static int tsdfx_scan_slurp(struct tsd_task *);
static void tsdfx_scan_child(void *);

static int tsdfx_scan_add(struct tsd_task *);
static int tsdfx_scan_remove(struct tsd_task *);
static int tsdfx_scan_start(struct tsd_task *);
static int tsdfx_scan_stop(struct tsd_task *);

/*
 * Regular expression used to validate the output from the scan task.  It
 * must consist of zero or more lines, each representing a path.  Each
 * path must start but not end with a slash, and each slash must be
 * followed by a sequence of one or more characters from the POSIX
 * Portable Filename Character Set, the first of which is not a period.
 *
 * XXX allow spaces as well for now
 */
#define SCAN_REGEX \
	"^((/[0-9A-Za-z_-]([ 0-9A-Za-z._-]*[0-9A-Za-z._-])?)+/?\n)*$"
static regex_t scan_regex;

/*
 * Generate a unique name for a scan task.
 */
static void
tsdfx_scan_name(char *name, const char *path)
{
	uint8_t digest[SHA1_DIGEST_LEN];
	sha1_ctx ctx;
	unsigned int i;

	sha1_init(&ctx);
	sha1_update(&ctx, "scan", sizeof "scan");
	sha1_update(&ctx, path, strlen(path) + 1);
	sha1_final(&ctx, digest);
	for (i = 0; i < SHA1_DIGEST_LEN; ++i) {
		name[i * 2] = "0123456789abcdef"[digest[i] / 16];
		name[i * 2 + 1] = "0123456789abcdef"[digest[i] % 16];
	}
	name[i * 2] = '\0';
}

/*
 * Return the state of a scan task.
 */
enum tsd_task_state
tsdfx_scan_state(const struct tsd_task *t)
{

	return (t->state);
}

/*
 * Return the result of the scan.
 */
const char *
tsdfx_scan_result(const struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;

	if (t->state != TASK_FINISHED)
		return (NULL);
	return (std->buf);
}

/*
 * Add a task to the task list.
 */
int
tsdfx_scan_add(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;

	VERBOSE("%s", std->path);
	if (tsd_tset_insert(tsdfx_scan_tasks, t) != 0)
		return (-1);
	VERBOSE("%d jobs, %d running", tsdfx_scan_tasks->ntasks,
	    tsdfx_scan_tasks->nrunning);
	return (0);
}

/*
 * Remove a task from the task list.
 */
static int
tsdfx_scan_remove(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;

	VERBOSE("%s", std->path);
	if (tsd_tset_remove(tsdfx_scan_tasks, t) != 0)
		return (-1);
	VERBOSE("%d jobs, %d running", tsdfx_scan_tasks->ntasks,
	    tsdfx_scan_tasks->nrunning);
	return (0);
}

/*
 * Prepare a scan task.
 */
struct tsd_task *
tsdfx_scan_new(const char *path)
{
	char name[NAME_MAX];
	struct tsdfx_scan_task_data *std = NULL;
	struct tsd_task *t = NULL;
	struct stat st;
	int serrno;

	/* check that the source exists */
	if (stat(path, &st) != 0)
		return (NULL);
	if (!S_ISDIR(st.st_mode)) {
		errno = ENOTDIR;
		return (NULL);
	}

	/* check for existing task */
	tsdfx_scan_name(name, path);
	if (tsd_tset_find(tsdfx_scan_tasks, name) != NULL) {
		errno = EEXIST;
		goto fail;
	}

	/* create task data */
	if ((std = calloc(1, sizeof *std)) == NULL)
		goto fail;
	if (strlcpy(std->path, path, sizeof std->path) >= sizeof std->path)
		goto fail;
	std->st = st;
	std->bufsz = INITIAL_BUFFER_SIZE;
	std->buflen = 0;
	if ((std->buf = malloc(std->bufsz)) == NULL)
		goto fail;
	std->buf[0] = '\0';
	std->interval = SCAN_INTERVAL; /* XXX should be tunable */

	/* create task and set credentials */
	if ((t = tsd_task_create(name, tsdfx_scan_child, std)) == NULL)
		goto fail;
	t->flags = TASK_STDIN_NULL | TASK_STDOUT_PIPE;
	if (tsd_task_setcred(t, st.st_uid, &st.st_gid, 1) != 0)
		goto fail;
	if (tsdfx_scan_add(t) != 0)
		goto fail;
	return (t);
fail:
	serrno = errno;
	if (std != NULL) {
		if (std->buf != NULL)
			free(std->buf);
		free(std);
	}
	if (t != NULL)
		tsd_task_destroy(t);
	errno = serrno;
	return (NULL);
}

/*
 * Scan task child: execute the scanner program.
 */
static void
tsdfx_scan_child(void *ud)
{
	struct tsdfx_scan_task_data *std = ud;
	const char *argv[4];
	int argc;

	/* check credentials */
	if (std->st.st_uid == 0)
		WARNING("scanning %s with uid 0", std->path);
	if (std->st.st_gid == 0)
		WARNING("scanning %s with gid 0", std->path);

	/* change into the target directory, chroot if possible */
	// XXX chroot code removed, move this into tsd_task_start()
	if (chdir(std->path) != 0) {
		ERROR("%s: %s", std->path, strerror(errno));
		_exit(1);
	}

	/* run the scan task */
	argc = 0;
	argv[argc++] = tsdfx_scanner;
	if (tsdfx_verbose)
		argv[argc++] = "-v";
	argv[argc++] = ".";
	argv[argc] = NULL;
	/* XXX should clean the environment */
	execv(tsdfx_scanner, (char * const *)argv);
	ERROR("failed to execute scanner process");
	_exit(1);
}

/*
 * Start a scan task.
 */
static int
tsdfx_scan_start(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;

	VERBOSE("%s", std->path);
	if (t->state == TASK_RUNNING)
		return (0);
	if (tsd_task_start(t) != 0)
		return (-1);
	VERBOSE("%d jobs, %d running", tsdfx_scan_tasks->ntasks,
	    tsdfx_scan_tasks->nrunning);
	return (0);
}

/*
 * Stop a scan task.
 */
static int
tsdfx_scan_stop(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;

	VERBOSE("%s", std->path);
	if (tsd_task_stop(t) != 0)
		return (-1);
	return (0);
}

/*
 * Delete a scan task.
 */
void
tsdfx_scan_delete(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std;

	if (t == NULL)
		return;

	std = t->ud;

	VERBOSE("%s", std->path);
	tsdfx_scan_stop(t);
	tsdfx_scan_remove(t);
	tsd_task_destroy(t);
	free(std->buf);
	free(std);
}

/*
 * Reset a scan task.
 */
int
tsdfx_scan_reset(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;
	struct stat st;

	VERBOSE("%s", std->path);

	/* stop and reset to idle */
	if (t->state == TASK_IDLE)
		return (0);
	tsd_task_reset(t);

	/* clear the buffer */
	std->buf[0] = '\0';
	std->buflen = 0;

	/* check that it's still there */
	if (stat(std->path, &st) != 0) {
		WARNING("%s has disappeared", std->path);
		t->state = TASK_INVALID;
		return (-1);
	}

	/* re-stat and check for suspicious changes */
	if (!S_ISDIR(st.st_mode)) {
		WARNING("%s is no longer a directory", std->path);
		t->state = TASK_INVALID;
		return (-1);
	}
	if (st.st_uid != std->st.st_uid)
		WARNING("%s owner changed from %ld to %ld", std->path,
		    (long)std->st.st_uid, (long)st.st_uid);
	if (st.st_gid != std->st.st_gid)
		WARNING("%s group changed from %ld to %ld", std->path,
		    (long)std->st.st_gid, (long)st.st_gid);
	std->st = st;

	/* reschedule */
	std->nextrun = time(&std->lastran) + std->interval;

	return (0);
}

/*
 * Read all available data from a single task, validating it as we go.
 */
static int
tsdfx_scan_slurp(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;
	size_t bufsz;
	ssize_t rlen;
	char *buf, *p;
	int len;

	VERBOSE("%s", std->path);
	p = std->buf + std->buflen;
	len = 0;
	do {
		/* make sure we have room for at least one more character */
		if (std->buflen + 2 >= (int)std->bufsz) {
			bufsz = std->bufsz * 2;
			if (bufsz > MAXIMUM_BUFFER_SIZE)
				bufsz = MAXIMUM_BUFFER_SIZE;
			if (bufsz <= std->bufsz) {
				errno = ENOSPC;
				return (-1);
			}
			if ((buf = realloc(std->buf, bufsz)) == NULL)
				return (-1);
			std->buf = buf;
			std->bufsz = bufsz;
		}

		/* where do we start, and how much room do we have? */
		buf = std->buf + std->buflen;
		bufsz = std->bufsz - std->buflen - 1;

		/* read and update pointers and counters */
		if ((rlen = read(t->pout, buf, bufsz)) < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return (rlen);
		}
		VERBOSE("read %ld characters", (long)rlen);
		std->buflen += rlen;
		std->buf[std->buflen] = '\0';
		len += rlen;
	} while (rlen > 0);
	return (len);
}

/*
 * Poll the state of a child process.
 */
static int
tsdfx_scan_poll(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;
	struct pollfd pfd;
	int serrno;

	/* see if there's any output waiting for us */
	pfd.fd = t->pout;
	pfd.events = POLLIN;
	pfd.revents = 0;
	switch (poll(&pfd, 1, 0)) {
	case 1:
		/* yes, let's get it */
		if (pfd.revents & POLLIN) {
			if (tsdfx_scan_slurp(t) < 0)
				if (tsdfx_scan_stop(t) == 0)
					t->state = TASK_FAILED;
		}
		if (pfd.revents & POLLHUP) {
			if (tsdfx_scan_stop(t) == 0)
				t->state = TASK_FINISHED;
		}
		break;
	case 0:
		/* no, process has terminated */
		if (tsdfx_scan_stop(t) == 0) {
			/* validate */
			if (regexec(&scan_regex, std->buf, 0, NULL, 0) != 0) {
				WARNING("invalid output from child %ld for %s",
				    (long)t->pid, std->path);
				t->state = TASK_FAILED;
			} else {
				t->state = TASK_FINISHED;
			}
		}
		break;
	default:
		/* oops */
		serrno = errno;
		tsdfx_scan_stop(t);
		VERBOSE("%s: %s", std->path, strerror(serrno));
		errno = serrno;
		t->state = TASK_FAILED;
		break;
	}

	/* and the verdict */
	switch (t->state) {
	case TASK_RUNNING:
		return (1);
	case TASK_FINISHED:
		return (0);
	default:
		return (-1);
	}
}

/*
 * Start any scheduled tasks
 */
int
tsdfx_scan_sched(void)
{
	struct tsdfx_scan_task_data *std;
	struct tsd_task *t, *tn;
	time_t now;

	time(&now);
	t = tsd_tset_first(tsdfx_scan_tasks);
	while (t != NULL) {
		/* look ahead so we can safely delete dead tasks */
		tn = tsd_tset_next(tsdfx_scan_tasks, t);
		std = t->ud;
		switch (t->state) {
		case TASK_IDLE:
			/* see if the task is due to start again */
			if (tsdfx_scan_tasks->nrunning < tsdfx_scan_max_tasks &&
			    now >= std->nextrun && tsdfx_scan_start(t) != 0)
				WARNING("failed to start t: %s",
				    strerror(errno));
			break;
		case TASK_RUNNING:
			/* see if there is any output waiting */
			tsdfx_scan_poll(t);
			break;
		case TASK_STOPPED:
		case TASK_DEAD:
		case TASK_FINISHED:
		case TASK_FAILED:
			// tsd_task_reset(t);
			break;
		default:
			/* unreachable */
			break;
		}
		t = tn;
	}
	return (tsdfx_scan_tasks->nrunning);
}

/*
 * Initialize the scanning subsystem
 */
int
tsdfx_scan_init(void)
{

	if (tsdfx_scanner == NULL &&
	    (tsdfx_scanner = getenv("TSDFX_SCANNER")) == NULL &&
	    access(tsdfx_scanner = "/usr/libexec/tsdfx-scanner", R_OK|X_OK) != 0 &&
	    access(tsdfx_scanner = "/usr/local/libexec/tsdfx-scanner", R_OK|X_OK) != 0 &&
	    access(tsdfx_scanner = "/opt/tsd/libexec/tsdfx-scanner", R_OK|X_OK) != 0) {
		ERROR("failed to locate scanner child");
		return (-1);
	}
	if (regcomp(&scan_regex, SCAN_REGEX, REG_EXTENDED|REG_NOSUB) != 0)
		return (-1);
	if ((tsdfx_scan_tasks = tsd_tset_create("tsdfx scanner")) == NULL)
		return (-1);
	return (0);
}
