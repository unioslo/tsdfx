/*-
 * Copyright (c) 2012 Dag-Erling Smørgrav
 * Copyright (c) 2014 The University of Oslo
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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <tsd/strutil.h>

#define MIN_WORDV_SIZE	32

/*
 * Read a line from a file and split it into words.
 */

char **
tsd_readlinev(FILE *f, int *lineno, int *lenp)
{
	char *word, **wordv, **tmp;
	size_t wordlen, wordvsize;
	int ch, serrno, wordvlen;

	wordvsize = MIN_WORDV_SIZE;
	wordvlen = 0;
	if ((wordv = malloc(wordvsize * sizeof *wordv)) == NULL) {
		errno = ENOMEM;
		return (NULL);
	}
	wordv[wordvlen] = NULL;
	while ((word = tsd_readword(f, lineno, &wordlen)) != NULL) {
		if ((unsigned int)wordvlen + 1 >= wordvsize) {
			/* need to expand the array */
			wordvsize *= 2;
			tmp = realloc(wordv, wordvsize * sizeof *wordv);
			if (tmp == NULL) {
				errno = ENOMEM;
				break;
			}
			wordv = tmp;
		}
		/* insert our word */
		wordv[wordvlen++] = word;
		wordv[wordvlen] = NULL;
	}
	if (errno != 0) {
		/* I/O error or out of memory */
		serrno = errno;
		while (wordvlen--)
			free(wordv[wordvlen]);
		free(wordv);
		errno = serrno;
		return (NULL);
	}
	/* assert(!ferror(f)) */
	ch = fgetc(f);
	/* assert(ch == EOF || ch == '\n') */
	if (ch == EOF && wordvlen == 0) {
		free(wordv);
		return (NULL);
	}
	if (ch == '\n' && lineno != NULL)
		++*lineno;
	if (lenp != NULL)
		*lenp = wordvlen;
	return (wordv);
}
