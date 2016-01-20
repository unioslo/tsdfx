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

static char *tsd_log_filename;
static FILE *tsd_log_file;
static char *tsd_userlog_filename;
static FILE *tsd_userlog_file;
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
	if (level == TSD_LOG_LEVEL_USERERROR && tsd_userlog_file != NULL)
		fprintf(tsd_userlog_file, LOGFMT, timestr, (int)getpid(),
		    tsd_log_level2str(level), file, line, func, msgbuffer);

	free(msgbuffer);
	msgbuffer = NULL;
	errno = serrno;
}

/*
 * Close a log destination
 */
static void
tsd_log_closelog(char **fnp, FILE **fp)
{

	if (*fp != NULL)
		fclose(*fp);
	fp = NULL;
	if (*fnp != NULL)
		free(*fnp);
	fnp = NULL;
}

/*
 * Parse a log destination specification and open it
 */
static int
tsd_log_initlog(char **fnp, FILE **fp, const char *logspec)
{
	char *fn;
	FILE *f;
	int serrno;

	if (logspec == NULL || *logspec == '\0') {
		errno = ENOENT;
		return (-1);
	} else if (strcmp(logspec, ":stderr") == 0) {
		logspec = "/dev/stderr";
	} else if (logspec[0] == ':') {
		errno = EINVAL;
		return (-1);
	} else if ((fn = strdup(logspec)) == NULL) {
		return (-1);
	}
	if ((f = fopen(logspec, "a")) == NULL) {
		serrno = errno;
		free(fn);
		errno = serrno;
		return (-1);
	}
	setvbuf(f, NULL, _IOLBF, 0);
	tsd_log_closelog(fnp, fp);
	*fnp = fn;
	*fp = f;
	return (0);
}

/*
 * Specify a optional destination for user errors, in addition to the
 * standard log destination.  This has to be either ":stderr" or the path
 * to a file (or device node).  Passing NULL or an empty string resets the
 * user log destination so user errors only go to the standard log.
 */
int
tsd_log_userlog(const char *logspec)
{

	if (logspec == NULL || *logspec == '\0')
		tsd_log_closelog(&tsd_userlog_filename, &tsd_userlog_file);
	return (tsd_log_initlog(&tsd_userlog_filename, &tsd_userlog_file,
	    logspec));
}

/*
 * Specify a destination for log messages.  Passing NULL or an empty
 * string resets the log destination to stderr.
 */
int
tsd_log_init(const char *ident, const char *logspec)
{

	if (logspec == NULL || *logspec == '\0')
		logspec = ":stderr";
	if (strcmp(logspec, ":syslog") == 0) {
		tsd_log_closelog(&tsd_log_filename, &tsd_log_file);
		tsd_log_ident = ident ? ident : "tsd";
		openlog(tsd_log_ident, LOG_CONS | LOG_PID | LOG_NDELAY,
		    LOG_LOCAL3);
	} else {
		if (tsd_log_initlog(&tsd_log_filename, &tsd_log_file,
		    logspec) != 0)
			return (-1);
		tsd_log_ident = NULL;
	}
	return (0);
}

/*
 * Close all log destinations
 */
int
tsd_log_exit(void)
{

	tsd_log_closelog(&tsd_log_filename, &tsd_log_file);
	tsd_log_closelog(&tsd_userlog_filename, &tsd_userlog_file);
	return (0);
}

const char *
tsd_log_getname(void)
{

	return (tsd_log_filename ? tsd_log_filename : ":syslog");
}

const char *
tsd_userlog_getname(void)
{

	return (tsd_userlog_filename);
}
