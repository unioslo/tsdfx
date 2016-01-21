/*-
 * Copyright (c) 2014-2016 The University of Oslo
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

#include <tsd/assert.h>
#include <tsd/ctype.h>
#include <tsd/log.h>
#include <tsd/sha1.h>
#include <tsd/strutil.h>
#include <tsd/task.h>

#include "tsdfx.h"
#include "tsdfx_map.h"
#include "tsdfx_scan.h"

#define SCAN_BUFFER_SIZE	16384

#define DEFAULT_SCAN_INTERVAL	300

unsigned int tsdfx_scan_interval;
unsigned int tsdfx_reset_interval;

struct tsdfx_scan_task_databuf {
	char *buf;
	size_t bufsz;
	int buflen;
};

/*
 * Private data for a scan task
 */
struct tsdfx_scan_task_data {
	/* what to scan */
	struct tsdfx_map *map;
	char path[PATH_MAX];
	struct stat st;

	/* when to scan */
	time_t lastran, nextrun;
	int interval;

	/* scanned files */
	struct tsdfx_scan_task_databuf stdin;

	/* error messages */
	struct tsdfx_scan_task_databuf stderr;
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
 * Regular expression used to validate the output from the scan task.
 * Each line of output represents a path which must start with a slash,
 * and each slash must be followed by a sequence of one or more characters
 * from the POSIX Portable Filename Character Set, the first of which is
 * not a period.  If the path is a directory, it ends with a slash.
 *
 * XXX allow spaces as well for now
 */
#define SCAN_REGEX \
	"^(/[0-9A-Za-z_-]([ 0-9A-Za-z._-]*[0-9A-Za-z._-])?)+/?$"
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
	return (std->stdin.buf);
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
	ASSERT(t->set == tsdfx_scan_tasks);
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
tsdfx_scan_new(struct tsdfx_map *map, const char *path)
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
	std->map = map;
	if (strlcpy(std->path, path, sizeof std->path) >= sizeof std->path)
		goto fail;
	std->st = st;
	std->stdin.bufsz = SCAN_BUFFER_SIZE;
	std->stdin.buflen = 0;
	if ((std->stdin.buf = malloc(std->stdin.bufsz)) == NULL)
		goto fail;
	std->stdin.buf[0] = '\0';
	std->stderr.bufsz = SCAN_BUFFER_SIZE;
	std->stderr.buflen = 0;
	if ((std->stderr.buf = malloc(std->stderr.bufsz)) == NULL)
		goto fail;
	std->stderr.buf[0] = '\0';
	std->interval = tsdfx_scan_interval;

	/* create task and set credentials */
	if ((t = tsd_task_create(name, tsdfx_scan_child, std)) == NULL)
		goto fail;
	//t->flags = TASK_STDIN_NULL | TASK_STDOUT_PIPE;
	t->flags = TASK_STDIN_NULL | TASK_STDOUT_PIPE | TASK_STDERR_PIPE;
	if (tsd_task_setcred(t, st.st_uid, &st.st_gid, 1) != 0)
		goto fail;
	if (tsdfx_scan_add(t) != 0)
		goto fail;
	return (t);
fail:
	serrno = errno;
	if (std != NULL) {
		if (std->stderr.buf != NULL) {
			free(std->stderr.buf);
			std->stderr.buf = NULL;
		}
		if (std->stdin.buf != NULL) {
			free(std->stdin.buf);
			std->stdin.buf = NULL;
		}
		free(std);
		std = NULL;
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
	const char *argv[8];
	int argc;

	/* check credentials */
	if (geteuid() == 0 || getegid() == 0)
		WARNING("scanning %s with uid %u gid %u", std->path,
		    (unsigned int)geteuid(), (unsigned int)getegid());

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
	argv[argc++] = "-l";
	argv[argc++] = tsd_log_getname();
	/*
	 * Always log user errors to stderr.  We pick up the log messages
	 * and pass them on to the final destination.
	 */
	argv[argc++] = "-l";
	argv[argc++] = ":usererror=stderr";
	argv[argc++] = ".";
	argv[argc] = NULL;
	ASSERTF((size_t)argc < sizeof argv / sizeof argv[0],
	    "argv overflowed: %d > %z", argc, sizeof argv / sizeof argv[0]);
	/* XXX should clean the environment */
	execv(tsdfx_scanner, (char * const *)(uintptr_t)argv);
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
	if (t->state != TASK_RUNNING && tsd_task_start(t) != 0)
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
	if (t->state == TASK_RUNNING && tsd_task_stop(t) != 0)
		return (-1);
	VERBOSE("%d jobs, %d running", tsdfx_scan_tasks->ntasks,
	    tsdfx_scan_tasks->nrunning);
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
	tsdfx_scan_remove(t);
	tsd_task_destroy(t);
	VERBOSE("%d jobs, %d running", tsdfx_scan_tasks->ntasks,
	    tsdfx_scan_tasks->nrunning);
	free(std->stderr.buf);
	free(std->stdin.buf);
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
	time(&std->lastran);

	/* clear the buffer */
	std->stdin.buf[0] = '\0';
	std->stdin.buflen = 0;
	std->stderr.buf[0] = '\0';
	std->stderr.buflen = 0;

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
	std->nextrun = std->lastran + std->interval;

	return (0);
}

/*
 * Mark a scan task for immediate execution.
 */
int
tsdfx_scan_rush(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;
	time_t now;

	VERBOSE("%s", std->path);
	switch (t->state) {
	case TASK_IDLE:
		time(&now);
		if (std->nextrun > now)
			std->nextrun = now;
		/* fall through */
	case TASK_RUNNING:
		return (0);
	default:
		return (-1);
	}
}

/*
 * Read available data from a single task, validate it and start copiers.
 * Returns < 0 on error, > 0 if any data was read and / or is pending, and
 * 0 otherwise.
 */
static int
tsdfx_scan_slurp(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;
	size_t bufsz, len;
	ssize_t rlen;
	char *buf, *end, *p, *q;

	/* read as much as we can in the space we have left */
	len = 0;
	do {
		/* where do we start, and how much room do we have? */
		buf = std->stdin.buf + std->stdin.buflen;
		bufsz = std->stdin.bufsz - std->stdin.buflen;

		/* read and update pointers and counters */
		if ((rlen = read(t->pout, buf, bufsz)) < 0) {
			if (errno != EAGAIN && errno != EWOULDBLOCK)
				return (rlen);
			rlen = 0;
		}
		VERBOSE("read %ld characters from child %ld",
		    (long)rlen, (long)t->pid);
		std->stdin.buflen += rlen;
		std->stdin.buf[std->stdin.buflen] = '\0';
		len += rlen;
	} while (rlen > 0 && (size_t)len < bufsz);
	end = std->stdin.buf + std->stdin.buflen;

	/* process output line by line */
	for (p = q = std->stdin.buf; p < end; p = q) {
		for (q = p; q < end && *q != '\n'; ++q)
			/* nothing */ ;
		if (q == end)
			break;
		*q++ = '\0';
		if (regexec(&scan_regex, p, 0, NULL, 0) != 0) {
			WARNING("invalid output from child %ld for %s",
			    (long)t->pid, std->path);
			continue;
		}
		VERBOSE("[%s]", p);
		tsdfx_map_process(std->map, p);
	}

	/*
	 * After the above loop, p points to the first character of the
	 * first incomplete line, or the beginning of the buffer if it is
	 * empty or does not contain at least one line.  If the amount of
	 * data remaining exceeds the maximum length of a path name (not
	 * including the newline, which is still missing), something is
	 * wrong.  Otherwise, move what's left to the start of the buffer.
	 */
	if ((len = end - p) > PATH_MAX) {
		errno = ENAMETOOLONG;
		return (-1);
	}
	if (p > std->stdin.buf) {
		memmove(std->stdin.buf, p, std->stdin.buflen = len);
		VERBOSE("left over: [%.*s]", (int)std->stdin.buflen, std->stdin.buf);
	}

	return (rlen + std->stdin.buflen);
}

static int
tsdfx_scan_slurp_stderr(struct tsd_task *t)
{
	struct tsdfx_scan_task_data *std = t->ud;
	size_t bufsz, len;
	ssize_t rlen;
	char *buf, *end, *p, *q;

	WARNING("Reading from stderr");
	/* read as much as we can in the space we have left */
	len = 0;
	do {
		/* where do we start, and how much room do we have? */
		buf = std->stderr.buf + std->stderr.buflen;
		bufsz = std->stderr.bufsz - std->stderr.buflen;

		/* read and update pointers and counters */
		if ((rlen = read(t->perr, buf, bufsz)) < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return (rlen);
		}
		VERBOSE("read %ld stderr characters from child %ld",
		    (long)rlen, (long)t->pid);
		std->stderr.buflen += rlen;
		std->stderr.buf[std->stderr.buflen] = '\0';
		len += rlen;
	} while (rlen > 0 && (size_t)len < bufsz);
	end = std->stderr.buf + std->stderr.buflen;

	/* process output line by line */
	for (p = q = std->stderr.buf; p < end; p = q) {
		for (q = p; q < end && *q != '\n'; ++q)
			/* nothing */ ;
		if (q == end)
			break;
		*q++ = '\0';
		tsdfx_map_log(std->map, p);
		ERROR("%s", p);
	}

	/*
	 * After the above loop, q points to the first character of
	 * the first incomplete line, or the beginning of the buffer
	 * if it is empty or does not contain at least one line.  Move
	 * what's left to the start of the buffer.
	 */
	if (q > std->stderr.buf)
		memmove(std->stderr.buf, q, std->stderr.buflen = len);

	return (len);
}

/*
 * Poll the state of a child process.
 */
static int
tsdfx_scan_poll(struct tsd_task *t)
{
	int events;
	struct tsdfx_scan_task_data *std = t->ud;
	struct pollfd pfd[2];
	int ret, serrno;

	/*
	 * See if there's any output waiting for us.
	 *
	 * Linux and FreeBSD behave differently here.  Linux will return
	 * POLLIN until the other end of the pipe is closed (i.e. the scan
	 * child has terminated).  It will then return POLLIN|POLLHUP
	 * until we've read all the data that was buffered in the kernel.
	 * After that, it will return POLLHUP alone.  FreeBSD on the other
	 * hand may continue to return POLLIN|POLLHUP even after we've
	 * read everything.  Hence, the termination condition can't be
	 * "POLLHUP alone" but must be "either POLLHUP alone *or*
	 * POLLIN|POLLHUP and read() == 0".
	 */
	pfd[0].fd = t->pout;
	pfd[0].events = POLLIN;
	pfd[0].revents = 0;
	pfd[1].fd = t->perr;
	pfd[1].events = POLLIN;
	pfd[1].revents = 0;
	events = poll(pfd, (sizeof(pfd)/sizeof(pfd[0])), 0);
	switch (events) {
	case 1:
	case 2:
		if (pfd[0].revents & POLLIN) {
			/* yes, let's get it */
			if ((ret = tsdfx_scan_slurp(t)) < 0) {
				/* error in slurp(), kill task and bail */
				if (tsdfx_scan_stop(t) == 0)
					t->state = TASK_FAILED;
				break;
			}
			if (ret > 0)
				break;
		}
		if (pfd[1].revents & POLLIN) {
			/* yes, let's get it */
			if ((ret = tsdfx_scan_slurp_stderr(t)) < 0) {
				/* error in slurp(), kill task and bail */
				if (tsdfx_scan_stop(t) == 0)
					t->state = TASK_FAILED;
				break;
			}
			if (ret > 0)
				break;
		}
		if (pfd[0].revents & POLLHUP && tsdfx_scan_stop(t) == 0) {
			/* we're done */
			if (std->stdin.buflen > 0) {
				WARNING("incomplete output from child %ld for %s",
				    (long)t->pid, std->path);
				t->state = TASK_FAILED;
			} else {
				t->state = TASK_FINISHED;
			}
		}
		break;
	case 0:
		/* no input for now */
		break;
	default:
		/* oops */
		serrno = errno;
		tsdfx_scan_stop(t);
		VERBOSE("did not expect %d from poll() %s: %s",
			events, std->path, strerror(serrno));
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
		case TASK_FINISHED:
			tsd_task_reset(t);
			break;
		case TASK_DEAD:
		case TASK_FAILED:
		case TASK_INVALID:
			if (now >= std->lastran + tsdfx_reset_interval)
				tsd_task_reset(t);
			break;
		case TASK_STOPPED:
			/* shouldn't happen */
			ERROR("scan task in TASK_STOPPED state");
			tsd_task_reset(t);
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
	if (tsdfx_scan_interval == 0)
		tsdfx_scan_interval = DEFAULT_SCAN_INTERVAL;
	if (tsdfx_reset_interval == 0)
		tsdfx_reset_interval = tsdfx_scan_interval * 3;
	if (tsdfx_reset_interval < tsdfx_scan_interval) {
		WARNING("reset interval inferior to scan interval");
		tsdfx_reset_interval = tsdfx_scan_interval * 3;
		WARNING("setting reset interval to %u", tsdfx_reset_interval);
	}
	return (0);
}

int
tsdfx_scan_exit(void)
{
	struct tsd_task *t, *tn;

	t = tsd_tset_first(tsdfx_scan_tasks);
	while (t != NULL) {
		/* look ahead so we can safely delete dead tasks */
		tn = tsd_tset_next(tsdfx_scan_tasks, t);
		tsdfx_scan_delete(t);
		t = tn;
	}
	tsd_tset_destroy(tsdfx_scan_tasks);
	tsdfx_scan_tasks = NULL;
	regfree(&scan_regex);
	return (0);
}
