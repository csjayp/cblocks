#ifndef	SOCK_IPC_DOT_H_
#define	SOCK_IPC_DOT_H_

int		sock_ipc_setup_unix(struct global_params *);
int		sock_ipc_setup_inet(struct global_params *);
int		sock_ipc_event_loop(struct global_params *);

struct prison_peer {
	int				p_sock;	/* socket associated with peer */
	uid_t				p_uid;	/* UID if available (PF_UNIX) */
	gid_t				p_gid;	/* GID if available (PF_UNIX) */
	int				p_family; /* address family */
	pthread_t			p_thr;
	TAILQ_ENTRY(prison_peer)	p_glue;
};

#endif	/* SOCK_IPC_DOT_H_ */
