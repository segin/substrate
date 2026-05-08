/* Character literal grammar:
 * Verifies that '+' '-' etc. get their character codes as token values
 * and don't get #define'd in the header file. */

%{
#include <stdio.h>
int yylex(void);
void yyerror(const char *);
%}

%token NUMBER
%left '+' '-'
%left '*' '/'

%%

expr
    : NUMBER                { $$ = $1; }
    | expr '+' expr         { $$ = $1 + $3; }
    | expr '-' expr         { $$ = $1 - $3; }
    | expr '*' expr         { $$ = $1 * $3; }
    | expr '/' expr         { $$ = $3 ? $1 / $3 : 0; }
    ;

%%
