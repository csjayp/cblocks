#ifndef MAIN_DOT_H_
#define	MAIN_DOT_H_

#define	MAXSOCKS	64

struct global_params {
	char		*c_name;
	char		*c_host;
	char		*c_port;
	int		 c_family;
	char		*c_launch_name;
	char		*c_connect_console;
};

struct global_params gcfg;

#endif
