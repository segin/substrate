#include "test_minimal.tab.h"

int yyparse(void);

int yylex(void) {
    static int once;
    if (once++) return 0;
    yylval = 7;
    return NUMBER;
}

void yyerror(const char *s) {
    (void)s;
}

int main(void) {
    return yyparse();
}
