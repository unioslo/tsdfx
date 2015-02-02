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

#include <errno.h>
#include <string.h>

#include <tsd/log.h>
#include <tsd/task.h>

/*
 * Create a new task queue
 */
struct tsd_taskq *
tsd_taskq_create(void)
{

	return (NULL);
}

/*
 * Destroy a task queue
 */
int
tsd_taskq_destroy(struct tsd_taskq *tq)
{

	(void)tq;
	return (-1);
}

/*
 * Add a task to a queue
 */
int
tsd_taskq_insert(struct tsd_taskq *tq, struct tsd_task *t)
{

	(void)tq;
	(void)t;
	return (-1);
}

/*
 * Remove a task from its queue
 */
int
tsd_taskq_remove(struct tsd_taskq *tq, struct tsd_task *t)
{

	(void)tq;
	(void)t;
	return (-1);
}

/*
 * Get the next runnable task from a queue
 */
struct tsd_task *
tsd_taskq_next(struct tsd_taskq *tq)
{

	(void)tq;
	return (NULL);
}