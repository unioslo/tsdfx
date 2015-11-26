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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if HAVE_BSD_UNISTD_H
#include <bsd/unistd.h>
#endif

#include "tsd/pidfile.h"

#include "tsdfx.h"

#ifndef PIDFILENAME
#define PIDFILENAME "/var/run/tsdfx.pid"
#endif

#if HAVE_SETPROCTITLE_INIT
extern char **environ;
#endif

static void
usage(void)
{

	fprintf(stderr, "usage: tsdfx [-1nv] "
	    "[-l logname] [-C copier] [-p pidfile] [-S scanner] -m mapfile\n");
	exit(1);
}

static void
showversion(void)
{
	fprintf(stderr, "%s\n\nReport bugs to %s and visit\n%s to learn more.\n\n",
	    PACKAGE_STRING, PACKAGE_BUGREPORT, PACKAGE_URL);
	exit(1);
}

int
main(int argc, char *argv[])
{
	const char *logfile, *mapfile, *pidfilename;
	struct tsd_pidfh *pidfh;
	int opt;
	pid_t pid;

#if HAVE_SETPROCTITLE_INIT
	setproctitle_init(argc, argv, environ);
#endif
#if HAVE_SETPROCTITLE
	setproctitle("master");
#endif

	logfile = mapfile = NULL;
	pidfilename = PIDFILENAME;
	while ((opt = getopt(argc, argv, "1C:hl:m:np:S:vV")) != -1)
		switch (opt) {
		case '1':
			++tsdfx_oneshot;
			break;
		case 'C':
			tsdfx_copier = optarg;
			break;
		case 'l':
			logfile = optarg;
			break;
		case 'm':
			mapfile = optarg;
			break;
		case 'n':
			++tsdfx_dryrun;
			break;
		case 'p':
			pidfilename = optarg;
			break;
		case 'S':
			tsdfx_scanner = optarg;
			break;
		case 'v':
			++tsdfx_verbose;
			break;
		case 'V':
			showversion();
			break;
		default:
			usage();
		}

	argc -= optind;
	argv += optind;

	if (argc != 0)
		usage();
	if (mapfile == NULL)
		usage();

	/*
	 * chdir() to a safe place.  Do this before trying to read the map
	 * file, in case we were given a relative path; we want to fail
	 * right away rather than upon receipt of a SIGHUP at some
	 * indeterminate later time.
	 */
	if (chdir("/var/empty") != 0 && chdir("/") != 0) {
		ERROR("/: chdir(): %s", strerror(errno));
		exit(1);
	}

	if (tsd_log_init("tsdfx", logfile) != 0)
		exit(1);
	if (tsdfx_init(mapfile) != 0)
		exit(1);

	if (!tsdfx_oneshot) {
		NOTICE("creating pid file %s", pidfilename);
		pid = 0;
		pidfh = tsd_pidfile_open(pidfilename, 0644, &pid);
		if (pidfh == NULL) {
			ERROR("unable to create pid file: %s", strerror(errno));
			exit(1);
		}

		if (0 > daemon(0, 0)) {
			ERROR("unable to daemonize: %s", strerror(errno));
			exit(1);
		}
		if (tsd_pidfile_write(pidfh) != 0) {
			ERROR("unable to write pid to file: %s", strerror(errno));
			exit(1);
		}
	} else {
		NOTICE("not creating pid file");
	}

	tsdfx_run(mapfile);

	tsdfx_exit();

	tsd_log_exit();

	if (!tsdfx_oneshot) {
		NOTICE("removing pid file %s", pidfilename);
		tsd_pidfile_remove(pidfh);
		pidfh = NULL;
	}

	exit(0);
}
