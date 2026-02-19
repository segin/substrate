%{
int tests_passed = 0;
%}

%%

hello   { yymore(); }
world   { if (strcmp(yytext, "helloworld") == 0) tests_passed++; }

foobar  { yyless(3); if (strcmp(yytext, "foo") == 0) tests_passed++; }
bar     { tests_passed++; }

input   { 
          int c = input(); // expects 'X'
          if (c == 'X') tests_passed++;
        }

unput   {
          unput('Z');
          int c = input();
          if (c == 'Z') tests_passed++;
        }

.       { }

%%

int main() {
    yylex();
    printf("Tests passed: %d\n", tests_passed);
    return 0;
}
