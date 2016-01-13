/*-
 * Copyright (c) 2013-2016 The University of Oslo
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

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if HAVE_BSD_STDLIB_H
#include <bsd/stdlib.h>
#endif

#include <tsd/assert.h>
#include <tsd/log.h>
#include <tsd/strutil.h>
#include <tsd/task.h>

#include "tsdfx.h"
#include "tsdfx_map.h"
#include "tsdfx_scan.h"
#include "tsdfx_copy.h"
#include "tsdfx_recentlog.h"

struct tsdfx_map {
	char name[NAME_MAX];
	char srcpath[PATH_MAX];
	char dstpath[PATH_MAX];
	struct tsd_task *task;
	struct tsdfx_recentlog *errlog;
};

static struct tsdfx_map **map;
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
 * Create a new struct tsdfx_map
 */
static struct tsdfx_map *
map_new(const char *fn, int n, const char *name, const char *src, const char *dst)
{
	struct tsdfx_map *m;
	char logpath[PATH_MAX];

	if ((m = calloc(1, sizeof *m)) == NULL) {
		ERROR("calloc()");
		return (NULL);
	}
	if (strlcpy(m->name, name, sizeof m->name) >= sizeof m->name) {
		ERROR("%s:%d: name too long", fn, n);
		free(m);
		return (NULL);
	}
	if (verify_path(src, m->srcpath) != 0) {
		ERROR("%s:%d: invalid source path", fn, n);
		free(m);
		return (NULL);
	}
	if (verify_path(dst, m->dstpath) != 0) {
		ERROR("%s:%d: invalid destination path", fn, n);
		free(m);
		return (NULL);
	}

	if ((int)sizeof(logpath) <= snprintf(logpath, sizeof logpath,
				       "%s/tsdfx-error.log", m->dstpath)) {
		ERROR("%s:%d: name too long", fn, n);
		free(m);
		return (NULL);
	}
	if ((m->errlog = tsdfx_recentlog_new(logpath, 5 * 60)) == NULL) {
		ERROR("%s: unable to set up log", logpath);
		free(m);
		return (NULL);
	}
	return (m);
}

/*
 * Delete a struct tsdfx_map
 */
static void
map_delete(struct tsdfx_map *m)
{

	if (m != NULL) {
		tsdfx_scan_delete(m->task);
		tsdfx_recentlog_destroy(m->errlog);
		m->errlog = NULL;
		free(m);
	}
}

/*
 * Compare two maps
 */
static int
map_compare(const void *a, const void *b)
{
	const struct tsdfx_map *ma = *(const struct tsdfx_map * const *)a;
	const struct tsdfx_map *mb = *(const struct tsdfx_map * const *)b;

	return (strcmp(ma->name, mb->name));
}

/*
 * Read the map file
 *
 * XXX deduplication algorithm sucks - it should compare and warn about
 * identical source paths, not just names, but that would require sorting
 * the list twice: once by name and once by source path.
 */
static int
map_read(const char *fn, struct tsdfx_map ***map, size_t *map_sz, int *map_len)
{
	FILE *f;
	char **words, *p;
	int i, j, lno, nwords;
	struct tsdfx_map **m, **tm;
	size_t sz;
	int len;

	if ((f = fopen(fn, "r")) == NULL) {
		ERROR("%s: %s", fn, strerror(errno));
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
		/* expecting "name: srcpath => dstpath" */
		if (nwords != 4 || (p = strchr(words[0], ':')) == NULL ||
		    p[1] != '\0' || strcmp(words[2], "=>") != 0) {
			ERROR("%s:%d: syntax error", fn, lno);
			goto fail;
		}
		/* strip colon from name */
		*p = '\0';
		/* resize array if necessary */
		if (len >= (int)sz) {
			sz = sz ? sz * 2 : 32;
			if ((tm = realloc(m, sz * sizeof *tm)) == NULL)
				goto fail;
			m = tm;
		}
		/* create new map */
		if ((m[len] = map_new(fn, lno, words[0], words[1], words[3])) == NULL)
			goto fail;
		++len;
		/* done, free allocated memory */
		for (i = 0; i < nwords; ++i)
			free(words[i]);
		free(words);
	}
	if (ferror(f) || errno != 0) {
		ERROR("%s", fn);
		goto fail;
	}
	fclose(f);
	/* sort and deduplicate */
	qsort(m, len, sizeof *m, map_compare);
	for (i = 0; i + 1 < len; ++i) {
		for (j = i + 1; j < len; ++j) {
			if (strcmp(m[i]->name, m[j]->name) != 0)
				break;
			map_delete(m[j]);
			m[j] = NULL;
		}
		if (j > i + 1) {
			WARNING("removing duplicate entries for %s",
			    m[i]->srcpath);
			memmove(m + i + 1, m + j, (len - j) * sizeof *m);
			len -= j - (i + 1);
		}
	}
	/* good, let's return this */
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
 *
 * XXX is it really necessary to merge the old and new maps together?
 * simply replacing the old map with the new would be much simpler.
 */
int
tsdfx_map_reload(const char *fn)
{
	struct tsdfx_map **newmap;
	size_t newmap_sz;
	int newmap_len;
	int i, j, res;

	/* read the new map */
	NOTICE("loading %s", fn);
	if (map_read(fn, &newmap, &newmap_sz, &newmap_len) != 0)
		return (-1);
	/* first, create new tasks */
	i = j = 0;
	while (j < newmap_len) {
		res = (i < map_len) ?
		    strcmp(map[i]->name, newmap[j]->name) : 1;
		if (res == 0) {
			/* unchanged task */
			VERBOSE("keeping %s", map[i]->name);
			++i, ++j;
		} else if (res < 0) {
			/* deleted task */
			VERBOSE("dropping %s", map[i]->name);
			++i;
		} else if (res > 0) {
			/* new task */
			VERBOSE("adding %s", newmap[j]->name);
			newmap[j]->task =
			    tsdfx_scan_new(newmap[j], newmap[j]->srcpath);
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
		    strcmp(map[i]->name, newmap[j]->name) : -1;
		if (res == 0) {
			/* unchanged task */
			map_delete(newmap[j]);
			newmap[j] = map[i];
			map[i] = NULL;
			tsdfx_scan_rush(newmap[j]->task);
			++i, ++j;
		} else if (res < 0) {
			/* deleted task */
			map_delete(map[i]);
			map[i] = NULL;
			++i;
		} else if (res > 0) {
			/* new task */
			++j;
		} else {
			/* unreachable */
		}
	}
	/* the old map is now empty */
	for (i = 0; i < map_len; ++i)
		ASSERT(map[i] == NULL);
	free(map);
	map = newmap;
	map_sz = newmap_sz;
	map_len = newmap_len;
	for (i = 0; i < map_len; ++i)
		VERBOSE("map %s: %s -> %s", map[i]->name,
		    map[i]->srcpath, map[i]->dstpath);
	return (0);
fail:
	for (j = 0; i < newmap_len; ++j)
		map_delete(newmap[j]);
	free(newmap);
	return (-1);
}

/*
 * Process a file reported by the scanner.
 */
int
tsdfx_map_process(struct tsdfx_map *map, const char *path)
{

	return (tsdfx_copy_wrap(map->srcpath, map->dstpath, path));
}

/*
 * Check all our map entries to see if a scan task recently completed.  If
 * so, set up and kick off compare / copy tasks.  Reschedule the completed
 * scan tasks.
 */
int
tsdfx_map_sched(void)
{
	int i;

	for (i = 0; i < map_len; ++i) {
		/* nothing to do */
	}
	return (map_len);
}

int
tsdfx_map_init(void)
{
	return tsdfx_recentlog_init();
}

int
tsdfx_map_exit(void)
{
	int i;

	for (i = 0; i < map_len; ++i) {
		map_delete(map[i]);
		map[i] = NULL;
	}
	free(map);
	tsdfx_recentlog_exit();
	return (0);
}

void
tsdfx_map_log(struct tsdfx_map *map, const char *msg)
{
	tsdfx_recentlog_log(map->errlog, msg);
}
