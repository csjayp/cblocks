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
	char		*c_term_type;
};

struct global_params gcfg;

void		reset_getopt_state(void);
int		console_main(int, char **, int);
int		launch_main(int, char **, int);
int		build_main(int, char **, int);


#endif
