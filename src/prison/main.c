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
#include "parser.h"

struct termios otermios;
int need_resize;

static struct option long_options[] = {
	{ "ipv4",		no_argument,	0, '4' },
	{ "ipv6",		no_argument,	0, '6' },
	{ "unix-sock",		required_argument, 0, 'U' },
	{ "host",		required_argument, 0, 's' },
	{ "port",		required_argument, 0, 'p' },
	{ "help",		no_argument, 0, 'h' },
	{ "launch",		required_argument, 0, 'r' },
	{ "console",		required_argument, 0, 'C' },
	{ "terminal-type",	required_argument, 0, 't' },
	{ 0, 0, 0, 0 }
};

void
manifest_load(char *path)
{
	FILE *f;

	f = fopen(path, "r");
	if (f == NULL) {
		err(1, "fopen manifest failed");
	}
	yyfile = path;
	yyin = f;
	yyparse();
	(void) fclose(f);
}

static void
handle_window_resize(int sig)
{

	need_resize = 1;
}

static void
usage(void)
{
	(void) fprintf(stderr,
	    "Options\n"
	    " -4, --ipv4                  IPv4 sockets only\n"
	    " -6, --ipv6                  IPv6 sockets only\n"
	    " -U, --unix-sock=PATH        Path to UNIX socket\n"
	    " -s, --host=HOST             Connect to host/address\n"
	    " -p, --port=PORT             Conect to port\n"
	    " -r, --launch=NAME           Launch prison by name\n"
	    " -C, --console=NAME          Attach to container console\n"
	    " -t, --terminal-type         TERM to use when creating containers\n"
	);
	exit(1);
}

void
tty_atexit(void)
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

int
tty_set_raw_mode(int fd)
{
	struct termios tbuf;

	/*
	 * We are committed to setting the TTY into raw raw mode, whatever
	 * happens make sure we restore the TTY state to a clean, and sane
	 * place to work.
	 */
	atexit(tty_atexit);
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

void *
tty_handle_socket(void *arg)
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

void
tty_send_resize(int sock)
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

void *
tty_handle_stdin(void *arg)
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
			tty_send_resize(*sock);
			need_resize = 0;
		}
                if (write(*sock, buf, cc + sizeof(uint32_t)) == -1
		    && errno == EPIPE) {
			break;
		}
	}
	printf("tty_handle_stdin exiting\n");
        return (NULL);
}

void
tty_console_session(int sock)
{
	pthread_t thr[2];
	int s;
	void *ptr;

	printf("listening on sock %d\n", sock);
	tty_set_raw_mode(STDIN_FILENO);
	signal(SIGWINCH, handle_window_resize);
	s = sock;
	if (pthread_create(&thr[0], NULL, tty_handle_socket, &s) == -1) {
		err(1, "pthread_create(socket)");
	}
	if (pthread_create(&thr[1], NULL, tty_handle_stdin, &s) == -1) {
		err(1, "pthread_create(stdin)");
	}
	if (pthread_join(thr[0], &ptr) == -1) {
		err(1, "pthread_join(tty master)");
	}
	if (pthread_join(thr[1], &ptr) == -1) {
		err(1, "pthrad_join(stdin)");
	}
}

void
prison_connect_console(int sock, char *name)
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
	strlcpy(pcc.p_name, name, sizeof(pcc.p_name));
	sock_ipc_must_write(sock, &cmd, sizeof(cmd));
	sock_ipc_must_write(sock, &pcc, sizeof(pcc));
	sock_ipc_must_read(sock, &resp, sizeof(resp));
	if (resp.p_ecode != 0) {
		printf("failed to attach console to %s: %s\n", name, resp.p_errbuf);
		return;
	}
	printf("got error code %d\n", resp.p_ecode);
	tty_set_raw_mode(STDIN_FILENO);
	tty_console_session(sock);
}

void
prison_launch(int sock, char *name)
{
	struct prison_launch pl;
	struct prison_response resp;
	uint32_t cmd;
	char *term;

	if (gcfg.c_term_type != NULL) {
		term = gcfg.c_term_type;
	} else {
		term = getenv("TERM");
	}
	cmd = PRISON_IPC_LAUNCH_PRISON;
	sock_ipc_must_write(sock, &cmd, sizeof(cmd));
	strlcpy(pl.p_name, name, sizeof(pl.p_name));
	strlcpy(pl.p_term, term, sizeof(pl.p_term));
	sock_ipc_must_write(sock, &pl, sizeof(pl));
	sock_ipc_must_read(sock, &resp, sizeof(resp));
	printf("got error code %d\n", resp.p_ecode);
}

int
main(int argc, char *argv [])
{
	int option_index;
	int c, ctlsock;

	gcfg.c_family = PF_UNSPEC;
	gcfg.c_port = "3333";
	while (1) {
		option_index = 0;
		c = getopt_long(argc, argv, "t:C:r:46U:s:p:h", long_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
		case 't':
			gcfg.c_term_type = optarg;
			break;
		case 'C':
			gcfg.c_connect_console = optarg;
			break;
		case 'r':
			gcfg.c_launch_name = optarg;
			break;
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
		default:
			usage();
			/* NOT REACHED */
		}
	}
	manifest_load("Prisonfile");
	signal(SIGPIPE, SIG_IGN);
	if (gcfg.c_family != PF_UNSPEC && gcfg.c_name) {
		errx(1, "-4, -6 and --unix-sock are incompatable");
	}
	if (gcfg.c_connect_console && gcfg.c_term_type) {
		errx(1, "term type is only for container creation");
	}
	if (gcfg.c_name) {
		ctlsock = sock_ipc_connect_unix(&gcfg);
	} else {
		ctlsock = sock_ipc_connect_inet(&gcfg);
	}
	if (gcfg.c_launch_name) {
		prison_launch(ctlsock, gcfg.c_launch_name);
	}
	if (gcfg.c_connect_console) {
		prison_connect_console(ctlsock, gcfg.c_connect_console);
	}
	return (0);
}
