#ifndef BUILD_DOT_H_
#define BUILD_DOT_H_

struct build_step {
	int			step_op;
#define	STEP_ADD	1
#define	STEP_COPY	2
#define	STEP_RUN	3
	TAILQ_ENTRY(build_step)	step_glue;
};

struct build_stage {
	char				*bs_name;
	char				*bs_base_container;
	TAILQ_HEAD( , build_step)	step_head;
	TAILQ_ENTRY(build_stage)	stage_glue;
};

struct build_manifest {
	TAILQ_HEAD( , build_stage)	 stage_head;
	char				*entry_point;
	char				*maintainr;
};

struct build_manifest			manifest;

#endif	/* BUILD_DOT_H_ */
