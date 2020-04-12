#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <sys/ttycom.h>

#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <termios.h>
#include <libutil.h>
#include <signal.h>
#include <string.h>

#include <libprison.h>

#include "termbuf.h"
#include "main.h"
#include "dispatch.h"
#include "sock_ipc.h"

TAILQ_HEAD( , prison_peer) p_head;
TAILQ_HEAD( , prison_instance) pr_head;

static int reap_children;

pthread_mutex_t peer_mutex;
pthread_mutex_t prison_mutex;

static void
handle_reap_children(int sig)
{

	reap_children = 1;
}

void
prison_remove(struct prison_instance *pi)
{
	size_t cur;

	printf("removing prison instance\n");
	(void) close(pi->p_peer_sock);
	(void) close(pi->p_ttyfd);
	TAILQ_REMOVE(&pr_head, pi, p_glue);
	cur = pi->p_ttybuf.t_tot_len;
	while (cur > 0) {
		cur = termbuf_remove_oldest(&pi->p_ttybuf);
	}
	free(pi);
}

void *
tty_io_queue_loop(void *arg)
{
	struct prison_instance *pi, *p_temp;
	int maxfd, error, status;
	struct timeval tv;
	u_char buf[8192];
	fd_set rfds;
	ssize_t cc;

	printf("tty_io_queue_loop: dispatched\n");
	while (1) {
		maxfd = 0;
		FD_ZERO(&rfds);
		pthread_mutex_lock(&prison_mutex);
		TAILQ_FOREACH_SAFE(pi, &pr_head, p_glue, p_temp) {
			if (reap_children) {
				if (waitpid(pi->p_pid, &status, WNOHANG) == pi->p_pid) {
					pi->p_state |= STATE_DEAD;
					printf("collected exit status from proc %d\n", pi->p_pid);
				}
			}
			if ((pi->p_state & STATE_DEAD) != 0) {
				if ((pi->p_state & STATE_CONNECTED) == 0) {
					prison_remove(pi);
				}
				continue;
			}
			if (pi->p_ttyfd > maxfd) {
				maxfd = pi->p_ttyfd;
			}
			FD_SET(pi->p_ttyfd, &rfds);
		}
		pthread_mutex_unlock(&prison_mutex);
		reap_children = 0;
		tv.tv_sec = 1;
		tv.tv_usec = 0;
		error = select(maxfd + 1, &rfds, NULL, NULL, &tv);
		if (error == -1 && errno == EINTR) {
			printf("select interrupted\n");
			continue;
		}
		if (error == -1) {
			err(1, "select(tty io) failed");
		}
		if (error == 0) {
			continue;
		}
		pthread_mutex_lock(&prison_mutex);
		TAILQ_FOREACH(pi, &pr_head, p_glue) {
			if (!FD_ISSET(pi->p_ttyfd, &rfds)) {
				continue;
			}
			cc = read(pi->p_ttyfd, buf, sizeof(buf));
			if (cc == 0) {
				pi->p_state |= STATE_DEAD;
				continue;
			}
			termbuf_append(&pi->p_ttybuf, buf, cc);
			printf("%s: queued %zu bytes for console: %zu\n",
			    pi->p_name, cc, pi->p_ttybuf.t_tot_len);
			if (pi->p_state != STATE_CONNECTED) {
				continue;
			}
			ssize_t dd = write(pi->p_peer_sock, buf, cc);
			if (dd == -1 && errno == EPIPE) {
				err(1, "handled disappearing consoles here");
			}
			printf("%zu bytes written to console\n", dd);
		}
		pthread_mutex_unlock(&prison_mutex);
	}
}

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

void
dispatch_handle_resize(int ttyfd, char *buf)
{
	struct winsize *wsize;
	char *vptr;

	vptr = buf;
	vptr += sizeof(uint32_t);
	wsize = (struct winsize *)vptr;
	if (ioctl(ttyfd, TIOCSWINSZ, wsize) == -1) {
		err(1, "ioctl(TIOCSWINSZ): failed");
	}
}

void
tty_console_session(int sock, struct prison_instance *pi)
{
	ssize_t bytes;
	int ttyfd;

	printf("tty_console_session: enter, reading commands from client\n");
	ttyfd = pi->p_ttyfd;
	for (;;) {
		char buf[1024], *vptr;
		uint32_t *cmd;

		bzero(buf, sizeof(buf));
		ssize_t cc = read(sock, buf, sizeof(buf));
		if (cc == 0) {
			printf("read EOF from socket\n");
			break;
		}
		if (cc == -1 && errno == EINTR) {
			continue;
		}
		if ((pi->p_state & STATE_DEAD) != 0) {
			printf("getting bytes for dead proc\n");
			break;
		}
		vptr = buf;
		cmd = (uint32_t *)vptr;
		switch (*cmd) {
		case PRISON_IPC_CONSOL_RESIZE:
			dispatch_handle_resize(ttyfd, buf);
			break;
		case PRISON_IPC_CONSOLE_DATA:
			vptr += sizeof(uint32_t);
			cc -= sizeof(uint32_t);
			bytes = write(ttyfd, vptr, cc);
			if (bytes != cc) {
				err(1, "tty_write failed");
			}
			break;
		default:
			errx(1, "unknown console instruction");
		}
	}
	printf("console dis-connected\n");
}

int
dispatch_connect_console(int sock)
{
	struct prison_console_connect pcc;
	struct prison_response resp;
	struct prison_instance *pi;
	ssize_t tty_buflen;
	char *tty_block;
	int match;

	bzero(&resp, sizeof(resp));
	sock_ipc_must_read(sock, &pcc, sizeof(pcc));
	printf("got console connect for container %s\n", pcc.p_name);
	match = 0;
	pthread_mutex_lock(&prison_mutex);
	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (strcmp(pi->p_name, pcc.p_name) != 0) {
			continue;
		}
		match = 1;
		break;
	}
	if (!match) {
		printf("invalid container\n");
		pthread_mutex_unlock(&prison_mutex);
		sprintf(resp.p_errbuf, "%s invalid container", pcc.p_name);
		resp.p_ecode = 1;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	}
	printf("console connected for %s\n", pcc.p_name);
	pi->p_state = STATE_CONNECTED;
	tty_block = termbuf_to_contig(&pi->p_ttybuf);
	tty_buflen = pi->p_ttybuf.t_tot_len;
	pthread_mutex_unlock(&prison_mutex);
        resp.p_ecode = 0;
        sock_ipc_must_write(sock, &resp, sizeof(resp));
	if (tty_block) {
		sock_ipc_must_write(sock, tty_block, tty_buflen);
		free(tty_block);
	}
	if (tcsetattr(pi->p_ttyfd, TCSANOW, &pcc.p_termios) == -1) {
		err(1, "tcsetattr(TCSANOW) console connect");
	}
	if (ioctl(pi->p_ttyfd, TIOCSWINSZ, &pcc.p_winsize) == -1) {
		err(1, "ioctl(TIOCSWINSZ): failed");
	}
	pi->p_peer_sock = sock;
	tty_console_session(sock, pi);
	if ((pi->p_state & STATE_DEAD) != 0) {
		prison_remove(pi);
		return (1);
	}
	pi->p_state &= ~STATE_CONNECTED;
	pi->p_peer_sock = -1;
	return (1);
}

int
dispatch_launch_prison(int sock)
{
	extern struct global_params gcfg;
	struct prison_response resp;
	struct prison_instance *pi;
	char *env[32], *argv[32];
	struct prison_launch pl;
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
		argv[0] = "/bin/tcsh";
		argv[1] = NULL;
		env[0] = strdup(pl.p_term);
		env[1] = NULL;
		execve(*argv, argv, env);
		err(1, "execve failed");
	}
	TAILQ_INIT(&pi->p_ttybuf.t_head);
	pi->p_ttybuf.t_tot_len = 0;
	pthread_mutex_lock(&prison_mutex);
	TAILQ_INSERT_HEAD(&pr_head, pi, p_glue);
	pthread_mutex_unlock(&prison_mutex);
	printf("launched shell as pid %d\n", pi->p_pid);
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

	signal(SIGCHLD, handle_reap_children);
	p = (struct prison_peer *)arg;
	done = 0;
	while (!done) {
		printf("waiting for command\n");
		cc = sock_ipc_may_read(p->p_sock, &cmd, sizeof(cmd));
		if (cc == 1) {
			break;
		}
		printf("read command %d\n", cmd);
		switch (cmd) {
		case PRISON_IPC_CONSOLE_CONNECT:
			cc = dispatch_connect_console(p->p_sock);
			done = 1;
			break;
		case PRISON_IPC_LAUNCH_PRISON:
			cc = dispatch_launch_prison(p->p_sock);
			if (cc == 0) {
				done = 1;
				continue;
			}
			break;
		default:
			/*
			 * NB: maybe best to send a response
			 */
			warnx("unknown command %u", cmd);
			close(p->p_sock);
			break;
		}
	}
	printf("peer disconnected\n");
	close(p->p_sock);
	pthread_mutex_lock(&peer_mutex);
	TAILQ_REMOVE(&p_head, p, p_glue);
	pthread_mutex_unlock(&peer_mutex);
	free(p);
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
	pthread_mutex_lock(&peer_mutex);
	TAILQ_INSERT_HEAD(&p_head, p, p_glue);
	pthread_mutex_unlock(&peer_mutex);
	return (NULL);
}
