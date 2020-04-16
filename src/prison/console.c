#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/un.h>
#include <sys/ttycom.h>
#include <netinet/in.h>

#include <stdio.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <getopt.h>
#include <stdlib.h>
#include <err.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#include <libprison.h>

#include "main.h"
#include "sock_ipc.h"

struct termios otermios;
int need_resize;

struct console_config {
	char		*c_name;
};

static struct option console_options[] = {
	{ "help",		no_argument, 0, 'h' },
	{ "name",		required_argument, 0, 'n' },
	{ 0, 0, 0, 0 }
};

static void
console_handle_window_resize(int sig)
{

	need_resize = 1;
}

static void
console_usage(void)
{
	(void) fprintf(stderr,
	    "Options\n"
	    " -h, --help        Display program usage\n"
	    " -n, --name        Name of console to connect to\n"
	);
	exit(1);
}

static void
console_tty_atexit(void)
{
	struct termios def;

	cfmakesane(&def);
	otermios.c_cflag = def.c_cflag | (otermios.c_cflag & CLOCAL);
	otermios.c_iflag = def.c_iflag;
	/* preserve user-preference flags in lflag */
#define LKEEP	(ECHOKE|ECHOE|ECHOK|ECHOPRT|ECHOCTL|ALTWERASE|TOSTOP|NOFLSH)
	otermios.c_lflag = def.c_lflag | (otermios.c_lflag & LKEEP);
	otermios.c_oflag = def.c_oflag;
	if (tcsetattr(STDIN_FILENO, TCSANOW, &otermios) == -1) {
		err(1, "tcsetattr(TCSANOW)");
	}
}

static int
console_tty_set_raw_mode(int fd)
{
	struct termios tbuf;

	/*
	 * We are committed to setting the TTY into raw raw mode, whatever
	 * happens make sure we restore the TTY state to a clean, and sane
	 * place to work.
	 */
	atexit(console_tty_atexit);
	if (tcgetattr(fd, &otermios) == -1) {
		return (-1);
	}
	tbuf = otermios;
	tbuf.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	tbuf.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	tbuf.c_cflag &= ~(CSIZE | PARENB);
	tbuf.c_cflag |= CS8;
	tbuf.c_oflag &= ~(OPOST);
	tbuf.c_cc[VMIN] = 1;
	tbuf.c_cc[VTIME] = 0;
	if (tcsetattr(fd, TCSAFLUSH, &tbuf) == -1) {
		return (-1);
	}
	return (0);
}

static void *
console_tty_handle_socket(void *arg)
{
	char buf[4096];
	ssize_t cc;
	int *sock;

	sock = (int *)arg;
	while (1) {
		cc = read(*sock, buf, sizeof(buf));
		if (cc == 0) {
			exit(1);
			break;
		}
		if (cc == -1 && errno == EINTR) {
			continue;
		}
		/*
		 * The socket was closed on our side. Probably a better
		 * way to do this, perhaps with a timeout.
		 */
		if (cc == -1 && errno == EBADF) {
			break;
		}
		if (cc == -1) {
			err(1, "read failed");
		}
		write(STDIN_FILENO, buf, cc);
	}
	printf("tty_handle_socket: exiting\n");
	return (NULL);
}

static void
console_tty_send_resize(int sock)
{
	struct winsize wsize;
	char *buf, *vptr;
	size_t len;
	uint32_t *cmd;

	len = sizeof(struct winsize) + sizeof(uint32_t) + 1;
	buf = calloc(1, len);
	if (buf == NULL) {
		err(1, "calloc failed");
	}
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &wsize) == -1) {
		err(1, "ioctl(TIOCGWINSZ): failed");
	}
	vptr = buf;
	cmd = (uint32_t *)vptr;
	*cmd = PRISON_IPC_CONSOL_RESIZE;
	vptr += sizeof(uint32_t);
	bcopy(&wsize, vptr, sizeof(wsize));
	if (write(sock, buf, len) != len) {
		err(1, "tty send resize failed");
	}
}

static void *
console_tty_handle_stdin(void *arg)
{
	char buf[4096], *vptr;
	ssize_t cc;
	int *sock;
	uint32_t *cmd;

	sock = (int *)arg;
	while (1) {
		vptr = buf;
		cmd = (uint32_t *)buf;
		*cmd = PRISON_IPC_CONSOLE_DATA;
		vptr += sizeof(uint32_t);
		cc = read(STDIN_FILENO, vptr, sizeof(buf) - sizeof(uint32_t));
		if (cc == 0) {
			break;
		}
		if (cc == -1 && errno == EINTR) {
			continue;
		}
		if (cc == -1) {
			err(1, "read failed");
		}
		/*
		 * If we get ^Q exit. This probably should be configurable.
		 */
		if (cc == 1 && *vptr == 0x11) {
			exit(0);
		}
		if (need_resize) {
			console_tty_send_resize(*sock);
			need_resize = 0;
		}
                if (write(*sock, buf, cc + sizeof(uint32_t)) == -1
		    && errno == EPIPE) {
			break;
		}
	}
	printf("console_tty_handle_stdin exiting\n");
        return (NULL);
}

static void
console_tty_console_session(int sock)
{
	pthread_t thr[2];
	int s;
	void *ptr;

	printf("listening on sock %d\n", sock);
	console_tty_set_raw_mode(STDIN_FILENO);
	signal(SIGWINCH, console_handle_window_resize);
	s = sock;
	if (pthread_create(&thr[0], NULL, console_tty_handle_socket, &s) == -1) {
		err(1, "pthread_create(socket)");
	}
	if (pthread_create(&thr[1], NULL, console_tty_handle_stdin, &s) == -1) {
		err(1, "pthread_create(stdin)");
	}
	if (pthread_join(thr[0], &ptr) == -1) {
		err(1, "pthread_join(tty master)");
	}
	if (pthread_join(thr[1], &ptr) == -1) {
		err(1, "pthrad_join(stdin)");
	}
}

static void
console_connect_console(int sock, struct console_config *ccp)
{
	struct prison_console_connect pcc;
	struct prison_response resp;
	uint32_t cmd;

	cmd = PRISON_IPC_CONSOLE_CONNECT;
	if (tcgetattr(STDIN_FILENO, &pcc.p_termios) == -1) {
		err(1, "tcgetattr(STDIN_FILENO) failed");
	}
	if (ioctl(STDIN_FILENO, TIOCGWINSZ, &pcc.p_winsize) == -1) {
		err(1, "ioctl(TIOCGWINSZ): failed");
	}
	strlcpy(pcc.p_name, ccp->c_name, sizeof(pcc.p_name));
	sock_ipc_must_write(sock, &cmd, sizeof(cmd));
	sock_ipc_must_write(sock, &pcc, sizeof(pcc));
	sock_ipc_must_read(sock, &resp, sizeof(resp));
	if (resp.p_ecode != 0) {
		(void) printf("failed to attach console to %s: %s\n",
		    ccp->c_name, resp.p_errbuf);
		return;
	}
	printf("got error code %d\n", resp.p_ecode);
	console_tty_set_raw_mode(STDIN_FILENO);
	console_tty_console_session(sock);
}

int
console_main(int argc, char *argv [], int cltlsock)
{
	struct console_config cc;
	int option_index, c;

	bzero(&cc, sizeof(cc));
	reset_getopt_state();
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "n:h", console_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 'h':
			console_usage();
			exit(1);
		case 'n':
			cc.c_name = optarg;
			break;
		}
	}
	signal(SIGPIPE, SIG_IGN);
	console_connect_console(cltlsock, &cc);
	return (0);
}
