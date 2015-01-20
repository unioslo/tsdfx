/*-
 * Copyright (c) 2013-2014 Universitetet i Oslo
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

#include <signal.h>
#include <unistd.h>

#include <tsd/log.h>

#include "tsdfx_map.h"
#include "tsdfx_scan.h"
#include "tsdfx_copy.h"
#include "tsdfx.h"

int tsdfx_oneshot = 0;

static volatile sig_atomic_t sighup;

/*
 * Signal handler
 */
static void
signal_handler(int sig)
{

	switch (sig) {
	case SIGHUP:
		++sighup;
		break;
	default:
		/* nothing */;
	}
}

/*
 * Initialization
 */
int
tsdfx_init(const char *mapfile)
{

	if (tsdfx_scan_init() != 0)
		return (-1);
	if (tsdfx_map_reload(mapfile) != 0)
		return (-1);
	return (0);
}

void
tsdfx_run(const char *mapfile)
{

	signal(SIGHUP, signal_handler);
	for (;;) {
		/* check for sighup */
		if (sighup) {
			sighup = 0;
			if (tsdfx_map_reload(mapfile) != 0)
				WARNING("failed to reload map file");
		}

		/* start and run scan tasks */
		tsdfx_scan_sched();
		tsdfx_scan_iter();

		/* check scan tasks and create copy tasks as needed */
		tsdfx_map_iter();

		/* start and run copy tasks */
		tsdfx_copy_sched();
		tsdfx_copy_iter();

		/* in oneshot mode, are we done? */
		if (tsdfx_oneshot && scan_running == 0 && copy_running == 0)
			break;

		usleep(100 * 1000);
	}
	signal(SIGHUP, SIG_DFL);
}
