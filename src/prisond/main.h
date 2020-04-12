#ifndef MAIN_DOT_H_
#define	MAIN_DOT_H_

#define	MAXSOCKS	64

struct global_params {
	char		*c_name;
	char		*c_host;
	char		*c_port;
	int		 c_family;
	int		 c_socks[MAXSOCKS];
	size_t		 c_sock_count;
	void		*(*c_callback)(void *);
	char		**global_env;
	size_t		 c_tty_buf_size;
};

struct global_params gcfg;

#endif
