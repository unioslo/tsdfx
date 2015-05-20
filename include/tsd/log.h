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

#ifndef TSD_LOG_H_INCLUDED
#define TSD_LOG_H_INCLUDED

void tsd_log(const char *, int, const char *, const char *, ...);
int tsd_log_init(void);

extern int tsd_log_quiet;
extern int tsd_log_verbose;

#define VERBOSE(...) \
	do {								\
		if (tsd_log_verbose)					\
			tsd_log(__FILE__, __LINE__, __func__,		\
			    __VA_ARGS__);				\
	} while (0)

#define NOTICE(...) \
	do {								\
		if (!tsd_log_quiet)					\
			tsd_log(__FILE__, __LINE__, __func__,		\
			    __VA_ARGS__);				\
	} while (0)

#define WARNING(...) \
	do {								\
		tsd_log(__FILE__, __LINE__, __func__,			\
		    __VA_ARGS__);					\
	} while (0)

#define ERROR(...) \
	do {								\
		tsd_log(__FILE__, __LINE__, __func__,			\
		    __VA_ARGS__);					\
	} while (0)

#endif
