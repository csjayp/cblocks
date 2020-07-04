#ifndef CBLOCK_DOT_H_
#define CBLOCK_DOT_H_

int		cblock_create_pid_file(struct cblock_instance *);
size_t		cblock_instance_get_count(void);
struct instance_ent *
		cblock_populate_instance_entries(size_t);
int		cblock_instance_match(char *, const char *);
void		cblock_fork_cleanup(char *, char *, int, int);
void		cblock_remove(struct cblock_instance *);
void		cblock_detach_console(const char *);
void		cblock_reap_children(void);
int		cblock_instance_is_dead(const char *);
struct cblock_instance *
		cblock_lookup_instance(const char *);
void *		cblock_handle_request(void *);
void *		cblock_handle_request(void *);

cblock_peer_head_t p_head;
cblock_instance_head_t pr_head;

#endif
