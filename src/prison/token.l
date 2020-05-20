%{
/*-
 * Copyright (c) 2020 Christian S.J. Peron
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <assert.h>
#include <stdio.h>
#include <err.h>

#include "y.tab.h"
#include "parser.h"

#define YY_NO_UNPUT
int line;

%}

%option nounput
%option caseless

tokenchar       [^[:blank:];,<>"\n]
tokenstring     {tokenchar}+
integer         [0-9]+

%%
\#.*		/* ignore comments */
FROM		return (FROM);
\-\-FROM	return (COPY_FROM);
AS		return (AS);
RUN		return (RUN);
ADD		return (ADD);
COPY		return (COPY);
WORKDIR		return (WORKDIR);
ENTRYPOINT	return (ENTRYPOINT);
ROOTPIVOT	return (ROOTPIVOT);
CMD		return (CMD);
{integer}       {
                        yylval.num = atoi(yytext);
                        return (INTEGER);
                }
\n              line++;
[ \t]+          /* whitespace ignored */
\[		return (OPEN_SQUARE_BRACKET);
,		return (COMMA);
\]		return (CLOSE_SQUARE_BRACKET);
\"              {
                        char buf[8192], *ptr, c;
                        int escaped;

			escaped = 0;
                        memset(buf, 0, sizeof(buf));
                        for (ptr = buf; strlen(buf) < (sizeof(buf) - 1);) {
                                c = input();
                                if (escaped && c != EOF) {
                                        *ptr = c;
                                        escaped = 0;
                                        continue;
                                }
                                switch (c) {
                                case '\\':
                                        ptr--;
                                        escaped = 1;
                                        break;
                                case '\n':
                                        /* newlines encountered within
                                         * quoted strings are converted
                                         * into a single space. */
                                        *ptr = ' ';
					line++;
                                        break;
                                case '"':       /* end quote */
                                        ptr = NULL;
                                        break;
                                case EOF:
                                        printf("unmatched \"");
                                        exit(1);
                                default:
                                        *ptr = c;
                                        break;
                                }
                                if (ptr == NULL) {
                                        break;
				}
				ptr++;
                        }
                        yylval.c_string = strdup(buf);
                        assert(yylval.c_string != NULL);
                        return (STRING);
                }
{tokenstring}   {
                        yylval.c_string = strdup(yytext);
                        assert(yylval.c_string != NULL);
                        return (STRING);
                }
%%

void
yyerror(const char *str)
{

	errx(1, "syntax error near %s:%d: '%s' (%s)", yyfile, yylineno, yytext, str);
}

int
yywrap(void)
{

	return (1);
}
