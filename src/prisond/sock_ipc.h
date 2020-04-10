#ifdef	SOCK_IPC_DOT_H_
#define	SOCK_IPC_DOT_H_

int		sock_ipc_may_read(int, void *, size_t);
ssize_t		sock_ipc_must_read(int, void *, size_t);
ssize_t		sock_ipc_must_write(int, void *, size_t);
int		sock_ipc_setup_unix(struct global_params *);
int		sock_ipc_setup_inet(struct global_params *);

#endif	/* SOCK_IPC_DOT_H_ */
