/*-
 * Copyright (c) 2015 The University of Oslo
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
#include <stdlib.h>
#include <string.h>

#include <tsd/assert.h>
#include <tsd/log.h>
#include <tsd/strutil.h>
#include <tsd/task.h>

/*
 * Create a new task queue
 */
struct tsd_tqueue *
tsd_tqueue_create(const char *name, unsigned int max)
{
	struct tsd_tqueue *tq;

	if ((tq = calloc(1, sizeof *tq)) == NULL)
		return (NULL);
	if (strlcpy(tq->name, name, sizeof tq->name) >= sizeof tq->name) {
		free(tq);
		errno = ENAMETOOLONG;
		return (NULL);
	}
	tq->max_running = max;
	return (tq);
}

/*
 * Destroy a task queue
 */
void
tsd_tqueue_destroy(struct tsd_tqueue *tq)
{

	tsd_tqueue_drain(tq);
	memset(tq, 0, sizeof *tq);
	free(tq);
}

/*
 * Add a task to a queue
 */
int
tsd_tqueue_insert(struct tsd_tqueue *tq, struct tsd_task *t)
{

	if (t->queue != NULL) {
		errno = EBUSY;
		return (-1);
	}
	ASSERT(t->qprev == NULL && t->qnext == NULL);
	if (tq->first == NULL) {
		ASSERT(tq->last == NULL);
		tq->first = tq->last = t;
	} else {
		ASSERT(tq->last != NULL);
		ASSERT(tq->last->qnext == NULL);
		t->qprev = tq->last;
		tq->last->qnext = t;
		tq->last = t;
	}
	if (t->state == TASK_RUNNING || t->state == TASK_STOPPING) {
		/* why would you do that? */
		tq->nrunning++;
	}
	tq->ntasks++;
	t->queue = tq;
	return (0);
}

/*
 * Remove a task from its queue
 */
int
tsd_tqueue_remove(struct tsd_tqueue *tq, struct tsd_task *t)
{

	if (t->queue != tq) {
		errno = ENOENT;
		return (-1);
	}
	if (t->qprev != NULL)
		t->qprev->qnext = t->qnext;
	if (t->qnext != NULL)
		t->qnext->qprev = t->qprev;
	if (tq->first == t)
		tq->first = t->qnext;
	if (tq->last == t)
		tq->last = t->qprev;
	if (t->state == TASK_RUNNING || t->state == TASK_STOPPING)
		tq->nrunning--;
	tq->ntasks--;
	t->queue = NULL;
	return (-1);
}

/*
 * Start any runnable tasks if we have free slots.
 */
unsigned int
tsd_tqueue_sched(struct tsd_tqueue *tq)
{
	struct tsd_task *t;

	/*
	 * Scan the queue looking for runnable tasks and try to start them
	 * if we are not already over the limit.  No housekeeping is done
	 * here since tsd_task_start() will either increment our nrunning
	 * or remove the task from the queue if it failed to start.
	 */
	for (t = tq->first; t != NULL; t = t->qnext) {
		if (tq->nrunning >= tq->max_running)
			break;
		if (t->state == TASK_IDLE)
			tsd_task_start(t);
	}
	return (tq->nrunning);
}

/*
 * Stop and remove all tasks from queue
 */
void
tsd_tqueue_drain(struct tsd_tqueue *tq)
{
	struct tsd_task *t;

	while ((t = tq->first) != NULL) {
		ASSERT(t->queue == tq);
		tsd_task_stop(t);
		tq->first = t->qnext;
		t->qprev = t->qnext = NULL;
		t->queue = NULL;
	}
}
