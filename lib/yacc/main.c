/*
 * main.c - default yacc library entry point
 *
 * POSIX requires the -ly library to provide a main() that:
 *   - calls setlocale(LC_ALL, ""),
 *   - calls yyparse() and returns its value.
 * Applications that want a different startup behavior link without -ly
 * (or supply their own main() ahead of -ly).
 */

#include <locale.h>

extern int yyparse(void);

int main(void) {
    setlocale(LC_ALL, "");
    return yyparse();
}
