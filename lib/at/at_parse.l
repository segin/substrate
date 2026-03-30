%{
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "at_parse_y.h"
%}

%option noyywrap nounput noinput

%%
"now"       { return NOW; }
"teatime"   { return TEATIME; }
"noon"      { return NOON; }
"midnight"  { return MIDNIGHT; }
"tomorrow"  { return TOMORROW; }
"+"         { return PLUS; }
"am"        { return AM; }
"pm"        { return PM; }
"minutes"|"minute" { return MINUTES; }
"hours"|"hour"     { return HOURS; }
"days"|"day"       { return DAYS; }
"weeks"|"week"     { return WEEKS; }
"months"|"month"   { return MONTHS; }
"years"|"year"     { return YEARS; }
"jan"|"january"    { yylval.ival = 0; return MONTH; }
"feb"|"february"   { yylval.ival = 1; return MONTH; }
"mar"|"march"      { yylval.ival = 2; return MONTH; }
"apr"|"april"      { yylval.ival = 3; return MONTH; }
"may"              { yylval.ival = 4; return MONTH; }
"jun"|"june"       { yylval.ival = 5; return MONTH; }
"jul"|"july"       { yylval.ival = 6; return MONTH; }
"aug"|"august"     { yylval.ival = 7; return MONTH; }
"sep"|"september"  { yylval.ival = 8; return MONTH; }
"oct"|"october"    { yylval.ival = 9; return MONTH; }
"nov"|"november"   { yylval.ival = 10; return MONTH; }
"dec"|"december"   { yylval.ival = 11; return MONTH; }

[0-9]{1,2}:[0-9]{2} {
    sscanf(yytext, "%d:%d", &yylval.time_val.hour, &yylval.time_val.min);
    return TIME;
}
[0-9]{4} {
    int val = atoi(yytext);
    yylval.time_val.hour = val / 100;
    yylval.time_val.min = val % 100;
    return TIME;
}

[0-9]+      { yylval.ival = atoi(yytext); return NUMBER; }
[ \t\n]+    { /* ignore whitespace */ }
.           { return -1; }
%%
