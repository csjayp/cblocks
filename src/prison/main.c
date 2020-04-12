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

static struct option long_options[] = {
	{ "ipv4",		no_argument,	0, '4' },
	{ "ipv6",		no_argument,	0, '6' },
	{ "unix-sock",		required_argument, 0, 'U' },
	{ "host",		required_argument, 0, 's' },
	{ "port",		required_argument, 0, 'p' },
	{ "help",		no_argument, 0, 'h' },
	{ "launch",		required_argument, 0, 'r' },
	{ "console",		required_argument, 0, 'C' },
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
	    " -s, --host=HOST             Connect to host/address\n"
	    " -p, --port=PORT             Conect to port\n"
	    " -r, --launch=NAME           Launch prison by name\n"
	    " -C, --console=NAME          Attach to container console\n"
	);
	exit(1);
}

void
tty_atexit(void)
{
	if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &otermios) == -1) {
		err(1, "tcsetattr(TCSAFLUSH)");
	}
}

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

int
tty_set_raw_mode(int fd)
{
	struct termios tbuf;

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

void
tty_console_session(int sock)
{
	fd_set rfds;
	int error, maxfd;
	char buf[8192];
	ssize_t cc;
	FILE *fp;

	fp = fopen("/tmp/diags", "w+");
	if (fp == NULL) {
		err(1, "fopen failed");
	}
	fprintf(fp, "booyaka\n");
	fflush(fp);
	while (1) {
		maxfd = 0;
		FD_ZERO(&rfds);
		if (sock > maxfd) {
			maxfd = sock;
		}
		FD_SET(sock, &rfds);
		if (STDIN_FILENO > maxfd) {
			maxfd = STDIN_FILENO;
		}
		FD_SET(STDIN_FILENO, &rfds);
		error = select(maxfd + 1, &rfds, NULL, NULL, NULL);
		if (error == -1 && errno == EINTR) {
			fprintf(fp, "select failed with EINTR\n");
			continue;
		}
		fprintf(fp, "select returned\n");
		fflush(fp);
		if (FD_SET(sock, &rfds)) {
			cc = read(sock, buf, sizeof(buf));
			if (cc == 0) {
				break;
			}
			if (cc == -1 && errno == EINTR) {
				continue;
			}
			if (cc == -1) {
				err(1, "read(from console sock) failed");
			}
			fprintf(fp, "wrote some data: %zu bytes\n", cc);
			fwrite(buf, cc, 1, fp);
			fflush(fp);
			if (tty_write(STDOUT_FILENO, buf, cc) != cc) {
				err(1, "tty_write failed");
			}
		}
		if (FD_SET(STDIN_FILENO, &rfds)) {
			cc = read(STDIN_FILENO, buf, sizeof(buf));
			if (cc == 0) {
				break;
			}
			if (cc == -1 && errno == EINTR) {
				continue;
			}
			if (cc == -1) {
				err(1, "read(from console sock) failed");
			}
			if (tty_write(sock, buf, cc) != cc) {
				err(1, "tty write");
			}
		}
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

	cmd = PRISON_IPC_LAUNCH_PRISON;
	sock_ipc_must_write(sock, &cmd, sizeof(cmd));
	strlcpy(pl.p_name, name, sizeof(pl.p_name));
	strlcpy(pl.p_term, getenv("TERM"), sizeof(pl.p_term));
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
		c = getopt_long(argc, argv, "C:r:46U:s:p:h", long_options,
		    &option_index);
		if (c == -1) {
			break;
		}
		switch (c) {
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
	signal(SIGPIPE, SIG_IGN);
	if (gcfg.c_family != PF_UNSPEC && gcfg.c_name) {
		errx(1, "-4, -6 and --unix-sock are incompatable");
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
