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

#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tsdfx/strutil.h>

#include <tsdfx.h>

/*
 * Validate a path
 */
static int
verify_path(const char *path, char *buf)
{
	struct stat st;

	if (realpath(path, buf) == NULL)
		return (-1);
	if (stat(buf, &st) != 0)
		return (-1);
	if (!S_ISDIR(st.st_mode)) {
		errno = ENOTDIR;
		return (-1);
	}
	return (0);
}

/*
 * Create a new struct map
 */
static struct map *
map_new(const char *fn, int n, const char *src, const char *dst)
{
	struct map *m;

	if ((m = malloc(sizeof *m)) == NULL) {
		warn("malloc()");
		return (NULL);
	}
	if (verify_path(src, m->srcpath) != 0) {
		warn("%s:%d: invalid source path", fn, n);
		free(m);
		return (NULL);
	}
	if (verify_path(dst, m->dstpath) != 0) {
		warn("%s:%d: invalid destination path", fn, n);
		free(m);
		return (NULL);
	}
	return (m);
}

/*
 * Read the map file
 */
struct map *
map_read(const char *fn)
{
	FILE *f;
	struct map *m, *n;
	char **words;
	int i, len, lno;

	if ((f = fopen(fn, "r")) == NULL) {
		warn("%s", fn);
		return (NULL);
	}
	m = n = NULL;
	while ((words = tsdfx_readlinev(f, &lno, &len)) != NULL && len > 0) {
		if (len > 0) {
			if (len != 3 || strcmp(words[1], "=>") != 0) {
				warnx("%s:%d: syntax error", fn, lno);
				goto fail;
			}
			if ((m = map_new(fn, lno, words[0], words[2])) == NULL)
				goto fail;
			/* prepend to list */
			m->next = n;
			n = m;
		}
		for (i = 0; i < len; ++i)
			free(words[i]);
		free(words);
	}
	if (ferror(f) || errno != 0) {
		warn("%s", fn);
		goto fail;
	}
	fclose(f);
	return (n);
fail:
	if (words != NULL) {
		for (i = 0; i < len; ++i)
			free(words[i]);
		free(words);
	}
	while (n != NULL) {
		m = n;
		n = m->next;
		free(m->srcpath);
		free(m->dstpath);
		free(m);
	}
	fclose(f);
	return (NULL);
}
