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
#include <sys/types.h>
#include <sys/queue.h>

#include <stdio.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/evp.h>

#include "termbuf.h"
#include "main.h"
#include "dispatch.h"
#include "config.h"

#include <cblock/libcblock.h>

void
gen_sha256_string(u_char *hash, char *output, u_int len)
{
	u_int k;

	// NB: assumed zeroed out `output` buffer
	for (k = 0; k < len; k++) {
		(void) sprintf(output + (k * 2), "%02x", hash[k]);
	}
}

char *
gen_sha256_instance_id(char *instance_name __attribute__((unused)))
{
	char buf[32], inbuf[128], *ret, outbuf[EVP_MAX_MD_SIZE * 2];
	u_char hash[EVP_MAX_MD_SIZE];
	EVP_MD_CTX *hash_ctx;
	u_int dlen;

	bzero(inbuf, sizeof(inbuf));
	bzero(hash, sizeof(hash));
	arc4random_buf(inbuf, sizeof(inbuf) - 1);
	bzero(outbuf, sizeof(outbuf));
	hash_ctx = EVP_MD_CTX_new();
	if (hash_ctx == NULL) {
		return (NULL);
	}
	if (!EVP_DigestInit_ex(hash_ctx, EVP_sha256(), NULL)) {
		fprintf(stderr, "EVP_DigestInit_ex failed\n");
		return (NULL);
	}
	if (!EVP_DigestUpdate(hash_ctx, inbuf, strlen(inbuf))) {
		fprintf(stderr, "EVP_DigestUpdate failed\n");
		return (NULL);
	}
	if (!EVP_DigestFinal_ex(hash_ctx, hash, &dlen)) {
		fprintf(stderr, "EVP_DigestFinal_ex failed\n");
		return (NULL);
	}
	EVP_MD_CTX_free(hash_ctx);
	gen_sha256_string(&hash[0], outbuf, dlen);
	(void) sprintf(buf, "%.10s", outbuf);
	ret = strdup(buf);
	return (ret);
}
