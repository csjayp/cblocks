#ifndef DISPATCH_DOT_H_
#define DISPATCH_DOT_H_

void		*prison_handle_request(void *);

struct prison_instance {
	uint32_t			p_state;
#define	STATE_DEAD		0x00000001
#define	STATE_CONNECTED		0x00000002
	char				p_name[256];
	pid_t				p_pid;
	int				p_ttyfd;
	char				p_ttyname[256];
	TAILQ_ENTRY(prison_instance)	p_glue;
	struct tty_buffer		p_ttybuf;
	int				p_peer_sock;
};

void *tty_io_queue_loop(void *);


#endif
