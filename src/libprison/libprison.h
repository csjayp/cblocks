#ifndef LIBPRISON_DOT_H_
#define	LIBPRISON_DOT_H_

int		sock_ipc_may_read(int, void *, size_t);
ssize_t		sock_ipc_must_read(int, void *, size_t);
ssize_t		sock_ipc_must_write(int, void *, size_t);

#define	MAX_PRISON_NAME	512

#define	PRISON_IPC_LAUNCH_PRISON	1
#define	PRISON_IPC_CONSOLE_CONNECT	2

struct prison_response {
	int		p_ecode;
	char		p_errbuf[512];
};

struct prison_launch {
	char		p_name[MAX_PRISON_NAME];
};

struct prison_console_connect {
	char		p_name[MAX_PRISON_NAME];
	struct winsize 	p_winsize;
	struct termios	p_termios;
};

#endif
