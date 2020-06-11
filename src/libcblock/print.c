#include <stdio.h>
#include <stdarg.h>

void
print_red(FILE *fp, char *fmt, ...)
{
	char fmtbuf[1024];
	va_list ap;

	va_start(ap, fmt);
	(void) vsnprintf(fmtbuf, sizeof(fmtbuf), fmt, ap);
	va_end(ap);
	fprintf(fp, "\033[1;31m%s\033[0m", fmtbuf);
	fflush(fp);
}
 
void
print_bold_prefix(FILE *fp)
{
 
	fprintf(fp, "\033[1m--\033[0m ");
}  
