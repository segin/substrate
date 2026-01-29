/*
 * output.c - C Code Generation
 *
 * Generates the y.tab.c parser file with LALR(1) tables and yyparse().
 * Also generates y.tab.h header file if -d option used.
 */

#include "defs.h"

/* External references */
extern action **parser;
extern short final_state;
extern int SRtotal, RRtotal;

/* Output state */
static int outline;         /* Current line in output */

/* Table output helpers */
static void output_prefix(void);
static void output_stored_text(void);
static void output_defines(void);
static void output_stype(void);
static void output_rule_data(void);
static void output_yydefred(void);
static void output_actions(void);
static void output_base(void);
static void output_table(void);
static void output_check(void);
static void output_parser(void);
static void output_trailing_text(void);
static void output_semantic_actions(void);
static void write_section(const char *section_name);
static void putc_code(FILE *f, int c);
static void puts_code(FILE *f, const char *s);
static void output_line_directive(FILE *f, int line, const char *file);

void output(void) {
    output_prefix();
    output_stored_text();
    output_defines();
    output_stype();
    output_rule_data();
    output_yydefred();
    output_actions();
    output_base();
    output_table();
    output_check();
    output_parser();
    output_semantic_actions();
    output_trailing_text();
    
    if (dflag) {
        /* Generate header file */
        output_defines();
    }
}

static void output_prefix(void) {
    /* Output any custom prefix definitions */
    if (symbol_prefix && strcmp(symbol_prefix, "yy") != 0) {
        fprintf(output_file, "#define yyparse %sparse\n", symbol_prefix);
        fprintf(output_file, "#define yylex %slex\n", symbol_prefix);
        fprintf(output_file, "#define yyerror %serror\n", symbol_prefix);
        fprintf(output_file, "#define yylval %slval\n", symbol_prefix);
        fprintf(output_file, "#define yychar %schar\n", symbol_prefix);
        fprintf(output_file, "#define yydebug %sdebug\n", symbol_prefix);
        fprintf(output_file, "#define yynerrs %snerrs\n", symbol_prefix);
        fprintf(output_file, "\n");
    }
}

static void output_stored_text(void) {
    /* Output %{ ... %} blocks from grammar file */
    /* These are stored during parsing and copied here */
    if (!lflag) {
        output_line_directive(output_file, 1, infile_name);
    }
    /* Copy stored text (from code_file or action_file) */
}

static void output_defines(void) {
    bucket *bp;
    
    fprintf(output_file, "/* Token definitions */\n");
    
    /* Output #define for each token */
    /* Iterate through symbol table */
    for (bp = first_symbol; bp != NULL; bp = bp->next) {
        if (bp->class == CLASS_TERM && bp->value > 256) {
            fprintf(output_file, "#define %s %d\n", bp->name, bp->value);
        }
    }
    fprintf(output_file, "\n");
    
    /* Also write to header file if -d */
    if (dflag && defines_file != NULL) {
        fprintf(defines_file, "#ifndef YYTOKENTYPE\n");
        fprintf(defines_file, "#define YYTOKENTYPE\n");
        for (bp = first_symbol; bp != NULL; bp = bp->next) {
            if (bp->class == CLASS_TERM && bp->value > 256) {
                fprintf(defines_file, "#define %s %d\n", bp->name, bp->value);
            }
        }
        fprintf(defines_file, "#endif\n\n");
    }
}

static void output_stype(void) {
    /* Output YYSTYPE definition */
    fprintf(output_file, "/* Semantic value type */\n");
    fprintf(output_file, "#ifndef YYSTYPE\n");
    fprintf(output_file, "typedef int YYSTYPE;\n");
    fprintf(output_file, "#endif\n\n");
    fprintf(output_file, "YYSTYPE yylval;\n\n");
    
    if (dflag && defines_file != NULL) {
        fprintf(defines_file, "#ifndef YYSTYPE\n");
        fprintf(defines_file, "typedef int YYSTYPE;\n");
        fprintf(defines_file, "#endif\n");
        fprintf(defines_file, "extern YYSTYPE yylval;\n\n");
    }
}

static void output_rule_data(void) {
    int i;
    
    fprintf(output_file, "/* Rule lengths (number of RHS symbols) */\n");
    fprintf(output_file, "static const short yylen[] = {\n");
    for (i = 0; i < nrules; i++) {
        /* Calculate rule length: count items until negative (rule end) */
        int len = 0;
        int j = rrhs[i];
        while (ritem[j] >= 0) {
            len++;
            j++;
        }
        fprintf(output_file, "  %d,\n", len);
    }
    fprintf(output_file, "};\n\n");
    
    fprintf(output_file, "/* Left-hand side symbols */\n");
    fprintf(output_file, "static const short yylhs[] = {\n");
    for (i = 0; i < nrules; i++) {
        fprintf(output_file, "  %d,\n", plhs[i]);
    }
    fprintf(output_file, "};\n\n");
}

static void output_yydefred(void) {
    /* Output default reductions */
    fprintf(output_file, "/* Default reductions */\n");
    fprintf(output_file, "static const short yydefred[] = {\n");
    /* For each state, output default reduction rule number (or 0) */
    fprintf(output_file, "};\n\n");
}

static void output_actions(void) {
    /* Output compressed ACTION table */
    fprintf(output_file, "/* Parser actions (compressed) */\n");
    /* This uses pair displacement compression */
}

static void output_base(void) {
    /* Output table base offsets */
    fprintf(output_file, "/* Table offsets */\n");
}

static void output_table(void) {
    /* Output main parser table */
    fprintf(output_file, "/* Parser table */\n");
}

static void output_check(void) {
    /* Output table check values */
    fprintf(output_file, "/* Check values for table access */\n");
}

static void output_parser(void) {
    /* Output the yyparse() function - complete skeleton */
    fprintf(output_file, "\n/* Parser constants */\n");
    fprintf(output_file, "#ifndef YYMAXDEPTH\n");
    fprintf(output_file, "#define YYMAXDEPTH 500\n");
    fprintf(output_file, "#endif\n");
    fprintf(output_file, "#ifndef YYERRCODE\n");
    fprintf(output_file, "#define YYERRCODE 256\n");
    fprintf(output_file, "#endif\n\n");
    
    fprintf(output_file, "/* External declarations */\n");
    fprintf(output_file, "extern int yylex(void);\n");
    fprintf(output_file, "extern void yyerror(const char *);\n");
    fprintf(output_file, "extern YYSTYPE yylval;\n\n");
    
    fprintf(output_file, "int yydebug = 0;\n");
    fprintf(output_file, "int yynerrs = 0;\n");
    fprintf(output_file, "int yyerrflag = 0;\n");
    fprintf(output_file, "int yychar = -1;\n\n");
    
    fprintf(output_file, "int yyparse(void) {\n");
    fprintf(output_file, "    int yyn;\n");
    fprintf(output_file, "    int yystate = 0;\n");
    fprintf(output_file, "    YYSTYPE yyval;\n");
    fprintf(output_file, "    \n");
    fprintf(output_file, "    /* State and value stacks */\n");
    fprintf(output_file, "    short yyss[YYMAXDEPTH];\n");
    fprintf(output_file, "    YYSTYPE yyvs[YYMAXDEPTH];\n");
    fprintf(output_file, "    short *yyssp = yyss;\n");
    fprintf(output_file, "    YYSTYPE *yyvsp = yyvs;\n");
    fprintf(output_file, "    \n");
    fprintf(output_file, "    *yyssp = 0; /* Push initial state */\n");
    fprintf(output_file, "    yychar = -1;\n");
    fprintf(output_file, "    yynerrs = 0;\n");
    fprintf(output_file, "    yyerrflag = 0;\n\n");
    
    fprintf(output_file, "yyloop:\n");
    fprintf(output_file, "    /* Check for stack overflow */\n");
    fprintf(output_file, "    if (yyssp >= yyss + YYMAXDEPTH - 1) {\n");
    fprintf(output_file, "        yyerror(\"yacc stack overflow\");\n");
    fprintf(output_file, "        return 1;\n");
    fprintf(output_file, "    }\n\n");
    
    fprintf(output_file, "    /* Check for default reduction */\n");
    fprintf(output_file, "    yyn = yydefred[yystate];\n");
    fprintf(output_file, "    if (yyn != 0) goto yyreduce;\n\n");
    
    fprintf(output_file, "    /* Get lookahead token if needed */\n");
    fprintf(output_file, "    if (yychar < 0) {\n");
    fprintf(output_file, "        yychar = yylex();\n");
    fprintf(output_file, "        if (yychar < 0) yychar = 0; /* EOF */\n");
    fprintf(output_file, "    }\n\n");
    
    fprintf(output_file, "    /* Shift action? */\n");
    fprintf(output_file, "    /* TODO: Table lookup for shift */\n");
    fprintf(output_file, "    /* yyn = yysindex[yystate] + yychar; */\n");
    fprintf(output_file, "    /* if (check_bounds && yytable[yyn] == yychar) goto yyshift; */\n\n");
    
    fprintf(output_file, "    /* Reduce action? */\n");
    fprintf(output_file, "    /* TODO: Table lookup for reduce */\n\n");
    
    fprintf(output_file, "    /* Error handling */\n");
    fprintf(output_file, "    if (yyerrflag == 0) {\n");
    fprintf(output_file, "        yyerror(\"syntax error\");\n");
    fprintf(output_file, "        yynerrs++;\n");
    fprintf(output_file, "    }\n");
    fprintf(output_file, "    goto yyloop;\n\n");
    
    fprintf(output_file, "yyshift:\n");
    fprintf(output_file, "    /* Shift: push new state */\n");
    fprintf(output_file, "    yystate = yyn;\n");
    fprintf(output_file, "    *++yyssp = yystate;\n");
    fprintf(output_file, "    *++yyvsp = yylval;\n");
    fprintf(output_file, "    yychar = -1; /* Consumed token */\n");
    fprintf(output_file, "    if (yyerrflag > 0) yyerrflag--;\n");
    fprintf(output_file, "    goto yyloop;\n\n");
    
    fprintf(output_file, "yyreduce:\n");
    fprintf(output_file, "    /* Reduce by rule yyn */\n");
    fprintf(output_file, "    yyssp -= yylen[yyn];\n");
    fprintf(output_file, "    yyvsp -= yylen[yyn];\n");
    fprintf(output_file, "    yyval = yyvsp[1]; /* Default $$ = $1 */\n");
    fprintf(output_file, "    \n");
    fprintf(output_file, "    /* Execute semantic action */\n");
    fprintf(output_file, "    switch (yyn) {\n");
    fprintf(output_file, "    /* Semantic actions will be inserted here */\n");
    fprintf(output_file, "    }\n");
    fprintf(output_file, "    \n");
    fprintf(output_file, "    /* Push result */\n");
    fprintf(output_file, "    /* TODO: GOTO table lookup */\n");
    fprintf(output_file, "    /* yystate = yypgoto[yylhs[yyn]] + *yyssp; */\n");
    fprintf(output_file, "    *++yyssp = yystate;\n");
    fprintf(output_file, "    *++yyvsp = yyval;\n");
    fprintf(output_file, "    goto yyloop;\n\n");
    
    fprintf(output_file, "yyaccept:\n");
    fprintf(output_file, "    return 0;\n\n");
    
    fprintf(output_file, "yyabort:\n");
    fprintf(output_file, "    return 1;\n");
    fprintf(output_file, "}\n\n");
}


static void output_semantic_actions(void) {
    /* Output semantic action switch */
    fprintf(output_file, "/* Semantic actions */\n");
    fprintf(output_file, "static void yyaction(int rule, YYSTYPE *yyvsp, YYSTYPE *yyval) {\n");
    fprintf(output_file, "    switch (rule) {\n");
    /* For each rule with an action, output case statement */
    /* Copy action code from action_file */
    fprintf(output_file, "    }\n");
    fprintf(output_file, "}\n\n");
}

static void output_trailing_text(void) {
    /* Copy user code section (after second %%) */
    if (!lflag) {
        output_line_directive(output_file, lineno, infile_name);
    }
    /* Copy remainder of input file */
}

static void output_line_directive(FILE *f, int line, const char *file) {
    if (!lflag) {
        fprintf(f, "#line %d \"%s\"\n", line, file);
    }
}

static void putc_code(FILE *f, int c) {
    putc(c, f);
    if (c == '\n') outline++;
}

static void puts_code(FILE *f, const char *s) {
    while (*s) {
        putc_code(f, *s++);
    }
}

void output_header(void) {
    /* Generate y.tab.h */
    if (!dflag || defines_file == NULL) return;
    
    fprintf(defines_file, "/* Generated by yacc */\n\n");
    output_defines();
    output_stype();
    
    fclose(defines_file);
}
