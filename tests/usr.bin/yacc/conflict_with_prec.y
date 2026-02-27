%token NUM
%left '+'
%left '*'
%%
expr : expr '+' expr
     | expr '*' expr
     | NUM
     ;
%%
