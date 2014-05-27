/*-
 * Copyright (c) 2012 Universitetet i Oslo
 * Copyright (c) 2012 Dag-Erling Smørgrav
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
 * 3. Neither the name of the author nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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

#ifndef TSD_SHA1_H_INCLUDED
#define TSD_SHA1_H_INCLUDED

#define SHA1_DIGEST_LEN 20

typedef void *sha1_ctx;

void *tsd_sha1_init(void);
void tsd_sha1_discard(void *);
void tsd_sha1_update(void *, const void *, size_t);
void tsd_sha1_final(void *, void *);
int tsd_sha1_complete(const void *, size_t, void *);

static inline sha1_ctx
sha1_init(void)
{

	return (tsd_sha1_init());
}

static inline void
sha1_discard(sha1_ctx ctx)
{

	tsd_sha1_discard(ctx);
}

static inline void
sha1_update(sha1_ctx ctx, const void *msg, size_t msglen)
{

	tsd_sha1_update(ctx, msg, msglen);
}

static inline void
sha1_final(sha1_ctx ctx, void *md)
{

	tsd_sha1_final(ctx, md);
}

static inline int
sha1_complete(const void *msg, size_t msglen, void *md)
{

	return (tsd_sha1_complete(msg, msglen, md));
}

#endif
