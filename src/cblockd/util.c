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

#include <openssl/sha.h>
#include <sanitizer/msan_interface.h>

#include "termbuf.h"
#include "main.h"
#include "dispatch.h"
#include "config.h"

#include <cblock/libcblock.h>

void
gen_sha256_string(unsigned char *hash, char *output)
{
	int k;

	__msan_unpoison(hash, SHA256_DIGEST_LENGTH);
	__msan_unpoison(output, SHA256_DIGEST_LENGTH * 2);
	for (k = 0; k < SHA256_DIGEST_LENGTH; k++) {
		sprintf(output + (k * 2), "%02x", hash[k]);
	}
	output[64] = '\0';
}

char *
gen_sha256_instance_id(char *instance_name)
{
	u_char hash[SHA256_DIGEST_LENGTH];
	char inbuf[128], *ret;
	char outbuf[128+1];
	SHA256_CTX sha256;
	char buf[32];

	bzero(inbuf, sizeof(inbuf));
	arc4random_buf(inbuf, sizeof(inbuf) - 1);
	bzero(outbuf, sizeof(outbuf));
	SHA256_Init(&sha256);
	SHA256_Update(&sha256, inbuf, strlen(inbuf));
	SHA256_Final(hash, &sha256);
	gen_sha256_string(&hash[0], outbuf);
	sprintf(buf, "%.10s", outbuf);
	ret = strdup(buf);
	return (ret);
}
