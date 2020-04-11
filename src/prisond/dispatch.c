#include <sys/types.h>
#include <sys/queue.h>
#include <sys/ioctl.h>
#include <sys/ttycom.h>

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <termios.h>
#include <libutil.h>
#include <string.h>

#include <libprison.h>

#include "main.h"
#include "dispatch.h"
#include "sock_ipc.h"

TAILQ_HEAD( , prison_peer) p_head;
TAILQ_HEAD( , prison_instance) pr_head;

pthread_mutex_t peer_mutex;
pthread_mutex_t prison_mutex;

static void
tty_set_noecho(int fd)
{
	struct termios term;

	if (tcgetattr(fd, &term) == -1) {
		err(1, "tcgetattr: failed");
	}
	term.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
	term.c_oflag &= ~(ONLCR);
	if (tcsetattr(fd, TCSANOW, &term) == -1) {
		err(1, "tcsetattr: failed");
	}
}

static int
prison_instance_is_unique(char *name)
{
	struct prison_instance *pi;

	pthread_mutex_lock(&prison_mutex);
	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (strcmp(pi->p_name, name) == 0) {
			pthread_mutex_unlock(&prison_mutex);
			return (0);
		}
	}
	pthread_mutex_unlock(&prison_mutex);
	return (1);
}

int
dispatch_launch_prison(int sock)
{
	struct prison_launch pl;
	struct prison_instance *pi;
	struct prison_response resp;
	ssize_t cc;

	cc = sock_ipc_must_read(sock, &pl, sizeof(pl));
	if (cc == 0) {
		return (0);
	}
	if (!prison_instance_is_unique(pl.p_name)) {
		resp.p_ecode = 1;
		sprintf(resp.p_errbuf, "prison already exists");
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	printf("launching prison %s\n", pl.p_name);
	pi = calloc(1, sizeof(*pi));
	if (pi == NULL) {
		err(1, "calloc failed");
	}
	strlcpy(pi->p_name, pl.p_name, sizeof(pi->p_name));
	pi->p_pid = forkpty(&pi->p_ttyfd, pi->p_ttyname, NULL, NULL);
	if (pi->p_pid == 0) {
		tty_set_noecho(STDIN_FILENO);
		char *argv[12];
		argv[0] = "/bin/tcsh";
		argv[1] = NULL;
		execve(*argv, argv, NULL);
		err(1, "execve failed");
	}
	printf("launched shell as pid %d\n", pi->p_pid);
	pthread_mutex_lock(&prison_mutex);
	TAILQ_INSERT_HEAD(&pr_head, pi, p_glue);
	pthread_mutex_unlock(&prison_mutex);
	resp.p_ecode = 0;
	resp.p_errbuf[0] = '\0';
	sock_ipc_must_write(sock, &resp, sizeof(resp));
	return (1);
}

void *
dispatch_work(void *arg)
{
	struct prison_peer *p;
	uint32_t cmd;
	ssize_t cc;
	int done;

	printf("debug: %s\n", __func__);
	p = (struct prison_peer *)arg;
	pthread_mutex_lock(&peer_mutex);
	TAILQ_INSERT_HEAD(&p_head, p, p_glue);
	pthread_mutex_unlock(&peer_mutex);
	done = 0;
	while (!done) {
		printf("waiting for command\n");
		cc = sock_ipc_may_read(p->p_sock, &cmd, sizeof(cmd));
		if (cc == 1) {
			break;
		}
		printf("read command %d\n", cmd);
		switch (cmd) {
		case PRISON_IPC_LAUNCH_PRISON:
			cc = dispatch_launch_prison(p->p_sock);
			if (cc == 0) {
				done = 1;
				continue;
			}
			break;
		}
	}
	printf("peer disconnected\n");
	close(p->p_sock);
	return (NULL);
}

void *
prison_handle_request(void *arg)
{
	struct prison_peer *p;

	printf("%s\n", __func__);
	p = (struct prison_peer *)arg;
	if (pthread_create(&p->p_thr, NULL, dispatch_work, arg) != 0) {
		err(1, "pthread_create(dispatch_work) failed");
	}
	return (NULL);
}
