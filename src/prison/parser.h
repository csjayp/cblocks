#ifndef PARSER_DOT_H_
#define PARSER_DOT_H_

extern int	 yyparse(void);
extern int      yylex(void);
void            yyerror(const char *);
char		*yyfile;
int			lineno;
extern FILE		*yyin;


struct build_manifest 	*build_manifest_init(void);
void			 set_current_build_manifest(struct build_manifest *);
struct build_manifest	*get_current_build_manifest(void);

#endif
