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
#include <sys/poll.h>
#include <sys/stat.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if HAVE_BSD_UNISTD_H
#include <bsd/unistd.h>
#endif

#include <tsd/ctype.h>
#include <tsd/strutil.h>

#include "tsdfx.h"
#include "tsdfx_log.h"
#include "tsdfx_task.h"
#include "tsdfx_scan.h"

#define INITIAL_BUFFER_SIZE	(PATH_MAX * 2)
#define MAXIMUM_BUFFER_SIZE	(PATH_MAX * 1024)

/* XXX this needs to be configurable */
#define SCAN_INTERVAL		60

struct scan_task {
	/* what to scan */
	char path[PATH_MAX];
	struct stat st;

	/* when to scan */
	time_t lastran, nextrun;
	int interval;

	/* place in the task list */
	int index;

	/* state */
	enum task_state state;

	/* child process */
	pid_t pid;
	int pd;

	/* scanned files */
	char *buf;
	size_t bufsz;
	int buflen;
};

static struct scan_task **scan_tasks;
static struct pollfd *scan_pipes;
static size_t scan_sz;
static int scan_len;

static inline void tsdfx_scan_invariant(const struct scan_task *);
static inline int tsdfx_scan_find(const struct scan_task *);

/*
 * Regular expression used to validate the output from the scan task.  It
 * must consist of zero or more lines, each representing a path.  Each
 * path must start but not end with a slash, and each slash must be
 * followed by a sequence of one or more characters from the POSIX
 * Portable Filename Character Set, the first of which is not a period.
 */
#define SCAN_REGEX \
	"^((/[0-9A-Za-z_-][0-9A-Za-z._-]*)+\n)*$"
static regex_t scan_regex;

/*
 * Debugging aid
 */
static inline void
tsdfx_scan_invariant(const struct scan_task *task)
{

	switch (task->state) {
	case TASK_IDLE:
		assert(task->buflen == 0);
	case TASK_FAILED:
	case TASK_FINISHED:
		assert(task->index < 0 || tsdfx_scan_find(task) >= 0);
		assert(task->pid == -1);
		assert(task->pd == -1);
		break;
	case TASK_RUNNING:
		assert(tsdfx_scan_find(task) >= 0);
		assert(task->pid > 0);
		assert(task->pd >= 0);
		break;
	case TASK_STARTING:
	case TASK_STOPPING:
		/* transitional states */
		assert(tsdfx_scan_find(task) >= 0);
		break;
	case TASK_STOPPED:
	case TASK_DEAD:
		assert(task->pid == -1);
		assert(task->pd == -1);
		break;
	case TASK_INVALID:
		/* unknown */
		break;
	default:
		assert(0);
	}
}

/*
 * Locate a task on the task list
 */
static inline int
tsdfx_scan_find(const struct scan_task *task)
{

	if (task->index < 0 || task->index >= scan_len ||
	    scan_tasks[task->index] != task)
		return (-1);
	return (task->index);
}

/*
 * Return the state of a scan task.
 */
enum task_state
tsdfx_scan_state(const struct scan_task *task)
{

	return (task->state);
}

/*
 * Return the result of the scan.
 */
const char *
tsdfx_scan_result(const struct scan_task *task)
{

	if (task->state != TASK_FINISHED)
		return (NULL);
	return (task->buf);
}

/*
 * Add a task to the task list.
 */
int
tsdfx_scan_add(struct scan_task *task)
{
	struct scan_task **tasks;
	struct pollfd *pipes;
	size_t sz;
	int i;

	VERBOSE("%s", task->path);

	/* is it already on the list? */
	if ((i = tsdfx_scan_find(task)) >= 0) {
		/* refresh pollfd */
		if ((scan_pipes[i].fd = task->pd) >= 0)
			scan_pipes[i].events = POLLIN;
		else
			scan_pipes[i].events = 0;
		return (i);
	}

	/* make room for it */
	while (scan_len >= (int)scan_sz) {
		sz = scan_sz ? scan_sz * 2 : 16;
		if ((tasks = realloc(scan_tasks, sz * sizeof *tasks)) == NULL)
			return (-1);
		memset(tasks + scan_sz, 0, (sz - scan_sz) * sizeof *tasks);
		scan_tasks = tasks;
		if ((pipes = realloc(scan_pipes, sz * sizeof *pipes)) == NULL)
			return (-1);
		memset(pipes + scan_sz, 0, (sz - scan_sz) * sizeof *pipes);
		scan_pipes = pipes;
		scan_sz = sz;
	}

	/* append the task to the list */
	assert(scan_len + 1 <= (int)scan_sz);
	scan_tasks[scan_len] = task;
	if ((scan_pipes[scan_len].fd = task->pd) >= 0)
		scan_pipes[scan_len].events = POLLIN;
	task->index = scan_len++;
	tsdfx_scan_invariant(task);
	return (task->index);
}

/*
 * Temporarily mute a task without removing it from the list.
 */
int
tsdfx_scan_mute(const struct scan_task *task)
{
	int i;

	if ((i = tsdfx_scan_find(task)) < 0)
		return (-1);
	scan_pipes[i].fd = -1;
	scan_pipes[i].events = 0;
	return (0);
}

/*
 * Unmute a task which is already on the task list.
 */
int
tsdfx_scan_unmute(const struct scan_task *task)
{
	int i;

	if ((i = tsdfx_scan_find(task)) < 0 || task->pd < 0)
		return (-1);
	scan_pipes[task->index].fd = task->pd;
	scan_pipes[task->index].events = POLLIN;
	return (0);
}

/*
 * Remove a task from the task list.
 */
int
tsdfx_scan_remove(struct scan_task *task)
{
	int i;

	VERBOSE("%s", task->path);
	if ((i = tsdfx_scan_find(task)) < 0)
		return (-1);
	task->index = -1;
	if (i + 1 < scan_len) {
		memmove(scan_tasks + i, scan_tasks + i + 1,
		    (scan_len - (i + 1)) * sizeof *scan_tasks);
		memmove(scan_pipes + i, scan_pipes + i + 1,
		    (scan_len - (i + 1)) * sizeof *scan_pipes);
	}
	memset(scan_tasks + scan_len, 0, sizeof *scan_tasks);
	memset(scan_pipes + scan_len, 0, sizeof *scan_pipes);
	--scan_len;
	for (; i < scan_len; ++i)
		scan_tasks[i]->index = i;
	return (0);
}

/*
 * Prepare a scan task.
 */
struct scan_task *
tsdfx_scan_new(const char *path)
{
	struct scan_task *task;
	struct stat st;
	int serrno;

	VERBOSE("%s", path);
	if (lstat(path, &st) != 0)
		return (NULL);
	if (!S_ISDIR(st.st_mode)) {
		errno = ENOTDIR;
		return (NULL);
	}
	if ((task = calloc(1, sizeof *task)) == NULL)
		return (NULL);
	task->st = st;
	task->pid = -1;
	task->pd = -1;
	if (realpath(path, task->path) == NULL)
		goto fail;
	task->bufsz = INITIAL_BUFFER_SIZE;
	task->buflen = 0;
	if ((task->buf = malloc(task->bufsz)) == NULL)
		goto fail;
	if (tsdfx_scan_add(task) < 0)
		goto fail;
	task->state = TASK_IDLE;
	task->interval = SCAN_INTERVAL; /* XXX should be tunable */
	return (task);
fail:
	serrno = errno;
	free(task->buf);
	free(task);
	errno = serrno;
	return (NULL);
}

/*
 * Fork a child process and start a scan task inside.
 */
int
tsdfx_scan_start(struct scan_task *task)
{
	int pd[2];

	VERBOSE("%s", task->path);
	tsdfx_scan_invariant(task);

	if (task->state == TASK_RUNNING)
		return (0);
	if (task->state != TASK_IDLE)
		return (-1);
	task->state = TASK_STARTING;

	/* open the pipe and register the task */
	if (pipe(pd) != 0)
		return (-1);
	if (fcntl(pd[0], F_SETFL, (long)O_NONBLOCK) != 0)
		goto fail;
	task->pd = pd[0];
	if (tsdfx_scan_unmute(task) != 0)
		goto fail;

	/* fork the scanner child */
	fflush(NULL);
	if ((task->pid = fork()) < 0)
		goto fail;

	/* child */
	if (task->pid == 0) {
		VERBOSE("child process for %s", task->path);
#if HAVE_SETPROCTITLE
		/* set process title if possible */
		setproctitle("scan %s", task->path);
#endif

		/* replace stdout with pipe and close read end */
		close(pd[0]);
		dup2(pd[1], STDOUT_FILENO);
		close(pd[1]);
#if HAVE_CLOSEFROM
		/* we really really need a linux alternative... */
		closefrom(3);
#endif
		setvbuf(stdout, NULL, _IOLBF, 0);

		/* change into the target directory */
		if (chdir(task->path) != 0) {
			warn("%s", task->path);
			_exit(1);
		}

		/* not root */
		if (geteuid() != 0)
			_exit(!!tsdfx_scanner("."));

		/* chroot */
		if (chroot(task->path) != 0) {
			warn("%s", task->path);
			_exit(1);
		}

		/* drop privileges */
#if 0
		if (task->st.st_uid == 0)
			warnx("scanning %s with uid 0", task->path);
		if (task->st.st_gid == 0)
			warnx("scanning %s with gid 0", task->path);
#endif
		setgid(task->st.st_gid);
		setuid(task->st.st_uid);

		/* run the scan task */
		_exit(!!tsdfx_scanner("."));
	}

	/* parent */
	/* close the write end of the pipe */
	close(pd[1]);
	task->state = TASK_RUNNING;
	return (0);
fail:
	close(pd[1]);
	close(pd[0]);
	task->pd = -1;
	task->state = TASK_DEAD;
	return (-1);
}

/*
 * Stop a scan task.  We make three attempts: first we check to see if the
 * child is already dead.  Then we wait 10 ms and check again; if it still
 * isn't dead, we send a SIGTERM, wait 10 ms, and check again.  If it
 * still isn't dead after the SIGTERM, we send a SIGKILL, wait 10 ms, and
 * try one last time.
 */
int
tsdfx_scan_stop(struct scan_task *task)
{
	int sig[] = { SIGCONT, SIGTERM, SIGKILL, -1 };
	int i;

	VERBOSE("%s", task->path);
	tsdfx_scan_invariant(task);

	/* check current state */
	if (task->state != TASK_RUNNING)
		return (-1);
	task->state = TASK_STOPPING;

	/* close the pipe */
	tsdfx_scan_mute(task);
	close(task->pd);
	task->pd = -1;

	/* reap the child */
	for (i = 0; sig[i] >= 0; ++i) {
		tsdfx_task_poll(task->pid, &task->state);
		if (task->state != TASK_STOPPING)
			break;
		/* not dead yet; kill, wait 10 ms and retry */
		if (sig[i])
			kill(task->pid, sig[i]);
		kill(task->pid, SIGCONT);
		usleep(10000);
	}

	/* either done or gave up */
	if (sig[i] < 0) {
		warnx("gave up waiting for child %d", (int)task->pid);
		task->state = TASK_DEAD;
	}

	/* in summary... */
	task->pid = -1;
	if (task->state != TASK_STOPPED)
		return (-1);
	return (0);
}

/*
 * Reset a scan task.
 */
int
tsdfx_scan_reset(struct scan_task *task)
{
	struct stat st;

	VERBOSE("%s", task->path);

	if (task->state == TASK_IDLE)
		return (0);

	/* mute */
	if (tsdfx_scan_mute(task) != 0)
		return (-1);

	/* stop the task */
	if (task->state == TASK_RUNNING)
		(void)tsdfx_scan_stop(task);

	/* clear the buffer */
	task->buf[0] = '\0';
	task->buflen = 0;

	/* check that it's still there */
	if (lstat(task->path, &st) != 0) {
		warnx("%s has disappeared", task->path);
		task->state = TASK_INVALID;
		return (-1);
	}

	/* re-stat and check for suspicious changes */
	if (!S_ISDIR(st.st_mode)) {
		warnx("%s is no longer a directory", task->path);
		task->state = TASK_INVALID;
		return (-1);
	}
	if (st.st_uid != task->st.st_uid)
		warnx("%s owner changed from %ld to %ld", task->path,
		    (long)st.st_uid, (long)task->st.st_uid);
	if (st.st_gid != task->st.st_gid)
		warnx("%s group changed from %ld to %ld", task->path,
		    (long)st.st_gid, (long)task->st.st_gid);
	task->st = st;
	task->state = TASK_IDLE;

	/* reschedule */
	task->nextrun = time(&task->lastran) + task->interval;

	return (0);
}

/*
 * Delete a scan task.
 */
void
tsdfx_scan_delete(struct scan_task *task)
{

	if (task == NULL)
		return;
	VERBOSE("%s", task->path);
	tsdfx_scan_invariant(task);
	if (task->pid != -1 || task->pd != -1)
		tsdfx_scan_stop(task);
	tsdfx_scan_remove(task);
	free(task->buf);
	free(task);
}

/*
 * Read all available data from a single task, validating it as we go.
 */
int
tsdfx_scan_slurp(struct scan_task *task)
{
	size_t bufsz;
	ssize_t rlen;
	char *buf, *p;
	int len;

	p = task->buf + task->buflen;
	len = 0;
	do {
		/* make sure we have room for at least one more character */
		if (task->buflen + 2 >= (int)task->bufsz) {
			bufsz = task->bufsz * 2;
			if (bufsz > MAXIMUM_BUFFER_SIZE)
				bufsz = MAXIMUM_BUFFER_SIZE;
			if (bufsz <= task->bufsz) {
				errno = ENOSPC;
				return (-1);
			}
			if ((buf = realloc(task->buf, bufsz)) == NULL)
				return (-1);
			task->buf = buf;
			task->bufsz = bufsz;
		}

		/* where do we start, and how much room do we have? */
		buf = task->buf + task->buflen;
		bufsz = task->bufsz - task->buflen - 1;

		/* read and update pointers and counters */
		if ((rlen = read(task->pd, buf, bufsz)) < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				break;
			return (rlen);
		}
		task->buflen += rlen;
		task->buf[task->buflen] = '\0';
		len += rlen;
	} while (rlen > 0);

	/* validate */
	/*
	 * XXX two problems here:
	 *
	 *  - We revalidate the entire buffer every time, which is
         *    inefficient - n * (n - 1) / 2, or O(n²).
	 *
	 *  - We assume that we read one or more complete lines.  This
         *    happens to be the case because a) the child's stdout is
         *    line-buffered and b) a combination of implementation details
         *    in stdio and the kernel which result in each line being
         *    written (and read) atomically, but this assumption may not
         *    hold on other platforms, and it may break if the child
         *    writes a very long line.
	 *
	 * A simple (table-driven?) state machine would solve both issues.
	 */
	if (regexec(&scan_regex, task->buf, 0, NULL, 0) != 0) {
		VERBOSE("invalid output from child %ld for %s",
		    (long)task->pid, task->path);
		goto einval;
	}

	return (len);
einval:
	errno = EINVAL;
	return (-1);
}

/*
 * Start any scheduled tasks
 */
void
tsdfx_scan_sched(void)
{
	time_t now;
	int i;

	time(&now);
	for (i = 0; i < scan_len; ++i)
		if (now >= scan_tasks[i]->nextrun &&
		    scan_tasks[i]->state == TASK_IDLE)
			if (tsdfx_scan_start(scan_tasks[i]) != 0)
				warn("failed to start task");
}

/*
 * The heart of the scan loop: check for available data and slurp it.
 */
int
tsdfx_scan_iter(int timeout)
{
	struct scan_task *task;
	int i, ret;

	/* wait for input */
	if ((ret = poll(scan_pipes, scan_len, timeout)) <= 0)
		return (ret);
	for (i = 0; i < scan_len; ++i) {
		if (scan_pipes[i].revents == 0)
			continue;
		task = scan_tasks[i];
		tsdfx_scan_invariant(task);
		if ((ret = tsdfx_scan_slurp(task)) < 0) {
			if (tsdfx_scan_stop(task) == 0)
				task->state = TASK_FAILED;
		} else if (ret == 0) {
			if (tsdfx_scan_stop(task) == 0)
				task->state = TASK_FINISHED;
		}
	}
	return (0);
}

/*
 * Initialize the scanning subsystem
 */
int
tsdfx_scan_init(void)
{

	if (regcomp(&scan_regex, SCAN_REGEX, REG_EXTENDED|REG_NOSUB) != 0)
		return (-1);
	return (0);
}
