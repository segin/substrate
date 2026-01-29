/* Calculator with variables - tests symbol table and complex actions */

%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int yylex(void);
void yyerror(const char *s);

#define MAX_VARS 26
int variables[MAX_VARS];
%}

%union {
    int val;
    char var;
}

%token <val> NUMBER
%token <var> VARIABLE
%token EQUALS NEWLINE
%left '+' '-'
%left '*' '/'
%right UMINUS

%type <val> expr

%%

input
    : /* empty */
    | input line
    ;

line
    : NEWLINE
    | expr NEWLINE          { printf("= %d\n", $1); }
    | VARIABLE EQUALS expr NEWLINE  { variables[$1 - 'a'] = $3; printf("%c = %d\n", $1, $3); }
    | error NEWLINE         { yyerrok; }
    ;

expr
    : NUMBER                { $$ = $1; }
    | VARIABLE              { $$ = variables[$1 - 'a']; }
    | expr '+' expr         { $$ = $1 + $3; }
    | expr '-' expr         { $$ = $1 - $3; }
    | expr '*' expr         { $$ = $1 * $3; }
    | expr '/' expr         { $$ = ($3 == 0) ? 0 : $1 / $3; }
    | '-' expr %prec UMINUS { $$ = -$2; }
    | '(' expr ')'          { $$ = $2; }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Error: %s\n", s);
}

int main(void) {
    int i;
    for (i = 0; i < MAX_VARS; i++) {
        variables[i] = 0;
    }
    return yyparse();
}
