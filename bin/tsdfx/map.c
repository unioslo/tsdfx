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

#include <sys/stat.h>

#include <err.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <tsd/strutil.h>

#include "tsdfx_log.h"
#include "tsdfx_map.h"
#include "tsdfx_scan.h"

struct map {
	char srcpath[PATH_MAX];
	char dstpath[PATH_MAX];
	struct scan_task *task;
};

static struct map **map;
static size_t map_sz;
static int map_len;

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

	if ((m = calloc(1, sizeof *m)) == NULL) {
		warn("calloc()");
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
 * Delete a struct map
 */
static void
map_delete(struct map *m)
{

	if (m != NULL) {
		tsdfx_scan_delete(m->task);
		free(m);
	}
}

/*
 * Compare two maps
 */
static int
map_compare(const void *a, const void *b)
{
	const struct map *ma = *(const struct map * const *)a;
	const struct map *mb = *(const struct map * const *)b;

	return (strcmp(ma->srcpath, mb->srcpath));
}

/*
 * Read the map file
 */
static int
map_read(const char *fn, struct map ***map, size_t *map_sz, int *map_len)
{
	FILE *f;
	char **words;
	int i, lno, nwords;
	struct map **m, **tm;
	size_t sz;
	int len;

	if ((f = fopen(fn, "r")) == NULL) {
		warn("%s", fn);
		return (-1);
	}
	words = NULL;
	nwords = 0;
	m = tm = NULL;
	sz = 0;
	len = 0;
	lno = 0;
	while ((words = tsd_readlinev(f, &lno, &nwords)) != NULL) {
		if (nwords == 0)
			continue;
		if (nwords != 3 || strcmp(words[1], "=>") != 0) {
			warnx("%s:%d: syntax error", fn, lno);
			goto fail;
		}
		if (len >= (int)sz) {
			sz = sz ? sz * 2 : 32;
			if ((tm = realloc(m, sz * sizeof *tm)) == NULL)
				goto fail;
			m = tm;
		}
		if ((m[len] = map_new(fn, lno, words[0], words[2])) == NULL)
			goto fail;
		++len;
		for (i = 0; i < nwords; ++i)
			free(words[i]);
		free(words);
	}
	if (ferror(f) || errno != 0) {
		warn("%s", fn);
		goto fail;
	}
	fclose(f);
	qsort(m, len, sizeof *m, map_compare);
	*map = m;
	*map_sz = sz;
	*map_len = len;
	return (0);
fail:
	if (words != NULL) {
		for (i = 0; i < nwords; ++i)
			free(words[i]);
		free(words);
	}
	for (i = 0; i < len; ++i)
		map_delete(m[i]);
	free(m);
	fclose(f);
	return (-1);
}

/*
 * Reload the map from the specified file.
 */
int
map_reload(const char *fn)
{
	struct map **newmap;
	size_t newmap_sz;
	int newmap_len;
	int i, j, res;

	/* read the new map */
	VERBOSE("loading %s", fn);
	if (map_read(fn, &newmap, &newmap_sz, &newmap_len) != 0)
		return (-1);
	/* XXX add code to check for duplicates */
	/* first, create new tasks */
	i = j = 0;
	while (j < newmap_len) {
		res = (i < map_len) ?
		    strcmp(map[i]->srcpath, newmap[j]->srcpath) : 1;
		if (res == 0) {
			/* unchanged task */
			VERBOSE("keeping %s", map[i]->srcpath);
			++i, ++j;
		} else if (res < 0) {
			/* deleted task */
			VERBOSE("dropping %s", map[i]->srcpath);
			++i;
		} else if (res > 0) {
			/* new task */
			VERBOSE("adding %s", newmap[j]->srcpath);
			newmap[j]->task =
			    tsdfx_scan_new(newmap[j]->srcpath);
			if (newmap[j]->task == NULL)
				goto fail;
			++j;
		} else {
			/* unreachable */
		}
	}
	/* copy unchanged tasks */
	i = j = 0;
	while (i < map_len) {
		res = (j < newmap_len) ?
		    strcmp(map[i]->srcpath, newmap[j]->srcpath) : -1;
		if (res == 0) {
			/* unchanged task */
			newmap[j]->task = map[i]->task;
			map[i]->task = NULL;
			++i, ++j;
		} else if (res < 0) {
			/* deleted task */
			++i;
		} else if (res > 0) {
			/* new task */
			++j;
		} else {
			/* unreachable */
		}
	}
	/* delete the old map and any tasks that weren't copied over */
	for (i = 0; i < map_len; ++i)
		map_delete(map[i]);
	free(map);
	map = newmap;
	map_sz = newmap_sz;
	map_len = newmap_len;
	for (i = 0; i < map_len; ++i)
		VERBOSE("map: %s -> %s", map[i]->srcpath, map[i]->dstpath);
	return (0);
fail:
	for (j = 0; i < newmap_len; ++j)
		map_delete(newmap[j]);
	free(newmap);
	return (-1);
}
