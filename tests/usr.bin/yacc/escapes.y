/* ISO C escape sequences in character literals */

%{
int yylex(void);
void yyerror(const char *);
%}

%token TOK

%%

s : TOK '\n' s
  | TOK '\t' s
  | TOK '\\' s
  | TOK '\'' s
  | TOK '\x41' s
  | TOK '\101' s
  | TOK
  ;

%%
