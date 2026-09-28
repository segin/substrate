%{
/* This should NOT stop the block: %} */
int test = 1;
%}

%%
hello   printf("HI");
%%

int main() {
    yylex();
    return 0;
}
