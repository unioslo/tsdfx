/*-
 * Copyright (c) 2016 The University of Oslo
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include <tsd/assert.h>

struct tsdfx_logentry {
	time_t timestamp;
	char *msg;
	struct tsdfx_logentry *next;  
};

struct tsdfx_recentlog {
	char *logfile;
	time_t duration; /* in seconds */
	struct tsdfx_logentry *first, *last;
};

int
tsdfx_recentlog_init(void)
{

	return (0);
}

int
tsdfx_recentlog_exit(void)
{

	return (0);
}

struct tsdfx_recentlog *
tsdfx_recentlog_new(const char *logfile, time_t duration)
{
	struct tsdfx_recentlog *r;

	r = calloc(1, sizeof *r);
	r->logfile = strdup(logfile);
	r->duration = duration;
	return (r);
}

static void
tsdfx_logentry_destroy(struct tsdfx_logentry *cur)
{

	cur->next = NULL;
	free(cur->msg);
	cur->msg = NULL;
	free(cur);
}

int
tsdfx_recentlog_destroy(struct tsdfx_recentlog *r)
{
	struct tsdfx_logentry *cur, *next;

	cur = r->first;
	while (cur) {
		next = cur->next;
		tsdfx_logentry_destroy(cur);
		cur = next;
	}
	r->first = r->last = NULL;
	free(r->logfile);
	r->logfile = NULL;
	free(r);
	return (0);
}

/*
 * Add log to log message queue, and remove log entries in queue older
 * than duration.  Update the log file with the remaining queue entries.
 */
void
tsdfx_recentlog_log(struct tsdfx_recentlog *r, const char *msg)
{
	struct tsdfx_logentry *cur, *next;
	FILE *fh;
	time_t now;

	/* First create and insert logentry */
	now = time(NULL);
	cur = calloc(1, sizeof *cur);
	cur->timestamp = now;
	cur->msg = strdup(msg);

	if (r->first == NULL) {
		/* First element in list */
		r->first = r->last = cur;
	} else {
		r->last->next = cur;
		r->last = cur;
	}

	/* Next print log queue, while removing obsolete entries */
	VERBOSE("updating user visible log file");
	/* FIXME write to logfile.new and rename */
	fh = fopen(r->logfile, "w");
	if (fh == NULL) {
		ERROR("unable to write user errors to %s: %s",
		      r->logfile, strerror(errno));
		return;
	}
	cur = r->first;
	while (cur != NULL) {
		next = cur->next;
		if (cur->timestamp + r->duration < now) {
			r->first = cur->next;
			tsdfx_logentry_destroy(cur);
			cur = NULL;
			if (r->first == NULL) {
				r->last = NULL;
			}
		}
		if (cur != NULL) {
			fprintf(fh, "%s\n", cur->msg);
		}
		cur = next;
	}
	fclose(fh);
}
