/* %prec rule precedence override: unary minus binds tighter than * */

%{
int yylex(void);
void yyerror(const char *);
%}

%token NUMBER
%left '+' '-'
%left '*' '/'
%right UMINUS

%%

expr
    : NUMBER
    | expr '+' expr
    | expr '-' expr
    | expr '*' expr
    | expr '/' expr
    | '-' expr  %prec UMINUS
    ;

%%
