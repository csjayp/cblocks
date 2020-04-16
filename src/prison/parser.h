#ifndef PARSER_DOT_H_
#define PARSER_DOT_H_

extern int	 yyparse(void);
extern int      yylex(void);
void            yyerror(const char *);
char		*yyfile;
int			lineno;
extern FILE		*yyin;

#endif
