/*-
 * Copyright (c) 2014-2015 Universitetet i Oslo
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

#ifndef TSD_TASK_H_INCLUDED
#define TSD_TASK_H_INCLUDED

enum tsd_task_state {
	TASK_INVALID	 = -1,	/* really screwed */
	TASK_IDLE	 =  0,	/* idle */
	TASK_QUEUED,		/* queued */
	TASK_STARTING,		/* starting */
	TASK_RUNNING,		/* running */
	TASK_STOPPING,		/* stopping */
	TASK_STOPPED,		/* stopped */
	TASK_DEAD,		/* dead */
	TASK_FINISHED,		/* stopped and successful */
	TASK_FAILED,		/* stopped and something went wrong */
};

typedef void (tsd_task_func)(void *);

struct tsd_task {
	/* unique name */
	char			 name[64];
	unsigned int		 h;

	/* state */
	enum tsd_task_state	 state;

	/* credentials */
	char			 user[32];
	uid_t			 uid;
	gid_t			 gids[32];
	int			 ngids;

	/* child process */
	tsd_task_func		*func;
	pid_t			 pid;
	int			 status;
	int			 pin;
	int			 pout;
	int			 perr;

	/* task set and queue */
	struct tsd_tset		*set;
	struct tsd_task		*snext;
	struct tsd_tqueue	*queue;
	struct tsd_tqueue	*qnext;

	/* user data */
	void			*ud;
};

struct tsd_tset {
	char			 name[64];
	struct tsd_task		*tasks[256];
	unsigned int		 ntasks;
	unsigned int		 nrunning;
};

struct tsd_tqueue {
};

struct tsd_task *tsd_task_create(const char *, tsd_task_func *, void *);
int tsd_task_setuser(struct tsd_task *, const char *);
int tsd_task_setcred(struct tsd_task *, uid_t, gid_t *, int);
void tsd_task_destroy(struct tsd_task *);
int tsd_task_start(struct tsd_task *);
int tsd_task_stop(struct tsd_task *);
int tsd_task_poll(struct tsd_task *);

struct tsd_tset *tsd_tset_create(const char *);
void tsd_tset_destroy(struct tsd_tset *);
int tsd_tset_insert(struct tsd_tset *, struct tsd_task *);
int tsd_tset_remove(struct tsd_tset *, struct tsd_task *);
struct tsd_task *tsd_tset_find(const struct tsd_tset *, const char *);
struct tsd_task *tsd_tset_first(const struct tsd_tset *);
struct tsd_task *tsd_tset_next(const struct tsd_tset *, const struct tsd_task *);

#endif
