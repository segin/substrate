/* Debug mode */
#ifndef YYDEBUG
#define YYDEBUG 0
#endif
#if YYDEBUG
int yydebug = 0;
#endif

#line 1 "at_parse.y"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int yylex(void);
void yyerror(const char *s);

extern time_t parsed_time;
extern time_t base_time;


/* Token definitions */
#define error 256
#define NOW 257
#define TEATIME 258
#define NOON 259
#define MIDNIGHT 260
#define TOMORROW 261
#define PLUS 262
#define AM 263
#define PM 264
#define MINUTES 265
#define HOURS 266
#define DAYS 267
#define WEEKS 268
#define MONTHS 269
#define YEARS 270
#define MONTH 271
#define NUMBER 272
#define TIME 273

/* Semantic value type */
#ifndef YYSTYPE
typedef union {

    int ival;
    struct {
        int hour;
        int min;
    } time_val;

} YYSTYPE;
#endif

YYSTYPE yylval;

/* Rule lengths (number of RHS symbols) */
static const short yylen[] = {
  1,
  2,
  1,
  1,
  1,
  1,
  1,
  1,
  3,
  1,
  2,
  2,
  2,
  3,
  4,
  4,
  2,
  1,
  1,
  1,
  1,
};

/* Left-hand side symbols */
static const short yylhs[] = {
  -274,
  4,
  2,
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  3,
  1,
  0,
  0,
  0,
  0,
};

/* Default reductions (0 = none) */
static const short yydefred[] = {
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  2,
  0,
  0,
  0,
  0,
  0,
  1,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
};

/* Shift index table */
static const short yysindex[] = {
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  3,
  0,
  0,
  3,
  0,
  5,
  6,
  6,
  0,
  14,
  0,
  11,
  12,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
};

/* Reduce index table */
static const short yyrindex[] = {
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
};

/* Goto index table */
static const short yygindex[] = {
  0,
  0,
  1,
  1,
  0,
};

/* Default GOTO destinations */
static const short yydgoto[] = {
  25,
  17,
  8,
  9,
  0,
};

/* Compressed parser table */
static const short yytable[] = {
  15,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  1,
  2,
  3,
  4,
  5,
  10,
  0,
  0,
  0,
  12,
  13,
  0,
  0,
  0,
  6,
  11,
  7,
  14,
  16,
  18,
  19,
  20,
  21,
  22,
  23,
  24,
  26,
  27,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
};

/* Table verification check */
static const short yycheck[] = {
  0,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  257,
  258,
  259,
  260,
  261,
  262,
  -1,
  -1,
  -1,
  263,
  264,
  -1,
  -1,
  -1,
  271,
  272,
  273,
  271,
  272,
  271,
  271,
  272,
  265,
  266,
  267,
  268,
  272,
  272,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
  -1,
};


/* Parser constants */
#ifndef YYMAXDEPTH
#define YYMAXDEPTH 500
#endif
#ifndef YYERRCODE
#define YYERRCODE 256
#endif

/* Standard yacc macros */
#define yyerrok   (yyerrflag = 0)
#define yyclearin (yychar = -1)
#define YYABORT   return 1
#define YYACCEPT  return 0
#define YYERROR   do { yyerrflag = 1; goto yyloop; } while (0)

/* External declarations */
extern int yylex(void);
extern void yyerror(const char *);
extern YYSTYPE yylval;

int yynerrs = 0;
int yyerrflag = 0;
int yychar = -1;

int yyparse(void) {
    int yyn;
    int yystate = 0;
    YYSTYPE yyval;

    /* State and value stacks */
    short yyss[YYMAXDEPTH];
    YYSTYPE yyvs[YYMAXDEPTH];
    short *yyssp = yyss;
    YYSTYPE *yyvsp = yyvs;

    *yyssp = 0; /* Push initial state */
    yychar = -1;
    yynerrs = 0;
    yyerrflag = 0;

yyloop:
    /* Check for stack overflow */
    if (yyssp >= yyss + YYMAXDEPTH - 1) {
        yyerror("yacc stack overflow");
        return 1;
    }

    /* Check for default reduction */
    yyn = yydefred[yystate];
    if (yyn != 0) goto yyreduce;

    /* Get lookahead token if needed */
    if (yychar < 0) {
        yychar = yylex();
        if (yychar < 0) yychar = 0; /* EOF */
    }

    /* Shift action check */
    yyn = yysindex[yystate];
    if (yyn != 0 && (yyn += yychar) >= 0 && yyn < 458 && yycheck[yyn] == yychar) {
        yyn = yytable[yyn];
        goto yyshift;
    }

    /* Reduce action check */
    yyn = yyrindex[yystate];
    if (yyn != 0 && (yyn += yychar) >= 0 && yyn < 458 && yycheck[yyn] == yychar) {
        yyn = -yytable[yyn];
        goto yyreduce;
    }

    /* Check for accept: state with shift on $end (token 0) */
    if (yychar == 0 && yystate == 8) return 0;

    /* Error handling */
    if (yyerrflag == 0) {
        yyerror("syntax error");
        yynerrs++;
    }
    yyerrflag = 1;
    goto yyloop;

yyshift:
    /* Shift: push new state */
    yystate = yyn;
    *++yyssp = yystate;
    *++yyvsp = yylval;
    yychar = -1; /* Consumed token */
    if (yyerrflag > 0) yyerrflag--;
    goto yyloop;

yyreduce:
    /* Reduce by rule yyn */
    yyssp -= yylen[yyn];
    yyvsp -= yylen[yyn];
    yyval = yyvsp[1]; /* Default $$ = $1 */

    /* Execute semantic action */
    switch (yyn) {
case 3:
{ parsed_time = base_time; }
break;
case 4:
{
        struct tm *info = localtime(&base_time);
        info->tm_hour = 16;
        info->tm_min = 0;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
      }
break;
case 5:
{
        struct tm *info = localtime(&base_time);
        info->tm_hour = 12;
        info->tm_min = 0;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
      }
break;
case 6:
{
        struct tm *info = localtime(&base_time);
        info->tm_hour = 0;
        info->tm_min = 0;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
      }
break;
case 7:
{
        struct tm *info = localtime(&base_time);
        info->tm_mday++;
        parsed_time = mktime(info);
      }
break;
case 8:
{ parsed_time = base_time + yyvsp[3].ival; }
break;
case 9:
{
        struct tm *info = localtime(&base_time);
        info->tm_hour = yyvsp[1].time_val.hour;
        info->tm_min = yyvsp[1].time_val.min;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
    }
break;
case 10:
{
        struct tm *info = localtime(&base_time);
        info->tm_hour = (yyvsp[1].time_val.hour == 12) ? 0 : yyvsp[1].time_val.hour;
        info->tm_min = yyvsp[1].time_val.min;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
    }
break;
case 11:
{
        struct tm *info = localtime(&base_time);
        info->tm_hour = (yyvsp[1].time_val.hour == 12) ? 12 : yyvsp[1].time_val.hour + 12;
        info->tm_min = yyvsp[1].time_val.min;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_mday++;
            parsed_time = mktime(info);
        }
    }
break;
case 12:
{
        struct tm *info = localtime(&base_time);
        info->tm_mon = yyvsp[1].ival;
        info->tm_mday = yyvsp[2].ival;
        info->tm_hour = 0;
        info->tm_min = 0;
        info->tm_sec = 0;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_year++;
            parsed_time = mktime(info);
        }
    }
break;
case 13:
{
        struct tm *info = localtime(&base_time);
        info->tm_hour = yyvsp[1].time_val.hour;
        info->tm_min = yyvsp[1].time_val.min;
        info->tm_sec = 0;
        info->tm_mon = yyvsp[2].ival;
        info->tm_mday = yyvsp[3].ival;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_year++;
            parsed_time = mktime(info);
        }
    }
break;
case 14:
{
        struct tm *info = localtime(&base_time);
        info->tm_hour = (yyvsp[1].time_val.hour == 12) ? 0 : yyvsp[1].time_val.hour;
        info->tm_min = yyvsp[1].time_val.min;
        info->tm_sec = 0;
        info->tm_mon = yyvsp[3].ival;
        info->tm_mday = yyvsp[4].ival;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_year++;
            parsed_time = mktime(info);
        }
    }
break;
case 15:
{
        struct tm *info = localtime(&base_time);
        info->tm_hour = (yyvsp[1].time_val.hour == 12) ? 12 : yyvsp[1].time_val.hour + 12;
        info->tm_min = yyvsp[1].time_val.min;
        info->tm_sec = 0;
        info->tm_mon = yyvsp[3].ival;
        info->tm_mday = yyvsp[4].ival;
        parsed_time = mktime(info);
        if (parsed_time < base_time) {
            info->tm_year++;
            parsed_time = mktime(info);
        }
    }
break;
case 16:
{ yyval.ival = yyvsp[1].ival * yyvsp[2].ival; }
break;
case 17:
{ yyval.ival = 60; }
break;
case 18:
{ yyval.ival = 3600; }
break;
case 19:
{ yyval.ival = 86400; }
break;
case 20:
{ yyval.ival = 604800; }
break;
    }

    /* Push result */
    /* GOTO: new state based on LHS symbol */
    yystate = yylhs[yyn];
    yyn = yygindex[yystate];
    if (yyn != 0 && (yyn += *yyssp) >= 0 && yyn < 458 && yycheck[yyn] == *yyssp)
        yystate = yytable[yyn];
    else
        yystate = yydgoto[yystate];
    *++yyssp = yystate;
    *++yyvsp = yyval;
    goto yyloop;

    return 1;
}

#line 178 "at_parse.y"


void yyerror(const char *s) {
    (void)s;
}
