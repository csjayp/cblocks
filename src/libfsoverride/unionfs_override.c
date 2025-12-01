/*-
 * Copyright (c) 2025 Christian S.J. Peron
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
#include <sys/fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <limits.h>

typedef int (*orig_rename_type)(const char *, const char *);
typedef int (*orig_renameat_type)(int, const char *, int, const char *);
static int mv_wrapper(const char *, const char *);


int
rename(const char *oldpath, const char *newpath)
{
	static orig_rename_type orig_rename;

	if (!orig_rename) {
		orig_rename = (orig_rename_type)dlsym(RTLD_NEXT, "rename");
	}
	if (orig_rename && orig_rename(oldpath, newpath) == 0) {
		return (0);
	}
	return (mv_wrapper(oldpath, newpath));
}

int
renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
	char full_old[PATH_MAX], full_new[PATH_MAX];
	static orig_renameat_type orig_renameat;

	if (!orig_renameat) {
		orig_renameat = (orig_renameat_type)dlsym(RTLD_NEXT, "renameat");
	}
	if (orig_renameat && orig_renameat(olddirfd, oldpath, newdirfd, newpath) == 0) {
		return (0);
	}
	if (olddirfd == AT_FDCWD) {
		(void) snprintf(full_old, sizeof(full_old), "%s", oldpath);
	} else {
		fprintf(stderr, "using fdesfs\n");
		(void) snprintf(full_old, sizeof(full_old), "/dev/fd/%d", olddirfd);
	}
	if (newdirfd == AT_FDCWD) {
		snprintf(full_new, sizeof(full_new), "%s", newpath);
	} else {
		fprintf(stderr, "using fdesfs\n");
		snprintf(full_new, sizeof(full_new), "/dev/fd/%d", newdirfd);
	}
	if (strcmp(full_old, full_new) == 0) {
		return 0;
	}
	return (mv_wrapper(full_old, full_new));
}

static int
mv_wrapper(const char *oldpath, const char *newpath)
{
    int status, cc;
    char *argv[8];
    pid_t pid;

    pid = fork();
    if (pid == -1) {
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        return -1;
    }
    if (pid == 0) {
        close(1);
        close(2);
        unsetenv("LD_PRELOAD");
        argv[0] = "/bin/mv";
        argv[1] = "-f";
        argv[2] = (char *) oldpath;
        argv[3] = (char *) newpath;
        argv[4] = NULL;
        (void) execv(*argv, argv);
    }
    status = 0;
    while (1) {
        cc = waitpid(pid, &status, 0);
        if (cc == -1 && errno == EINTR) {
            continue;
        } else if (cc == -1) {
            fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
        }
        break;
    }
    return status;
}
