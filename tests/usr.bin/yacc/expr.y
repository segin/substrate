/* Simple expression grammar for testing yacc */

%{
#include <stdio.h>
#include <stdlib.h>

int yylex(void);
void yyerror(const char *s);
%}

%token NUMBER
%token PLUS MINUS TIMES DIVIDE
%token LPAREN RPAREN
%token NEWLINE

%left PLUS MINUS
%left TIMES DIVIDE

%%

input
    : /* empty */
    | input line
    ;

line
    : NEWLINE
    | expr NEWLINE  { printf("= %d\n", $1); }
    ;

expr
    : NUMBER                { $$ = $1; }
    | expr PLUS expr        { $$ = $1 + $3; }
    | expr MINUS expr       { $$ = $1 - $3; }
    | expr TIMES expr       { $$ = $1 * $3; }
    | expr DIVIDE expr      { $$ = $1 / $3; }
    | LPAREN expr RPAREN    { $$ = $2; }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Error: %s\n", s);
}

int main(void) {
    return yyparse();
}
