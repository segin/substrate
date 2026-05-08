/* Error recovery using the special 'error' token and yyerrok */

%{
#include <stdio.h>
int yylex(void);
void yyerror(const char *);
%}

%token NUMBER NEWLINE

%%

input
    : /* empty */
    | input line
    ;

line
    : NEWLINE
    | NUMBER NEWLINE
    | error NEWLINE       { yyerrok; }
    ;

%%
