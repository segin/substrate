%token NUM
%%
expr : expr '+' expr
     | expr '*' expr
     | NUM
     ;
%%
