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
#include <sys/wait.h>

#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <pwd.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if HAVE_BSD_UNISTD_H
#include <bsd/unistd.h>
#endif

#include <tsd/hash.h>
#include <tsd/log.h>
#include <tsd/strutil.h>
#include <tsd/task.h>

/*
 * Clear task credentials.
 */
static void
tsd_task_clearcred(struct tsd_task *t)
{

	memset(t->user, 0, sizeof t->user);
	t->uid = (uid_t)-1;
	memset(t->gids, 0, sizeof t->gids);
	t->gids[0] = (gid_t)-1;
	t->ngids = 0;
}

/*
 * Create a new task.
 */
struct tsd_task *
tsd_task_create(const char *name, tsd_task_func *func, void *ud)
{
	struct tsd_task *t;

	if ((t = calloc(1, sizeof *t)) == NULL)
		return (NULL);
	if (strlcpy(t->name, name, sizeof t->name) >= sizeof t->name) {
		memset(t, 0, sizeof *t);
		free(t);
		errno = ENAMETOOLONG;
		return (NULL);
	}
	t->h = tsd_strhash(t->name);
	t->state = TASK_IDLE;
	tsd_task_clearcred(t);
	t->func = func;
	t->pid = -1;
	t->pin = t->pout = t->perr = -1;
	t->ud = ud;
	VERBOSE("%s", name);
	return (t);
}

/*
 * Destroy a task.
 */
void
tsd_task_destroy(struct tsd_task *t)
{
	if (t == NULL)
		return;
	VERBOSE("%s", t->name);
	if (t->state == TASK_RUNNING)
		tsd_task_stop(t);
	if (t->queue != NULL)
		tsd_tqueue_remove(t->queue, t);
	if (t->set != NULL)
		tsd_tset_remove(t->set, t);
	/* ASSERT(t->state != TASK_STOPPING); */
	memset(t, 0, sizeof *t);
	free(t);
}

/*
 * Internal: perform cleanup after a task stops or fails.
 */
static void
tsd_task_close(struct tsd_task *t, enum tsd_task_state nextstate)
{

	t->pid = -1;
	if (t->flags & TASK_STDIN)
		close(t->pin);
	if (t->flags & TASK_STDOUT)
		close(t->pout);
	if (t->flags & TASK_STDERR)
		close(t->perr);
	t->pin = t->pout = t->perr = -1;
	if (t->state == TASK_RUNNING || t->state == TASK_STOPPING) {
		/* currently counted as running */
		if (t->set != NULL)
			t->set->nrunning--;
		if (t->queue != NULL)
			tsd_tqueue_remove(t->queue, t);
	}
	t->state = nextstate;
}

/*
 * Set the task credentials to those of the given user.
 */
int
tsd_task_setuser(struct tsd_task *t, const char *user)
{
	struct passwd *pwd;

	if (t->state != TASK_IDLE) {
		errno = EBUSY;
		return (-1);
	}
	errno = 0;
	if ((pwd = getpwnam(user)) == NULL) {
		if (errno == 0)
			errno = ENOENT;
		goto fail;
	}
	if (strlcpy(t->user, pwd->pw_name, sizeof t->user) >= sizeof t->user) {
		errno = ENAMETOOLONG;
		goto fail;
	}
	t->uid = pwd->pw_uid;
	t->ngids = sizeof t->gids / sizeof t->gids[0];
	if (getgrouplist(pwd->pw_name, pwd->pw_gid, t->gids, &t->ngids) < 0) {
		/* XXX does getgrouplist() set errno? */
		goto fail;
	}
	return (0);
fail:
	tsd_task_clearcred(t);
	return (-1);
}

/*
 * Set the task credentials to the given UID and GID.
 */
int
tsd_task_setcred(struct tsd_task *t, uid_t uid, gid_t *gids, int ngids)
{

	if (t->state != TASK_IDLE) {
		errno = EBUSY;
		return (-1);
	}
	tsd_task_clearcred(t);
	if (ngids < 1 || ngids > (int)(sizeof t->gids / sizeof t->gids[0]))
		return (-1);
	snprintf(t->user, sizeof t->user, "(%lu:%lu)",
	    (unsigned long)uid, (unsigned long)gids[0]);
	t->uid = uid;
	memcpy(t->gids, gids, ngids * sizeof *gids);
	t->ngids = ngids;
	return (0);
}

/*
 * Fork a child process and start a task inside it.
 *
 * XXX consider adding support for chroot
 */
int
tsd_task_start(struct tsd_task *t)
{
	int pin[2] = { -1, -1 };
	int pout[2] = { -1, -1 };
	int perr[2] = { -1, -1 };
	int ret, serrno;
#if !HAVE_CLOSEFROM
	int fd, maxfd;
#endif

	VERBOSE("%s", t->name);

	if (t->state == TASK_RUNNING)
		return (0);
	if (t->state != TASK_IDLE)
		return (-1);
	t->state = TASK_STARTING;

	/* prepare file descriptors */
	if (t->flags & TASK_STDIN_NULL) {
		if ((pin[0] = open("/dev/null", O_RDONLY)) < 0)
			goto fail;
	} else if (t->flags & TASK_STDIN_PIPE) {
		if (pipe(pin) != 0 || fcntl(pin[1], F_SETFL, O_NONBLOCK) != 0)
			goto fail;
		t->pin = pin[1];
	}
	if (t->flags & TASK_STDOUT_NULL) {
		if ((pout[1] = open("/dev/null", O_WRONLY)) < 0)
			goto fail;
	} else if (t->flags & TASK_STDOUT_PIPE) {
		if (pipe(pout) != 0 || fcntl(pout[0], F_SETFL, O_NONBLOCK) != 0)
			goto fail;
		t->pout = pout[0];
	}
	if (t->flags & TASK_STDERR_NULL) {
		if ((perr[1] = open("/dev/null", O_WRONLY)) < 0)
			goto fail;
	} else if (t->flags & TASK_STDERR_PIPE) {
		if (pipe(perr) != 0 || fcntl(perr[0], F_SETFL, O_NONBLOCK) != 0)
			goto fail;
		t->perr = perr[0];
	}

	/* fork the child */
	fflush(NULL);
	if ((t->pid = fork()) < 0)
		goto fail;

	/* child */
	if (t->pid == 0) {
		/* set up stdin/out/err and close everything else */
#if HAVE_FPURGE
		fpurge(stdin);
#endif
		if ((t->flags & TASK_STDIN && dup2(pin[0], 0) != 0) ||
		    (t->flags & TASK_STDOUT && dup2(pout[1], 1) != 1) ||
		    (t->flags & TASK_STDERR && dup2(perr[1], 2) != 2)) {
			ERROR("failed to set up standard file descriptors");
			_exit(1);
		}
#if HAVE_CLOSEFROM
		closefrom(3);
#else
		maxfd = getdtablesize();
		for (fd = 3; fd < maxfd; ++fd)
			close(fd);
#endif

		/* set process title if possible */
#if HAVE_SETPROCTITLE
		setproctitle("%s", t->name);
#endif

		/* drop privileges */
		if (geteuid() == 0 && t->gids[0] > 0 && t->uid != (uid_t)-1) {
			if ((ret = setgid(t->gids[0])) != 0)
				ERROR("failed to set process group");
#if HAVE_SETGROUPS
			else if ((ret = setgroups(t->ngids, t->gids)) != 0)
				ERROR("failed to set additional process groups");
#endif
			else if ((ret = setuid(t->uid)) != 0)
				ERROR("failed to set process user");
			if (ret != 0)
				_exit(1);
		}
		if (getgid() != getegid())
			(void)setgid(getgid());
		if (getuid() != geteuid())
			(void)setuid(getuid());
		(*t->func)(t->ud);
		_exit(1);
	}

	/* parent */
	if (t->flags & TASK_STDIN)
		close(pin[0]);
	if (t->flags & TASK_STDOUT)
		close(pout[1]);
	if (t->flags & TASK_STDERR)
		close(perr[1]);
	t->state = TASK_RUNNING;
	if (t->set != NULL)
		t->set->nrunning++;
	if (t->queue != NULL)
		t->queue->nrunning++;
	return (0);
fail:
	serrno = errno;
	/* close the child side, tsd_task_close() will close the other */
	if (t->flags & TASK_STDIN)
		close(pin[1]);
	if (t->flags & TASK_STDOUT)
		close(pout[0]);
	if (t->flags & TASK_STDERR)
		close(perr[0]);
	tsd_task_close(t, TASK_DEAD);
	errno = serrno;
	return (-1);
}

/*
 * Stop a task.  We make three attempts: first we check to see if the
 * child is already dead.  Then we wait 10 ms and check again; if it still
 * isn't dead, we send a SIGTERM, wait 10 ms, and check again.  If it
 * still isn't dead after the SIGTERM, we send a SIGKILL, wait 10 ms, and
 * try one last time.
 */
int
tsd_task_stop(struct tsd_task *t)
{
	static const int sig[] = { SIGCONT, SIGTERM, SIGKILL, 0, -1 };
	int i, serrno;

	VERBOSE("%s", t->name);

	/* check current state */
	if (t->state != TASK_RUNNING)
		return (-1);
	t->state = TASK_STOPPING;

	/* reap the child */
	for (i = 0; sig[i] >= 0; ++i) {
		tsd_task_poll(t);
		if (t->state != TASK_STOPPING)
			break;
		/* not dead yet; kill, wait 100 ms and retry */
		if (kill(t->pid, sig[i]) != 0) {
			serrno = errno;
			WARNING("unable to signal child %d", (int)t->pid);
			tsd_task_close(t, TASK_DEAD);
			errno = serrno;
			return (-1);
		}
		usleep(100000);
	}

	/* either done or gave up */
	if (sig[i] < 0) {
		WARNING("gave up waiting for child %d", (int)t->pid);
		tsd_task_close(t, TASK_DEAD);
		/* XXX set errno? */
		return (-1);
	}

	/* in summary... */
	if (t->state != TASK_STOPPED)
		return (-1);
	return (0);
}

/*
 * Send a signal to a task.
 */
int
tsd_task_signal(const struct tsd_task *t, int sig)
{

	VERBOSE("%d", sig);

	if (t->state != TASK_RUNNING) {
		errno = ESRCH;
		return (-1);
	}
	return (kill(t->pid, sig));
}

/*
 * Reset a task so it can be started again.
 * Note: it is the caller's responsibility to add it to a queue.
 */
int
tsd_task_reset(struct tsd_task *t)
{

	VERBOSE("%s", t->name);

	if (t->state == TASK_IDLE)
		return (0);
	if (t->state == TASK_RUNNING)
		tsd_task_stop(t);
	t->status = 0;
	t->state = TASK_IDLE;
	return (0);
}

/*
 * Poll a task to see if it's still running.
 */
int
tsd_task_poll(struct tsd_task *t)
{
	enum tsd_task_state nextstate;
	int ret, serrno;

	VERBOSE("%s", t->name);

	if (t->state != TASK_RUNNING && t->state != TASK_STOPPING)
		return (-1);
	nextstate = t->state;
	ret = waitpid(t->pid, &t->status, WNOHANG);
	if (ret < 0) {
		serrno = errno;
		/* already reaped, or something is wrong */
		WARNING("waitpid(%lu): %s",
		    (unsigned long)t->pid, strerror(errno));
		nextstate = TASK_DEAD;
		errno = serrno;
		/* fall through */
	} else if (ret == 0) {
		/* still running */
		return (0);
	} else if (ret == t->pid) {
		if (WIFEXITED(t->status) && WEXITSTATUS(t->status) == 0) {
			VERBOSE("%s [%lu] succeeded", t->name,
			    (unsigned long)t->pid);
			nextstate = TASK_STOPPED;
		} else if (WIFEXITED(t->status)) {
			NOTICE("%s [%lu] failed with exit code %d", t->name,
			    (unsigned long)t->pid, WEXITSTATUS(t->status));
			nextstate = TASK_FAILED;
		} else if (WIFSIGNALED(t->status)) {
			WARNING("%s [%lu] caught signal %d", t->name,
			    (unsigned long)t->pid, WTERMSIG(t->status));
			nextstate = TASK_DEAD;
		} else {
			ERROR("%s [%lu] terminated with unknown status %d",
			    t->name, (unsigned long)t->pid, t->status);
			nextstate = TASK_DEAD;
		}
		/* fall through */
	} else {
		/* wtf? */
		ERROR("waitpid(%lu) returned %d",
		    (unsigned long)t->pid, ret);
		errno = EAGAIN;
		return (-1);
	}
	/* fell through from above: task has stopped */
	serrno = errno;
	tsd_task_close(t, nextstate);
	errno = serrno;
	if (t->state != TASK_STOPPED)
		return (-1);
	return (0);
}
