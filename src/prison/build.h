#ifndef BUILD_DOT_H_
#define BUILD_DOT_H_

struct build_step_workdir {
	char		*sw_dir;
};

struct build_step_add {
	int		 sa_op;
#define	ADD_TYPE_FILE		1
#define	ADD_TYPE_ARCHIVE	2
#define	ADD_TYPE_URL		3
	char		*sa_source;
	char		*sa_dest;
};

struct build_step_copy {
	char		*sc_source;
	char		*sc_dest;
};

struct build_step {
	int			step_op;
#define	STEP_ADD	1
#define	STEP_COPY	2
#define	STEP_RUN	3
#define	STEP_WORKDIR	4
	TAILQ_ENTRY(build_step)	step_glue;
	union {
		char				*step_cmd;
		struct build_step_copy		 step_copy;
		struct build_step_add		 step_add;
		struct build_step_workdir	 step_workdir;
	} step_data;
};

struct build_stage {
	char				*bs_name;
	int				 bs_index;
	char				*bs_base_container;
	TAILQ_HEAD( , build_step)	step_head;
	TAILQ_ENTRY(build_stage)	stage_glue;
};

struct build_manifest {
	TAILQ_HEAD( , build_stage)	 stage_head;
	char				*entry_point;
	char				*maintainr;
};

char *archive_extensions[] = {
	"*.tar.gz",
	"*.tgz",
	"*.txz",
	"*.tar.xz",
	"*.tar.bz2",
	"*.tbz2",
	NULL
};

#endif	/* BUILD_DOT_H_ */
