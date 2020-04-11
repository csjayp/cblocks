#ifndef DISPATCH_DOT_H_
#define DISPATCH_DOT_H_

void		*prison_handle_request(void *);

struct prison_instance {
	int				p_state;
#define	INSTANCE_DEAD		1
#define	STATE_CONNECTED		2
	char				p_name[256];
	pid_t				p_pid;
	int				p_ttyfd;
	char				p_ttyname[256];
	TAILQ_ENTRY(prison_instance)	p_glue;
	struct tty_buffer		p_ttybuf;
};

void *tty_io_queue_loop(void *);


#endif
