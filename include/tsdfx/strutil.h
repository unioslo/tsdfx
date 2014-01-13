/*-
 * Copyright (c) 2011-2012 Dag-Erling Smørgrav
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

#ifndef TSDFX_STRUTIL_H_INCLUDED
#define TSDFX_STRUTIL_H_INCLUDED

#ifndef HAVE_STRLCAT
size_t tsdfx_strlcat(char *, const char *, size_t);
#undef strlcat
#define strlcat(arg, ...) tsdfx_strlcat(arg, __VA_ARGS__)
#endif

#ifndef HAVE_STRLCPY
size_t tsdfx_strlcpy(char *, const char *, size_t);
#undef strlcpy
#define strlcpy(arg, ...) tsdfx_strlcpy(arg, __VA_ARGS__)
#endif

int tsdfx_straddch(char **, size_t *, size_t *, int);
#ifdef _IOFBF
char *tsdfx_readword(FILE *, int *, size_t *);
char **tsdfx_readlinev(FILE *, int *, int *);
#endif

#endif
