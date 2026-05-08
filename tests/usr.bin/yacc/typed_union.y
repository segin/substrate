/* %union with typed %token<tag> declarations and $$ access via tag */

%{
#include <stdio.h>
#include <stdlib.h>
int yylex(void);
void yyerror(const char *);
%}

%union {
    int  ival;
    char *sval;
}

%token <ival> INT
%token <sval> STR
%type  <ival> num
%type  <sval> str

%%

prog
    : num    { printf("num=%d\n", $1); }
    | str    { printf("str=%s\n", $1); }
    ;

num : INT      { $$ = $1; }
    | num INT  { $$ = $1 + $2; }
    ;

str : STR      { $$ = $1; }
    ;

%%
