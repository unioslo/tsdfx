/*-
 * Copyright (c) 2015 Universitetet i Oslo
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

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <tsd/hash.h>
#include <tsd/log.h>
#include <tsd/strutil.h>
#include <tsd/task.h>

/*
 * Create a new task set.
 */
struct tsd_tset *
tsd_tset_create(const char *name)
{
	struct tsd_tset *ts;

	if ((ts = calloc(1, sizeof *ts)) == NULL)
		return (NULL);
	if (strlcpy(ts->name, name, sizeof ts->name) >= sizeof ts->name) {
		free(ts);
		errno = ENAMETOOLONG;
		return (NULL);
	}
	return (ts);
}

/*
 * Destroy a task set.  It is the caller's responsibility to stop and
 * destroy all tasks before destroying the set.
 */
void
tsd_tset_destroy(struct tsd_tset *ts)
{
	struct tsd_task *t;
	unsigned int h;

	for (h = 0; h < sizeof ts->tasks / sizeof *ts->tasks; ++h) {
		while (ts->tasks[h] != NULL) {
			t = ts->tasks[h];
			ts->tasks[h] = t->snext;
			t->set = NULL;
			t->snext = NULL;
		}
	}
	memset(ts, 0, sizeof *ts);
	free(ts);
}

/*
 * Add a task to a task set
 */
int
tsd_tset_insert(struct tsd_tset *ts, struct tsd_task *t)
{
	struct tsd_task **tpp;

	if (t->set != NULL) {
		errno = EBUSY;
		return (-1);
	}
	assert(t->h < sizeof ts->tasks / sizeof *ts->tasks);
	assert(t->snext == NULL);
	assert(t->set == NULL);
	for (tpp = &ts->tasks[t->h]; *tpp != NULL; tpp = &(*tpp)->snext) {
		assert((*tpp)->h ==  t->h);
		if (strcmp((*tpp)->name, t->name) == 0) {
			errno = EEXIST;
			return (-1);
		}
	}
	*tpp = t;
	t->set = ts;
	ts->ntasks++;
	if (t->state == TASK_RUNNING)
		ts->nrunning++;
	return (0);
}

/*
 * Remove a task from its task set
 */
int
tsd_tset_remove(struct tsd_tset *ts, struct tsd_task *t)
{
	struct tsd_task **tpp;

	if (t->set != ts) {
		errno = ENOENT;
		return (-1);
	}
	assert(t->h < sizeof ts->tasks / sizeof *ts->tasks);
	assert(t->set == ts);
	for (tpp = &ts->tasks[t->h]; *tpp != NULL; tpp = &(*tpp)->snext) {
		assert((*tpp)->h ==  t->h);
		if (*tpp == t) {
			*tpp = t->snext;
			t->snext = NULL;
			t->set = NULL;
			ts->ntasks--;
			if (t->state == TASK_RUNNING)
				ts->nrunning--;
			return (0);
		}
	}
	errno = ENOENT;
	return (-1);
}

/*
 * Find a task in a task set
 */
struct tsd_task *
tsd_tset_find(const struct tsd_tset *ts, const char *name)
{
	struct tsd_task *tp;
	unsigned int h;

	h = tsd_strhash(name);
	assert(h < sizeof ts->tasks / sizeof *ts->tasks);
	for (tp = ts->tasks[h]; tp != NULL; tp = tp->snext) {
		assert(tp->h ==  h);
		if (strcmp(tp->name, name) == 0)
			return (tp);
	}
	errno = ENOENT;
	return (NULL);
}

/*
 * Iterate over a set: first task
 */
struct tsd_task *
tsd_tset_first(const struct tsd_tset *ts)
{
	unsigned int h;

	for (h = 0; h < sizeof ts->tasks / sizeof *ts->tasks; ++h) {
		if (ts->tasks[h] != NULL) {
			assert(ts->tasks[h]->h == h);
			return (ts->tasks[h]);
		}
	}
	return (NULL);
}

/*
 * Iterate over a set: next task
 */
struct tsd_task *
tsd_tset_next(const struct tsd_tset *ts, const struct tsd_task *t)
{
	unsigned int h;

	if (t == NULL)
		return (tsd_tset_first(ts));
	assert(t->h < sizeof ts->tasks / sizeof *ts->tasks);
	assert(t->set == ts);
	if (t->snext != NULL) {
		assert(t->snext->set == ts);
		return (t->snext);
	}
	for (h = t->h + 1; h < sizeof ts->tasks / sizeof *ts->tasks; ++h) {
		if (ts->tasks[h] != NULL) {
			assert(ts->tasks[h]->h == h);
			assert(ts->tasks[h]->set == ts);
			return (ts->tasks[h]);
		}
	}
	return (NULL);
}

/*
 * Send a signal to all running processes in the set
 */
int
tsd_tset_signal(const struct tsd_tset *ts, int sig)
{
	struct tsd_task *t;
	int ret = 0;

	for (t = tsd_tset_first(ts); t != NULL; t = tsd_tset_next(ts, t))
		if (tsd_task_signal(t, sig) != 0)
			ret = -1;
	return (ret);
}
