/* Mid-rule actions test with types */

%{
#include <stdio.h>
int yylex(void);
void yyerror(const char *s);
%}

%union {
    int ival;
    char *sval;
}

%token <ival> A B

%%

start: A { $<ival>$ = 100; printf("mid-rule executing\n"); } B
       {
         printf("end-rule. A=%d, mid=%d, B=%d\n", $<ival>1, $<ival>2, $<ival>3);
         if ($<ival>1 == 10 && $<ival>2 == 100 && $<ival>3 == 20) {
             printf("SUCCESS\n");
         } else {
             printf("FAILURE\n");
         }
       }
     ;

%%

int yylex(void) {
    static int count = 0;
    if (count == 0) { count++; yylval.ival = 10; return A; }
    if (count == 1) { count++; yylval.ival = 20; return B; }
    return 0;
}

void yyerror(const char *s) {
    printf("Error: %s\n", s);
}

int main(void) {
    yyparse();
    return 0;
}
