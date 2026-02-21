/* Grammar with semantic action for testing */

%token NUMBER

%%

expr
    : NUMBER        { $$ = $1; }
    ;

%%
