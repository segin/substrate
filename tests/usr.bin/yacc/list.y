/* Nested list grammar - tests recursive structures */

%token NUMBER COMMA NEWLINE

%%

input
    : /* empty */
    | input line
    ;

line
    : NEWLINE
    | list NEWLINE
    ;

list
    : /* empty */
    | elements
    ;

elements
    : element
    | elements COMMA element
    ;

element
    : NUMBER
    | '[' list ']'
    ;

%%
