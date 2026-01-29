/* Mid-rule actions test - simplified */

%token NUMBER ID NEWLINE

%%

input
    : /* empty */
    | input line
    ;

line
    : NEWLINE
    | stmt NEWLINE
    ;

stmt
    : ID '=' expr
    | expr
    ;

expr
    : NUMBER
    | expr '+' expr
    | '(' expr ')'
    ;

%%
