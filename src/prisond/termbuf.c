#include <sys/types.h>
#include <sys/queue.h>

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <err.h>

#include "main.h"
#include "termbuf.h"

#ifdef __TEST_TERMBUF_CODE__
termbuf_head_t t_head;
size_t term_buf_tot_len;
struct global_params gcfg;
#endif

char *
termbuf_to_contig(struct tty_buffer *ttyb)
{
	struct termbuf *tbp;
	char *buf, *vptr;

	if (TAILQ_EMPTY(&ttyb->t_head)) {
		return (NULL);
	}
	buf = calloc(1, ttyb->t_tot_len + 1);
	if (buf == NULL) {
		err(1, "calloc(termbuf_to_contig) failed)");
	}
	vptr = buf;
	TAILQ_FOREACH(tbp, &ttyb->t_head, t_glue) {
		switch (tbp->t_flag) {
		case TERMBUF_DYNAMIC:
			bcopy(tbp->t_dynamic, vptr, tbp->t_len);
			break;
		case TERMBUF_STATIC:
			bcopy(tbp->t_static, vptr, tbp->t_len);
			break;
		}
		vptr += tbp->t_len;
	}
	return (buf);
}

size_t
termbuf_remove_oldest(struct tty_buffer *ttyb)
{
	struct termbuf *tbp;

	assert(!TAILQ_EMPTY(&ttyb->t_head));
	tbp = TAILQ_FIRST(&ttyb->t_head);
	TAILQ_REMOVE(&ttyb->t_head, tbp, t_glue);
	if (tbp->t_flag == TERMBUF_DYNAMIC) {
		free(tbp->t_dynamic);
	}
	ttyb->t_tot_len -= tbp->t_len;
	free(tbp);
	return (ttyb->t_tot_len);
}

void
termbuf_append(struct tty_buffer *ttyb, u_char *bytes, size_t len)
{
	extern struct global_params gcfg;
	struct termbuf *tbp;
	size_t cur;

	assert(bytes != NULL);
	assert(len != 0);
	tbp = calloc(1, sizeof(*tbp));
	if (tbp == NULL) {
		err(1, "calloc(termbuf) failed");
	}
	tbp->t_len = len;
	if (len >= sizeof(tbp->t_static)) {
		tbp->t_dynamic = calloc(1, sizeof(len));
		if (tbp->t_dynamic == NULL) {
			err(1, "calloc(t_dynamic) failed");
		}
		bcopy(bytes, tbp->t_dynamic, tbp->t_len);
		tbp->t_flag = TERMBUF_DYNAMIC;
	} else {
		tbp->t_flag = TERMBUF_STATIC;
		bcopy(bytes, tbp->t_static, tbp->t_len);
	}
	ttyb->t_tot_len += tbp->t_len;
	TAILQ_INSERT_TAIL(&ttyb->t_head, tbp, t_glue);
	if (ttyb->t_tot_len > gcfg.c_tty_buf_size) {
		cur = ttyb->t_tot_len;
		while (cur >= gcfg.c_tty_buf_size) {
			cur = termbuf_remove_oldest(ttyb);
		}
	}
}


#ifdef __TEST_TERMBUF_CODE__
static void
termbuf_print_buf(struct termbuf *tbp)
{
	u_char *p;

	switch (tbp->t_flag) {
	case TERMBUF_STATIC:
		p = tbp->t_static;
		break;
	case TERMBUF_DYNAMIC:
		p = tbp->t_dynamic;
		break;
	}
	printf("%s\n", (char *)p);
}


static void
termbuf_print_queue(termbuf_head_t *head)
{
	struct termbuf *tbp;

	TAILQ_FOREACH(tbp, head, t_glue) {
		termbuf_print_buf(tbp);
	}
}

int
main(int argc, char *argv [])
{
	struct tty_buffer ttyb;
	char *p;

	gcfg.c_tty_buf_size = 1024;
	TAILQ_INIT(&ttyb.t_head);
	p = "test 1";
	termbuf_append(&ttyb, (u_char *)p, strlen(p));
	p = "test 2";
	termbuf_append(&ttyb, (u_char *)p, strlen(p));
	p = "test 3";
	termbuf_append(&ttyb, (u_char *)p, strlen(p));
	termbuf_print_queue(&ttyb.t_head);
	printf("removing oldest\n");
	termbuf_remove_oldest(&ttyb);
	termbuf_print_queue(&ttyb.t_head);
	printf("adding another\n");
	p = "whakawkwa";
	termbuf_append(&ttyb, (u_char *)p, strlen(p));
	termbuf_print_queue(&ttyb.t_head);
	printf("---\n");
	printf("%s\n", termbuf_to_contig(&ttyb));
	return (0);
}
#endif	/* __TEST_TERMBUF_CODE__ */
