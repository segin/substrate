/* Tokens with explicit numbers via %token NAME N */

%{
int yylex(void);
void yyerror(const char *);
%}

%token APPLE  300
%token BANANA 301
%token CHERRY 305
%token DATE   /* unnumbered: gets 257 */

%%

s : APPLE | BANANA | CHERRY | DATE ;

%%
