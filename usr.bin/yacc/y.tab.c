#line 1 "test/expr.y"
/* Token definitions */
#define NUMBER 257
#define LPAREN 258
#define RPAREN 259
#define PLUS 260
#define MINUS 261

/* Semantic value type */
#ifndef YYSTYPE
typedef int YYSTYPE;
#endif

YYSTYPE yylval;

/* Rule lengths (number of RHS symbols) */
static const short yylen[] = {
  0,
  0,
  0,
  2,
  1,
  2,
  1,
  3,
  3,
  3,
  3,
  3,
};

/* Left-hand side symbols */
static const short yylhs[] = {
  0,
  0,
  0,
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

/* Default reductions */
static const short yydefred[] = {
};

/* Parser actions (compressed) */
/* Table offsets */
/* Parser table */
/* Check values for table access */

/* Parser skeleton */
int yyparse(void) {
    int yystate = 0;
    int yychar = -1;
    int yynerrs = 0;
    YYSTYPE yyval;
    
    /* Stack */
    short yyss[YYMAXDEPTH];
    YYSTYPE yyvs[YYMAXDEPTH];
    short *yyssp = yyss;
    YYSTYPE *yyvsp = yyvs;
    
    *yyssp = 0; /* Push initial state */
    
yyloop:
    /* Main parse loop */
    /* ... implementation ... */
    return 0;
}

/* Semantic actions */
static void yyaction(int rule, YYSTYPE *yyvsp, YYSTYPE *yyval) {
    switch (rule) {
    }
}

#line 41 "test/expr.y"
/* Token definitions */
#define NUMBER 257
#define LPAREN 258
#define RPAREN 259
#define PLUS 260
#define MINUS 261

