#ifndef DISPATCH_DOT_H_
#define DISPATCH_DOT_H_

void		*prison_handle_request(void *);

struct prison_instance {
	char				p_name[256];
	pid_t				p_pid;
	int				p_ttyfd;
	char				p_ttyname[256];
	TAILQ_ENTRY(prison_instance)	p_glue;
};

#endif
