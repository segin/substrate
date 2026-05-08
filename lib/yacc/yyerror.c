/*
 * yyerror.c - default yacc library yyerror()
 *
 * POSIX requires the -ly library to provide a yyerror() that writes its
 * argument followed by a newline to standard error.  The application can
 * override this by providing its own yyerror() definition before linking.
 */

#include <stdio.h>

void yyerror(const char *s) {
    if (s == NULL) s = "syntax error";
    fputs(s, stderr);
    fputc('\n', stderr);
}
