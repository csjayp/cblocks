#include <sys/types.h>
#include <sys/queue.h>
#include <sys/socket.h>
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
#include <string.h>

#include <libprison.h>

#include "termbuf.h"
#include "main.h"
#include "dispatch.h"
#include "sock_ipc.h"

TAILQ_HEAD( , prison_peer) p_head;
TAILQ_HEAD( , prison_instance) pr_head;

pthread_mutex_t peer_mutex;
pthread_mutex_t prison_mutex;

ssize_t tty_write(int fd, const void *vptr, size_t n)
{
        size_t left;
        ssize_t written;
        const char *ptr;
         
        ptr = vptr;
        left = n;
        while (left > 0) {
                if ((written = write(fd, ptr, left)) <= 0) {
                        return (written);
                }
                left -= written;
                ptr += written;
        }
        return (n);
}

void *
tty_io_queue_loop(void *arg)
{
	struct prison_instance *pi;
	struct timeval tv;
	fd_set rfds;
	int maxfd;
	int error;

	printf("tty_io_queue_loop: dispatched\n");
	while (1) {
		maxfd = 0;
		FD_ZERO(&rfds);
		pthread_mutex_lock(&prison_mutex);
		TAILQ_FOREACH(pi, &pr_head, p_glue) {
			switch (pi->p_state) {
			case INSTANCE_DEAD:
				continue;
			}
			if (pi->p_ttyfd > maxfd) {
				maxfd = pi->p_ttyfd;
			}
			FD_SET(pi->p_ttyfd, &rfds);
		}
		pthread_mutex_unlock(&prison_mutex);
		tv.tv_sec = 2;
		tv.tv_usec = 0;
		error = select(maxfd + 1, &rfds, NULL, NULL, &tv);
		if (error == -1 && errno == EINTR) {
			continue;
		}
		if (error == -1) {
			err(1, "select(tty io) failed");
		}
		if (error == 0) {
			printf("timeout...\n");
			continue;
		}
		pthread_mutex_lock(&prison_mutex);
		TAILQ_FOREACH(pi, &pr_head, p_glue) {
			if (!FD_ISSET(pi->p_ttyfd, &rfds)) {
				continue;
			}
			ssize_t cc;
			u_char buf[8192];
			bzero(buf, sizeof(buf));
			cc = read(pi->p_ttyfd, buf, sizeof(buf));
			if (cc == 0) {
				pi->p_state = INSTANCE_DEAD;
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
	struct termbuf *tbp;
	ssize_t cc;

	printf("console session established, flushing buffers...\n");
	pi->p_state = STATE_CONNECTED;
	TAILQ_FOREACH(tbp, &pi->p_ttybuf.t_head, t_glue) {
		switch (tbp->t_flag) {
		case TERMBUF_STATIC:
			cc = write(sock, tbp->t_static, tbp->t_len);
			if (cc == -1)
				err(1, "write(TERMBUF_STATIC) failed");
			break;
		case TERMBUF_DYNAMIC:
			cc = write(sock, tbp->t_dynamic, tbp->t_len);
			if (cc == -1)
				err(1, "write(TERMBUF_DYNAMIC)");
			break;
		}
	}
	for (;;) {
		char buf[1024], *vptr;
		uint32_t *cmd;

		bzero(buf, sizeof(buf));
		ssize_t cc = read(sock, buf, sizeof(buf));
		if (cc == 0) {
			break;
		}
		if (cc == -1 && errno == EINTR) {
			continue;
		}
		vptr = buf;
		cmd = (uint32_t *)vptr;
		switch (*cmd) {
		case PRISON_IPC_CONSOL_RESIZE:
			dispatch_handle_resize(pi->p_ttyfd, buf);
			break;
		case PRISON_IPC_CONSOLE_DATA:
			vptr += sizeof(uint32_t);
			cc -= sizeof(uint32_t);
			if (tty_write(pi->p_ttyfd, vptr, cc) != cc) {
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
	int match;

	sock_ipc_must_read(sock, &pcc, sizeof(pcc));
	printf("got console connect for container %s\n", pcc.p_name);
	match = 0;
	TAILQ_FOREACH(pi, &pr_head, p_glue) {
		if (strcmp(pi->p_name, pcc.p_name) != 0) {
			continue;
		}
		match = 1;
		break;
	}
	bzero(&resp, sizeof(resp));
	if (!match) {
		resp.p_ecode = 1;
		sprintf(resp.p_errbuf, "%s invalid container", pcc.p_name);
		sock_ipc_must_write(sock, &resp, sizeof(resp));
		return (1);
	} else {
		resp.p_ecode = 0;
		sock_ipc_must_write(sock, &resp, sizeof(resp));
	}
	if (tcsetattr(pi->p_ttyfd, TCSANOW, &pcc.p_termios) == -1) {
		err(1, "tcsetattr(TCSANOW) console connect");
	}
	printf("setting window size row=%u col=%u xpix=%u ypix=%u\n",
	    pcc.p_winsize.ws_row, pcc.p_winsize.ws_col, pcc.p_winsize.ws_xpixel, pcc.p_winsize.ws_ypixel);

	if (ioctl(pi->p_ttyfd, TIOCSWINSZ, &pcc.p_winsize) == -1) {
		err(1, "ioctl(TIOCSWINSZ): failed");
	}
	pi->p_peer_sock = sock;
	tty_console_session(sock, pi);
	/*
	 * NB: XXX: locking
	 */
	pi->p_state = 0; 	/* clear CONNECTED state */
	pi->p_peer_sock = -1;
	return (1);
}

int
dispatch_launch_prison(int sock)
{
	struct prison_launch pl;
	struct prison_instance *pi;
	struct prison_response resp;
	char *env[32], buf[128];
	ssize_t cc;
	extern struct global_params gcfg;

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
		sprintf(buf, "TERM=%s", pl.p_term);
		env[0] = strdup(buf);
		env[1] = NULL;
		execve(*argv, argv, gcfg.global_env);
		err(1, "execve failed");
	}
	TAILQ_INIT(&pi->p_ttybuf.t_head);
	pi->p_ttybuf.t_tot_len = 0;
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
	/*
	 * NB: clean up
	 */
	printf("peer disconnected\n");
	close(p->p_sock);
	pthread_mutex_lock(&peer_mutex);
	TAILQ_REMOVE(&p_head, p, p_glue);
	pthread_mutex_unlock(&peer_mutex);
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
