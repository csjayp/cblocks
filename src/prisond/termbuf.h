#ifndef TERMBUF_DOT_H_
#define TERMBUF_DOT_H_

struct termbuf {
	int			 t_flag;
#define	TERMBUF_STATIC		1
#define	TERMBUF_DYNAMIC		2
	u_char			 t_static[4192];
	u_char			*t_dynamic;
	size_t	 t_len;
	TAILQ_ENTRY(termbuf)	 t_glue;
};

typedef TAILQ_HEAD( , termbuf) termbuf_head_t;

struct tty_buffer {
	termbuf_head_t	t_head;
	size_t		t_tot_len;
};

char		*termbuf_to_contig(struct tty_buffer *);
size_t	 	 termbuf_remove_oldest(struct tty_buffer *);
void		 termbuf_append(struct tty_buffer *, u_char *, size_t);

#endif
