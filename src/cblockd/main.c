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
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ttycom.h>
#include <sys/wait.h>
#include <sys/param.h>
#include <sys/un.h>
#include <netinet/in.h>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <getopt.h>
#include <stdlib.h>
#include <signal.h>
#include <err.h>
#include <pthread.h>

#include "termbuf.h"
#include "main.h"
#include "sock_ipc.h"
#include "dispatch.h"

#include "config.h"
#include "cblock.h"

#include <cblock/libcblock.h>

struct global_params gcfg;

static char *banner =
	"            __ __ __    __            __\n" \
	".----.-----|  |  |  |--|  .-----.----|  |--.-----.\n" \
	"|  __|  -__|  |  |  _  |  |  _  |  __|    <|__ --|\n" \
	"|____|_____|__|__|_____|__|_____|____|__|__|_____|\n";

static char *data_sub_dirs[] = {
	"spool",
	"lib",
	"locks",
	"images",
	"instances",
	"unions",
	"networks",
	NULL,
};

static struct option long_options[] = {
	{ "ipv4",		no_argument,	0, '4' },
	{ "ipv6",		no_argument,	0, '6' },
	{ "unix-sock",		required_argument, 0, 'U' },
	{ "listen-host",	required_argument, 0, 's' },
	{ "listen-port",	required_argument, 0, 'p' },
	{ "tty-buffer-size",	required_argument, 0, 'T' },
	{ "data-directory",	required_argument, 0, 'd' },
	{ "help",		no_argument, 0, 'h' },
	{ "ufs",		no_argument, 0, 'u' },
	{ "zfs",		no_argument, 0, 'z' },
	{ "fuse-unionfs",	no_argument, 0, 'N' },
	{ "verbose",		no_argument, 0, 'v' },
	{ "background",		no_argument, 0, 'b' },
	{ "sock-owner",		required_argument, 0, 'o' },
	{ "logfile",		required_argument, 0, 'l' },
	{ "create-forge",	required_argument, 0, 'f' },
	{ 0, 0, 0, 0 }
};

static void
usage(void)
{

	(void) fprintf(stderr,
	    "Options\n"
	    " -4, --ipv4                  IPv4 sockets only\n"
	    " -6, --ipv6                  IPv6 sockets only\n"
	    " -U, --unix-sock=PATH        Path to UNIX socket\n"
	    " -s, --listen-host=HOST      Listen host/address\n"
	    " -p, --listen-port=PORT      Listen on port\n"
	    " -T, --tty-buffer-size=SIZE  Store at most SIZE bytes in console\n"
	    " -d, --data-directory        Where the cblockd data/spools/images are stored\n"
	    " -u, --ufs                   UFS as the underlying file system\n"
	    " -z, --zfs                   ZFS as the underlying file system\n"
	    " -N, --fuse-unionfs          FUSE unionfs as the underlying file system\n"
	    " -v, --verbose               Increase verbosity\n"
	    " -b, --background            Launch daemon into the background\n"
	    " -o, --sock-owner=USER       Allow user/groups to connect to socket\n"
	    " -l, --logfile=FILE          Path to cblock daemon log\n"
	    " -f, --create-forge=FILE     Create the base image to forge containers\n"
	);
	exit(1);
}

static int
create_forge(char *forge_path)
{
	char exec_path[MAXPATHLEN], buf[32];
	char **argv, **envp;
	vec_t *cmd, *env;
	int status;
	pid_t pid;

	(void) fprintf(stderr,
	    "-- Constructing the base forge image for future forging operations\n");
	pid = fork();
	if (pid == -1) {
		err(1, "fork failed");
	}
	if (pid != 0) {
		waitpid_ignore_intr(pid, &status);
		fprintf(stderr, "-- Forge creation returned %d\n",
		    WEXITSTATUS(status));
		return (status);
	}
	cmd = vec_init(16);
	vec_append(cmd, "/bin/sh");
	if (gcfg.c_verbose) {
		vec_append(cmd, "-x");
	}
	snprintf(exec_path, sizeof(exec_path), "%s/lib/create_forge.sh",
	    gcfg.c_data_dir);
	vec_append(cmd, exec_path);
	vec_append(cmd, gcfg.c_data_dir);
	vec_append(cmd, forge_path);
	vec_finalize(cmd);
	argv = vec_return(cmd);
	sprintf(buf, "CBLOCK_FS=%s", gcfg.c_underlying_fs);
	env = vec_init(8);
	vec_append(env, DEFAULT_PATH);
	vec_append(env, buf);
	vec_finalize(env);
	envp = vec_return(env);
	execve(*argv, argv, envp);
	err(1, "execve failed");
	return (-1);
}

static void
initialize_data_directory(int iszfs)
{
	char path[256], *dir, **dir_list;
	struct stat sb;
	int ret;

	ret = stat(gcfg.c_data_dir, &sb);
	if (ret == -1) {
		err(1, "ERROR: data directory: %s", gcfg.c_data_dir);
	}
	if ((sb.st_mode & S_IFDIR) == 0) {
		errx(1, "%s: is not a directory", gcfg.c_data_dir);
	}
	/*
	 * NB: we need a chck for ZFS. The scripts depend on these directories
	 * actually being file system roots.
	 */
	dir_list = data_sub_dirs;
	while ((dir = *dir_list++)) {
		if (strcmp(dir, "instances") == 0 ||
		    strcmp(dir, "images") == 0) {
			continue;
		}
		(void) snprintf(path, sizeof(path), "%s/%s",
		    gcfg.c_data_dir, dir);
		if (mkdir(path, 0755) == -1 && errno != EEXIST) {
			err(1, "mkdir failed");
		}
	}
}

static void
daemonize(struct global_params *gcp)
{
	pid_t pid;
	int fd;

	if (gcp->c_logfile) {
		fd = open(gcp->c_logfile, O_WRONLY | O_CREAT, 0700);
	} else {
		gcp->c_logfile = "/dev/null";
		fd = open("/dev/null", O_WRONLY);
	}
	if (fd == -1) {
		err(1, "failed to open log: %s", gcp->c_logfile);
	}
	pid = fork();
	if (pid == -1) {
		err(1, "fork failed");
	}
	if (pid == 0) {
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		return;
	}
	close(fd);
	(void) fprintf(stderr,
	    "cblockd: launched into the background: %d\n", pid);
	exit(0);
}

int
main(int argc, char *argv [], char *env[])
{
	int option_index, c, zfs_selected;
	pthread_t thr;
	char *r;

	gcfg.c_data_dir = DEFAULT_DATA_DIR;
	gcfg.global_env = env;
	gcfg.c_callback = cblock_handle_request;
	gcfg.c_family = PF_UNSPEC;
	gcfg.c_port = "3333";
	gcfg.c_tty_buf_size = 5 * 4096;
	gcfg.c_name = "/var/run/cblock.sock";
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "f:l:o:bd:T:46U:s:p:huzNv", long_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'f':
			gcfg.c_forge_path = optarg;
			break;
		case 'l':
			gcfg.c_logfile = optarg;
			break;
		case 'o':
			gcfg.c_sock_owner = optarg;
			break;
		case 'b':
			gcfg.c_background = 1;
			break;
		case 'd':
			gcfg.c_data_dir = optarg;
			break;
		case 'T':
			gcfg.c_tty_buf_size = strtoul(optarg, &r, 10);
			if (*r != '\0') {
				errx(1, "invalid TTY buf size: %s", optarg);
			}
		case '4':
			gcfg.c_family = PF_INET;
			break;
		case '6':
			gcfg.c_family = PF_INET6;
			break;
		case 'U':
			gcfg.c_name = optarg;
			break;
		case 's':
			gcfg.c_host = optarg;
			break;
		case 'p':
			gcfg.c_port = optarg;
			break;
		case 'u':
			zfs_selected = 0;
			gcfg.c_underlying_fs = "ufs";
			break;
		case 'z':
			zfs_selected = 1;
			gcfg.c_underlying_fs = "zfs";
			break;
		case 'N':
			gcfg.c_underlying_fs = "fuse-unionfs";
			break;
		case 'v':
			gcfg.c_verbose++;
			break;
		default:
			usage();
			/* NOT REACHED */
		}
	}
	if (gcfg.c_family != PF_UNSPEC && gcfg.c_name) {
		errx(1, "-4, -6 and --unix-sock are incompatable");
	}
	if (gcfg.c_underlying_fs == NULL) {
		errx(1, "must specify underlying file system:\n"
		    "    --ufs\n"
		    "    --fuse-unionfs\n"
                    "    --zfs");
	}
	fprintf(stdout, "%s\n", banner);
	fprintf(stdout, "version %s\n", "0.0.0");
	initialize_data_directory(zfs_selected);
	if (gcfg.c_forge_path != NULL) {
		return (create_forge(gcfg.c_forge_path));
	}
	if (gcfg.c_host || gcfg.c_port) {
		sock_ipc_setup_inet(&gcfg);
	} else {
		sock_ipc_setup_unix(&gcfg);
	}
	if (gcfg.c_background) {
		daemonize(&gcfg);
	}
	if (pthread_create(&thr, NULL, tty_io_queue_loop, NULL) == -1) {
		err(1, "pthread_create(tty_io_queue_loop)");
	}
	sock_ipc_event_loop(&gcfg);
	return (0);
}
