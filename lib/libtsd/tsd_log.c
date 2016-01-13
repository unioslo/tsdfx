/*-
 * Copyright (c) 2013-2015 The University of Oslo
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include <tsd/log.h>

int tsd_log_quiet = 0;
int tsd_log_verbose = 0;
int tsd_log_usererror_to_stderr = 0;

static char *tsd_log_filename;
static FILE *tsd_log_file;
const char *tsd_log_ident;

static int
tsd_log_level2syslog(tsd_log_level_t level)
{

	switch (level) {
	case TSD_LOG_LEVEL_VERBOSE:
		return (LOG_INFO);
	case TSD_LOG_LEVEL_NOTICE:
		return (LOG_NOTICE);
	case TSD_LOG_LEVEL_WARNING:
		return (LOG_WARNING);
	case TSD_LOG_LEVEL_ERROR:
	case TSD_LOG_LEVEL_USERERROR:
		return (LOG_ERR);
	}
	return (0);
}

static const char *
tsd_log_level2str(tsd_log_level_t level)
{

	switch (level) {
	case TSD_LOG_LEVEL_VERBOSE:
		return ("verbose");
	case TSD_LOG_LEVEL_NOTICE:
		return ("notice");
	case TSD_LOG_LEVEL_WARNING:
		return ("warning");
	case TSD_LOG_LEVEL_ERROR:
	case TSD_LOG_LEVEL_USERERROR:
		return ("error");
	}
	return ("unknown");
}

/*
 * Log a message.
 *
 * Since this is often called during error handling, we save and restore
 * errno to spare the caller from having to do it.
 */
void
tsd_log(tsd_log_level_t level, const char *file, int line, const char *func,
    const char *fmt, ...)
{
	char *msgbuffer;
	char timestr[32];
	time_t now;
	va_list ap;
	int serrno;

	msgbuffer = NULL;

	/*
	 * Log levels:
	 *
	 *   quiet: print only warnings and errors
	 *   normal: print notices, warnings and errors
	 *   verbose: print everything
	 *
	 * If both the verbose and quiet flags are set, verbose wins.
	 */
	if (!tsd_log_verbose) {
		if (level <= TSD_LOG_LEVEL_VERBOSE ||
		    (level <= TSD_LOG_LEVEL_NOTICE && tsd_log_quiet))
			return;
	}

	/* make sure logging do not change errno */
	serrno = errno;

	va_start(ap, fmt);
	if (vasprintf(&msgbuffer, fmt, ap) < 0)
		msgbuffer = strdup(fmt);
	va_end(ap);

	if (tsd_log_file == NULL) {
		syslog(tsd_log_level2syslog(level), "%s:%d %s() %s",
		    file, line, func, msgbuffer);
	}
	now = time(NULL);
	strftime(timestr, sizeof timestr, "%Y-%m-%d %H:%M:%S UTC",
		 gmtime(&now));
#define LOGFMT "%s [%d] %s: %s:%d %s() %s\n"
	if (tsd_log_file != NULL)
		fprintf(tsd_log_file, LOGFMT, timestr, (int)getpid(),
			tsd_log_level2str(level), file, line, func, msgbuffer);
	if (level == TSD_LOG_LEVEL_USERERROR && tsd_log_file != stderr)
		fprintf(stderr, LOGFMT, timestr, (int)getpid(),
			tsd_log_level2str(level), file, line, func, msgbuffer);

	free(msgbuffer);
	msgbuffer = NULL;
	errno = serrno;
}

void
tsd_log_usererror2stderr(const int usererror2stderr)
{

	tsd_log_usererror_to_stderr = usererror2stderr;
}

int
tsd_log_init(const char *ident, const char *logfile)
{

	tsd_log_ident = ident ? ident : "tsd";
	if (logfile == NULL)
		logfile = ":stderr";
	if ((tsd_log_filename = strdup(logfile)) == NULL)
		return (-1);
	if (strcmp(logfile, ":stderr") == 0)
		tsd_log_file = stderr;
	else if (strcmp(logfile, ":syslog") == 0)
		openlog(ident, LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL3);
	else if ((tsd_log_file = fopen(logfile, "a")) == NULL)
		return (-1);
	if (tsd_log_file != NULL)
		setvbuf(tsd_log_file, NULL, _IOLBF, 0);
	return (0);
}

int
tsd_log_exit(void)
{
	free(tsd_log_filename);
	return (0);
}

const char *
tsd_log_getname(void)
{

	return (tsd_log_filename);
}
