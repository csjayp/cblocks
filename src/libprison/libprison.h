#ifndef LIBPRISON_DOT_H_
#define	LIBPRISON_DOT_H_

int		sock_ipc_may_read(int, void *, size_t);
ssize_t		sock_ipc_must_read(int, void *, size_t);
ssize_t		sock_ipc_must_write(int, void *, size_t);

#define	MAX_PRISON_NAME	512

#define	PRISON_IPC_LAUNCH_PRISON	1
#define	PRISON_IPC_CONSOLE_CONNECT	2
#define	PRISON_IPC_CONSOLE_DATA		3
#define PRISON_IPC_CONSOL_RESIZE	4

struct prison_response {
	int		p_ecode;
	char		p_errbuf[512];
};

struct prison_launch {
	char		p_name[MAX_PRISON_NAME];
	char		p_term[128];
};

struct prison_console_connect {
	char		p_name[MAX_PRISON_NAME];
	struct winsize 	p_winsize;
	struct termios	p_termios;
	char		p_term[64];
};

#endif
