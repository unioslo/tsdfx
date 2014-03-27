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
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <tsdfx/ctype.h>
#include <tsdfx/strutil.h>

#include <tsdfx.h>

#define INITIAL_BUFFER_SIZE	(PATH_MAX * 2)
#define MAXIMUM_BUFFER_SIZE	(PATH_MAX * 1024)

enum scan_task_state {
	SCAN_TASK_INVALID,	/* really screwed */
	SCAN_TASK_IDLE,		/* ready to run */
	SCAN_TASK_STARTING,	/* starting */
	SCAN_TASK_RUNNING,	/* running */
	SCAN_TASK_STOPPING,	/* stopping */
	SCAN_TASK_STOPPED,	/* stopped */
	SCAN_TASK_DEAD,		/* dead */
	SCAN_TASK_FINISHED,	/* stopped and successful */
	SCAN_TASK_FAILED,	/* stopped and something went wrong */
};

struct scan_task {
	/* what to scan */
	char path[PATH_MAX];
	struct stat st;

	/* place in the task list */
	int index;

	/* state */
	enum scan_task_state state;

	/* child process */
	pid_t pid;
	int pd;

	/* scanned files */
	char *buf;
	size_t buflen, bufsz;
};

static struct scan_task **scan_tasks;
static struct pollfd *scan_pipes;
static size_t scan_sz;
static int scan_len;

static inline void tsdfx_scan_invariant(const struct scan_task *);
static inline int tsdfx_scan_find(const struct scan_task *);
static int tsdfx_scan_add(struct scan_task *);
static int tsdfx_scan_mute(const struct scan_task *);
static int tsdfx_scan_unmute(const struct scan_task *);
static int tsdfx_scan_remove(struct scan_task *);
static struct scan_task *tsdfx_scan_new(const char *);
static int tsdfx_scan_start(struct scan_task *);
static int tsdfx_scan_stop(struct scan_task *);
static int tsdfx_scan_reset(struct scan_task *);
static void tsdfx_scan_delete(struct scan_task *);
static int tsdfx_scan_slurp(struct scan_task *);
static int tsdfx_scan_iter(int);

/*
 * Debugging aid
 */
static inline void
tsdfx_scan_invariant(const struct scan_task *task)
{

	switch (task->state) {
	case SCAN_TASK_IDLE:
		assert(task->buflen == 0);
	case SCAN_TASK_FAILED:
	case SCAN_TASK_FINISHED:
		assert(task->index < 0 || tsdfx_scan_find(task) >= 0);
		assert(task->pid == -1);
		assert(task->pd == -1);
		break;
	case SCAN_TASK_RUNNING:
		assert(tsdfx_scan_find(task) >= 0);
		assert(task->pid > 0);
		assert(task->pd >= 0);
		break;
	case SCAN_TASK_STARTING:
	case SCAN_TASK_STOPPING:
		/* transitional states */
		assert(tsdfx_scan_find(task) >= 0);
		break;
	case SCAN_TASK_STOPPED:
	case SCAN_TASK_DEAD:
		assert(task->pid == -1);
		assert(task->pd == -1);
		break;
	case SCAN_TASK_INVALID:
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
 * Add a task to the task list.
 */
static int
tsdfx_scan_add(struct scan_task *task)
{
	struct scan_task **tasks;
	struct pollfd *pipes;
	size_t sz;
	int i;

	/* is it already on the list? */
	if ((i = tsdfx_scan_find(task)) >= 0) {
		/* refresh pollfd */
		if ((scan_pipes[i].fd = task->pd) >= 0)
			scan_pipes[i].events = POLLIN;
		else
			scan_pipes[i].events = 0;
		return (0);
	}

	/* make room for it */
	if ((size_t)scan_len >= scan_sz) {
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
	// assert(scan_len + 1 <= scan_sz)
	scan_tasks[scan_len] = task;
	if ((scan_pipes[scan_len].fd = task->pd) >= 0)
		scan_pipes[scan_len].events = POLLIN;
	task->index = scan_len++;
	return (0);
}

/*
 * Temporarily mute a task without removing it from the list.
 */
static int
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
static int
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
static int
tsdfx_scan_remove(struct scan_task *task)
{
	int i;

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
	return (0);
}

/*
 * Prepare a scan task.
 */
static struct scan_task *
tsdfx_scan_new(const char *path)
{
	struct scan_task *task;
	int serrno;

	if ((task = calloc(1, sizeof *task)) == NULL)
		return (NULL);
	task->pid = -1;
	task->pd = -1;
	if (realpath(path, task->path) == NULL)
		goto fail;
	if (lstat(path, &task->st) != 0)
		goto fail;
	if (!S_ISDIR(task->st.st_mode)) {
		errno = ENOTDIR;
		goto fail;
	}
	task->bufsz = INITIAL_BUFFER_SIZE;
	task->buflen = 0;
	if ((task->buf = malloc(task->bufsz)) == NULL)
		goto fail;
	if (tsdfx_scan_add(task) != 0)
		goto fail;
	task->state = SCAN_TASK_IDLE;
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
static int
tsdfx_scan_start(struct scan_task *task)
{
	int pd[2];

	if (task->state == SCAN_TASK_RUNNING)
		return (0);
	if (task->state != SCAN_TASK_IDLE)
		return (-1);
	task->state = SCAN_TASK_STARTING;

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
		/* replace stdout with pipe and close read end */
		close(pd[0]);
		dup2(pd[1], STDOUT_FILENO);
		close(pd[1]);
#if HAVE_CLOSEFROM
		/* we really really need a linux alternative... */
		closefrom(3);
#endif
		setvbuf(stdout, NULL, _IOLBF, 0);

		/* not root */
		if (geteuid() != 0)
			_exit(tsdfx_scanner(task->path));

		/* change into the target directory */
		if (chdir(task->path) != 0 || chroot(task->path) != 0) {
			warn("%s", task->path);
			_exit(1);
		}

		/* drop privileges */
		if (task->st.st_uid == 0)
			warnx("scanning %s with uid 0", task->path);
		if (task->st.st_gid == 0)
			warnx("scanning %s with gid 0", task->path);
		setgid(task->st.st_gid);
		setuid(task->st.st_uid);

		/* run the scan task */
		_exit(tsdfx_scanner("/"));
	}

	/* parent */
	/* close the write end of the pipe */
	close(pd[1]);
	task->state = SCAN_TASK_RUNNING;
	return (0);
fail:
	close(pd[1]);
	close(pd[0]);
	task->pd = -1;
	task->state = SCAN_TASK_DEAD;
	return (-1);
}

/*
 * Stop a scan task.  We make three attempts: first we check to see if the
 * child is already dead.  Then we wait 10 ms and check again; if it still
 * isn't dead, we send a SIGTERM, wait 10 ms, and check again.  If it
 * still isn't dead after the SIGTERM, we send a SIGKILL, wait 10 ms, and
 * try one last time.
 */
static int
tsdfx_scan_stop(struct scan_task *task)
{
	int i, ret, status;
	int sig[] = { SIGCONT, SIGTERM, SIGKILL, -1 };

	/* check current state */
	if (task->state != SCAN_TASK_RUNNING)
		return (-1);
	task->state = SCAN_TASK_STOPPING;

	/* close the pipe */
	tsdfx_scan_mute(task);
	close(task->pd);
	task->pd = -1;

	/* reap the child */
	for (i = 0; sig[i] >= 0; ++i) {
		ret = waitpid(task->pid, &status, WNOHANG);
		if (ret < 0) {
			/* already reaped? */
			warn("waitpid(%d)", (int)task->pid);
			task->state = SCAN_TASK_DEAD;
			break;
		} else if (ret == task->pid) {
			/* good */
			if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
				task->state = SCAN_TASK_FAILED;
			break;
		} else if (ret == 0) {
			/* not yet; kill, wait 10 ms and retry */
			if (sig[i])
				kill(task->pid, sig[i]);
			kill(task->pid, SIGCONT);
			usleep(10000);
		} else {
			/* wtf? */
			warnx("waitpid(%d) returned %d", (int)task->pid, ret);
		}
	}

	/* either done or gave up */
	if (sig[i] < 0) {
		warnx("gave up waiting for child %d", (int)task->pid);
		task->state = SCAN_TASK_DEAD;
	}

	/* in summary... */
	task->pid = -1;
	if (task->state != SCAN_TASK_STOPPING)
		return (-1);
	task->state = SCAN_TASK_STOPPED;
	return (0);
}

/*
 * Reset a scan task.
 */
static int
tsdfx_scan_reset(struct scan_task *task)
{
	struct stat st;

	if (task->state == SCAN_TASK_IDLE)
		return (0);

	/* mute */
	if (tsdfx_scan_mute(task) != 0)
		return (-1);

	/* stop the task */
	if (task->state == SCAN_TASK_RUNNING)
		(void)tsdfx_scan_stop(task);

	/* clear the buffer */
	task->buf[0] = '\0';
	task->buflen = 0;

	/* check that it's still there */
	if (lstat(task->path, &st) != 0) {
		warnx("%s has disappeared", task->path);
		task->state = SCAN_TASK_INVALID;
		return (-1);
	}

	/* re-stat and check for suspicious changes */
	if (!S_ISDIR(st.st_mode)) {
		warnx("%s is no longer a directory", task->path);
		task->state = SCAN_TASK_INVALID;
		return (-1);
	}
	if (st.st_uid != task->st.st_uid)
		warnx("%s owner changed from %ld to %ld", task->path,
		    (long)st.st_uid, (long)task->st.st_uid);
	if (st.st_gid != task->st.st_gid)
		warnx("%s group changed from %ld to %ld", task->path,
		    (long)st.st_gid, (long)task->st.st_gid);
	task->st = st;
	task->state = SCAN_TASK_IDLE;
	return (0);
}

/*
 * Delete a scan task.
 */
static void
tsdfx_scan_delete(struct scan_task *task)
{

	if (task == NULL)
		return;
	if (task->pid != -1 || task->pd != -1)
		tsdfx_scan_stop(task);
	tsdfx_scan_remove(task);
	free(task->buf);
	free(task);
}

/*
 * Read all available data from a single task, validating it as we go.
 */
static int
tsdfx_scan_slurp(struct scan_task *task)
{
	size_t bufsz;
	ssize_t rlen;
	char *buf, *p;
	int bol, len;

	p = task->buf + task->buflen;
	len = 0;
	do {
		/* make sure we have room for at least one more character */
		if (task->buflen + 2 >= task->bufsz) {
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
	bol = (p == task->buf || p[-1] == '\n');
	for (; p < task->buf + task->buflen; ++p) {
		if (*p == '\n') {
			/* no empty lines, no trailing slashes */
			if (bol || p[-1] == '/')
				goto einval;
			bol = 1;
		} else if (*p == '/') {
			/* no double slashes */
			if (!bol && p[-1] == '/')
				goto einval;
			bol = 0;
		} else {
			/* always leading slash, only PFCS characters */
			if (bol || !is_pfcs(*p))
				goto einval;
		}
	}
	/* end of transmission, check for LF */
	if (len == 0 && !bol)
		goto einval;
	return (len);
einval:
	errno = EINVAL;
	return (-1);
}

/*
 * The heart of the scan loop: check for available data and slurp it.
 */
int
tsdfx_scan_iter(int timeout)
{
	struct scan_task *task;
	int i, ret;

	if ((ret = poll(scan_pipes, scan_len, timeout)) <= 0)
		return (ret);
	for (i = 0; i < scan_len; ++i) {
		if (scan_pipes[i].revents == 0)
			continue;
		task = scan_tasks[i];
		tsdfx_scan_invariant(task);
		if ((ret = tsdfx_scan_slurp(task)) < 0) {
			if (tsdfx_scan_stop(task) == 0)
				task->state = SCAN_TASK_FAILED;
		} else if (ret == 0) {
			if (tsdfx_scan_stop(task) == 0)
				task->state = SCAN_TASK_FINISHED;
		}
	}
	return (0);
}

int
tsdfx_scan(const char *src, const char *dst)
{
	struct scan_task *src_task, *dst_task;
	int ret;

	if ((src_task = tsdfx_scan_new(src)) == NULL)
		err(1, "failed to create src task");
	if (tsdfx_scan_start(src_task) != 0)
		err(1, "failed to start src task");
	if ((dst_task = tsdfx_scan_new(dst)) == NULL)
		err(1, "failed to create dst task");
	if (tsdfx_scan_start(dst_task) != 0)
		err(1, "failed to start dst task");
	printf("scanning...\n");
	while (src_task->state == SCAN_TASK_RUNNING ||
	    dst_task->state == SCAN_TASK_RUNNING) {
		if ((ret = tsdfx_scan_iter(1000)) != 0)
			break;
	}
	if (src_task->state == SCAN_TASK_FINISHED)
		printf("[%s]\n%s", src_task->path, src_task->buf);
	else
		printf("[%s] failed\n", src_task->path);
	if (dst_task->state == SCAN_TASK_FINISHED)
		printf("[%s]\n%s", dst_task->path, dst_task->buf);
	else
		printf("[%s] failed\n", dst_task->path);
	tsdfx_scan_reset(src_task);
	tsdfx_scan_reset(dst_task);
	tsdfx_scan_delete(src_task);
	tsdfx_scan_delete(dst_task);
	return (0);
}
