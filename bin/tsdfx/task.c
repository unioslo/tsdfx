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
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#include <sys/wait.h>

#include <err.h>

#include "tsdfx_log.h"
#include "tsdfx_task.h"

/*
 * Poll a task to see if it's still running.
 */
int
tsdfx_task_poll(pid_t pid, enum task_state *state)
{
	int ret, status;

	VERBOSE("pid %d", (int)pid);

	if (*state != TASK_RUNNING && *state != TASK_STOPPING)
		return (-1);
	ret = waitpid(pid, &status, WNOHANG);
	if (ret < 0) {
		/* already reaped, or something is wrong */
		warn("waitpid(%d)", (int)pid);
		*state = TASK_DEAD;
		return (-1);
	} else if (ret == 0) {
		/* still running */
		return (0);
	} else if (ret == pid) {
		if (!WIFEXITED(status))
			*state = TASK_DEAD;
		else if (WEXITSTATUS(status) != 0)
			*state = TASK_FAILED;
		else
			*state = TASK_STOPPED;
		return (0);
	} else {
		/* wtf? */
		warnx("waitpid(%d) returned %d", (int)pid, ret);
		return (-1);
	}
}
