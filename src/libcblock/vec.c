/*-
 * Copyright (c) 2020 Christian S.J. Peron
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include <cblock/libcblock.h>

char *
vec_marshal(vec_t *vec)
{
	size_t totlen, slen, count, k;
	char *const *copy;
	char *bp;

	copy = vec_return(vec);
	count = 0;
	totlen = 0;
	/*
	 * Iterate through array/vector to count the number of strings, but also
	 * to keep track of the size for each string.
	 */
	while ((bp = *copy++)) {
		totlen += strlen(bp);
		count++;
	}
	totlen += count;	/* \0 delimeter for each string */
	totlen += 1;		/* \0 terminating \0 */
	vec->vec_marshalled = malloc(totlen);
	if (vec->vec_marshalled == NULL) {
		return (NULL);
	}
	copy = vec_return(vec);
	vec->vec_marshalled_len = totlen;
	bp = vec->vec_marshalled;
	for (k = 0; k < count; k++) {
		slen = strlen(copy[k]);
		bcopy(copy[k], bp, slen);
		bp += slen;
		*bp++ = '\0';
	}
	*bp = '\0';
	return (vec->vec_marshalled);
}

char **
vec_unmarshal(vec_t *vec, char *marshalled, size_t len)
{
	size_t count, slen;
	char *bp, *ent;
	int k, j, h;

	if (marshalled == NULL) {
		return (NULL);
	}
	count = 0;
	for (k = 0; k < len; k++) {
		if (marshalled[k] == '\0') {
			count++;
		}
	}
	count--;
	vec->vec = calloc(count + 1, sizeof(char *));
	if (vec->vec == NULL) {
		return (NULL);
	}
	bp = marshalled;
	for (j = 0, k = 0; k < count; k++) {
		slen = strlen(bp);
		if (slen == 0) {
			bp += 1;
			continue;
		}
		ent = strdup(bp);
		if (ent == NULL) {
			for (h = 0; h < j; h++) {
				ent = vec->vec[h];
				free(ent);
			}
			free(vec->vec);
			return (NULL);
		}
		vec->vec[j++] = ent;
		bp += slen;
		bp++;
	}
	vec->vec_used = j;
	vec->vec_alloc = j;
	vec->vec_flag = 0;
	vec->vec[j] = NULL;
	return (vec->vec);
}

vec_t *
vec_init(size_t vec_size)
{
	vec_t *vec;

	vec = calloc(1, sizeof(*vec));
	if (vec == NULL) {
		return (NULL);
	}
	vec->vec_alloc = vec_size;
	vec->vec_used = 0;
	vec->vec_flag = 0;
	if (vec_size == 0) {
		vec->vec = NULL;
		return (vec);
	}
	vec->vec = calloc(vec->vec_alloc, sizeof(char *));
	if (vec->vec == NULL) {
		return (NULL);
	}
	return (vec);
}

void
vec_append(vec_t *vec, char *string)
{

	if (vec->vec_flag == VEC_OVERFLOW) {
		return;
	}
	if (vec->vec_used == vec->vec_alloc) {
		vec->vec_flag = VEC_OVERFLOW;
		return;
	}
	vec->vec[vec->vec_used] = strdup(string);
	if (!vec->vec[vec->vec_used]) {
		vec->vec_flag = VEC_ENOMEM;
		return;
	}
	vec->vec_used++;
}

int
vec_finalize(vec_t *vec)
{

	vec->vec[vec->vec_used] = NULL;
	return (vec->vec_flag);
}

int
vec_merge(vec_t *from, vec_t *to)
{
	int k;

	if ((from->vec_used + to->vec_used) > to->vec_alloc) {
		return (VEC_OVERFLOW);
	}
	for (k = 0; k < from->vec_used; k++) {
		vec_append(to, from->vec[k]);
	}
	return (0);
}

char *
vec_join(vec_t *vec, char delim)
{
	size_t totlen, slen;
	char *bp, *cp;
	int k;

	if (vec->vec_used == 0) {
		return (NULL);
	}
	for (totlen = 0, k = 0; k < vec->vec_used; k++) {
		totlen += strlen(vec->vec[k]);
	}
	totlen += (vec->vec_used - 1);
	bp = malloc(totlen + 1);
	if (bp == NULL) {
		return (NULL);
	}
	bzero(bp, totlen + 1);
	cp = bp;
	for (k = 0; k < vec->vec_used; k++) {
		slen = strlen(vec->vec[k]);
		bcopy(vec->vec[k], cp, slen);
		cp += slen;
		if (k < (vec->vec_used - 1)) {
			*cp++ = delim;
		}
	}
	*cp = '\0';
	return (bp);
}

char **
vec_return(vec_t *vec)
{

	assert(vec->vec_flag == 0);
	return (vec->vec);
}

void
vec_free(vec_t *vec)
{
	int k;

	for (k = 0; k < vec->vec_used; k++) {
		free(vec->vec[k]);
	}
	free(vec);
}
