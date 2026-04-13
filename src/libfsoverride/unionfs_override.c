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

/*
 * fdescfs is expected to be mounted at this path with the linrdlnk option:
 *   mount -t fdescfs -o linrdlnk fdescfs /dev/fd
 * With linrdlnk, each entry FDESCFS_PATH/<fd> is a symlink to the referenced
 * file or directory. To resolve a dirfd + relative path, readlink(2) is used
 * to obtain the directory's real path, which is then joined with the filename.
 */
#define	FDESCFS_PATH	"/dev/fd"

typedef int (*orig_rename_type)(const char *, const char *);
typedef int (*orig_renameat_type)(int, const char *, int, const char *);

static orig_rename_type orig_rename;
static orig_renameat_type orig_renameat;

static int mv_wrapper(const char *, const char *);

static int
fdesc_resolve(int dirfd, const char *path, char *buf, size_t bufsz)
{
	char linkpath[PATH_MAX], dirpath[PATH_MAX];
	ssize_t n;

	snprintf(linkpath, sizeof(linkpath), FDESCFS_PATH "/%d", dirfd);
	n = readlink(linkpath, dirpath, sizeof(dirpath) - 1);
	if (n == -1) {
		return (-1);
	}
	dirpath[n] = '\0';
	snprintf(buf, bufsz, "%s/%s", dirpath, path);
	return (0);
}

static __attribute__((constructor)) void
fsoverride_init(void)
{

	orig_rename = (orig_rename_type)dlsym(RTLD_NEXT, "rename");
	if (orig_rename == NULL) {
		fprintf(stderr, "libfsoverride: dlsym(rename) failed: %s\n",
		    dlerror());
	}
	orig_renameat = (orig_renameat_type)dlsym(RTLD_NEXT, "renameat");
	if (orig_renameat == NULL) {
		fprintf(stderr, "libfsoverride: dlsym(renameat) failed: %s\n",
		    dlerror());
	}
}

int
rename(const char *oldpath, const char *newpath)
{

	if (orig_rename != NULL && orig_rename(oldpath, newpath) == 0) {
		return (0);
	}
	return (mv_wrapper(oldpath, newpath));
}

int
renameat(int olddirfd, const char *oldpath, int newdirfd, const char *newpath)
{
	char full_old[PATH_MAX], full_new[PATH_MAX];

	if (orig_renameat != NULL &&
	    orig_renameat(olddirfd, oldpath, newdirfd, newpath) == 0) {
		return (0);
	}
	if (olddirfd == AT_FDCWD) {
		strlcpy(full_old, oldpath, sizeof(full_old));
	} else {
		if (fdesc_resolve(olddirfd, oldpath, full_old,
		    sizeof(full_old)) == -1) {
			return (-1);
		}
	}
	if (newdirfd == AT_FDCWD) {
		strlcpy(full_new, newpath, sizeof(full_new));
	} else {
		if (fdesc_resolve(newdirfd, newpath, full_new,
		    sizeof(full_new)) == -1) {
			return (-1);
		}
	}
	if (strcmp(full_old, full_new) == 0) {
		return (0);
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
		fprintf(stderr, "libfsoverride: fork failed: %s\n",
		    strerror(errno));
		return (-1);
	}
	if (pid == 0) {
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
		unsetenv("LD_PRELOAD");
		argv[0] = "/bin/mv";
		argv[1] = "-f";
		argv[2] = (char *) oldpath;
		argv[3] = (char *) newpath;
		argv[4] = NULL;
		(void) execv(*argv, argv);
		_exit(1);
	}
	status = 0;
	while (1) {
		cc = waitpid(pid, &status, 0);
		if (cc == -1 && errno == EINTR) {
			continue;
		} else if (cc == -1) {
			fprintf(stderr, "libfsoverride: waitpid failed: %s\n",
			    strerror(errno));
			return (-1);
		}
		break;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		return (0);
	}
	errno = EIO;
	return (-1);
}
