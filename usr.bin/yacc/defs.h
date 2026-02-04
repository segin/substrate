/*
 * defs.h - Global definitions for yacc
 *
 * Conforms to POSIX.1-2017
 */

#ifndef _DEFS_H_
#define _DEFS_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <limits.h>

/* -- Constants -- */

#define MAXTOKEN    2048    /* Limit on length of a token/name */
#define MAXSYM      4000    /* Maximum number of symbols */
#define MAXPROD     2000    /* Maximum number of productions */
#define MAXSTATE    2000    /* Maximum number of states */

/* Token start values (POSIX/Classic) */
#define FIRST_TOKEN 257

/* Associativity/Precedence types */
typedef enum {
    NO_ASSOC = 0,
    LEFT_ASSOC,
    RIGHT_ASSOC,
    NON_ASSOC
} assoc_t;

/* Symbol classes */
typedef enum {
    UNKNOWN = 0,
    CLASS_TERM,
    CLASS_NONTERM,
    CLASS_ACTION      /* For mid-rule actions */
} sym_class_t;

/* -- Data Structures -- */

/* Symbol Table Entry */
typedef struct bucket {
    struct bucket *link;    /* Hash chain */
    struct bucket *next;    /* Linear list */
    char *name;             /* Symbol name */
    char *tag;              /* Union tag (if any) */
    int value;              /* Token value (TERMs) or index (NONTERMs) */
    short index;            /* Symbol index (0..nsyms) */
    short prec;             /* Precedence level */
    int class;             /* sym_class_t */
    char assoc;             /* assoc_t */
} bucket;

/* LR(0) State Core - kernel items */
typedef struct core {
    struct core *next;          /* Linked list of all states */
    struct core *link;          /* Hash chain */
    short number;               /* State number */
    short accessing_symbol;     /* Symbol to reach this state */
    short nitems;               /* Number of kernel items */
    short *items;               /* Kernel items (dot positions in ritem) */
} core;

/* Shift actions for a state */
typedef struct shifts {
    struct shifts *next;
    short number;               /* State number */
    short nshifts;              /* Number of shifts */
    short shift[1];             /* Destination states (flexible array) */
} shifts;

/* Reduce actions for a state */
typedef struct reductions {
    struct reductions *next;
    short number;               /* State number */
    short nreds;                /* Number of reductions */
    short rules[1];             /* Rule numbers (flexible array) */
} reductions;

/* Action entry for parser table */
typedef struct action {
    struct action *next;
    short symbol;               /* Lookahead symbol */
    short number;               /* State or rule number */
    short prec;                 /* Precedence */
    char action_code;           /* SHIFT, REDUCE, ACCEPT, ERROR */
    char assoc;                 /* Associativity */
    char suppressed;            /* Conflict resolution result */
} action;

/* Action codes */
#define SHIFT   1
#define REDUCE  2
#define ACCEPT  3
#define ERROR   4

/* Production Rule */
/* A -> B C D */
/* stored as array of symbol indices */

/* Global Options */
extern int dflag;   /* -d: defines header */
extern int lflag;   /* -l: no #line */
extern int tflag;   /* -t: debug code */
extern int vflag;   /* -v: verbose output */
extern char *file_prefix; /* -b: prefix */
extern char *symbol_prefix; /* -p: name prefix */

/* File pointers */
extern FILE *input_file;
extern FILE *output_file;
extern FILE *verbose_file;
extern FILE *defines_file;
extern FILE *action_file;
extern FILE *code_file;
extern FILE *epilogue_file;
extern FILE *union_file;
extern FILE *graph_file;

/* Globals */
extern char *infile_name;
extern int lineno;
extern bucket *goal_symbol;

extern int nitems;
extern int nrules;
extern int nsyms;
extern int ntokens;
extern int nvars;
extern int union_defined;   /* Set if %union was used */

/* extern short *rlhs; -- stored in plhs? No, plhs is array of indices to symbol table? */
/* Standard yacc: */
/* ritem: array of symbol indices, 0-terminated for each rule, packed? */
/* OR: plhs[rule_no] = symbol_index */
/*     rritem[rule_no] = index into ritem array */

/* Classic Yacc structures */
extern short plhs[MAXPROD];
extern short ritem[MAXPROD * 4]; /* Conservative estimate */
extern short rlhs[MAXPROD];
extern short rrhs[MAXPROD]; /* Index into ritem */

/* LR(0) state construction globals (defined in lr0.c) */
extern short *item_set;
extern short *item_set_end;
extern unsigned *rules_used;
extern int nstates;

/* Closure globals (defined in closure.c) */
extern short *first_derives;
extern short *eff;

/* Symbol table globals (defined in symtab.c) */
extern bucket *first_symbol;
extern bucket *last_symbol;
extern bucket *symbol_table[];

/* LR(0) shift/reduction lists (defined in lr0.c) */
extern shifts *first_shift;
extern shifts *last_shift;
extern reductions *first_reduction;
extern reductions *last_reduction;
extern short *accessing_symbol;  /* Symbol to reach each state */

/* Parser tables (defined in mkpar.c) */
extern action **parser;          /* ACTION table */
extern short *yydefred;          /* Default reduction for each state */
extern short *yydgoto;           /* GOTO table */
extern short *yysindex;          /* Shift index table */
extern short *yyrindex;          /* Reduce index table */
extern short *yygindex;          /* Goto index table */
extern short *yytable;           /* Compressed action/goto table */
extern short *yycheck;           /* Check table for compression */
extern int yytable_size;         /* Size of yytable/yycheck */
extern short final_state;        /* Accept state */
extern int SRtotal;              /* Shift/reduce conflict count */
extern int RRtotal;              /* Reduce/reduce conflict count */

/* Reader state globals if needed */

/* -- Prototypes -- */

/* main.c */
void open_files(void);
void done(int k);
void no_space(void);

/* -- Reader/Lexer Tokens -- */
#define IDENTIFIER      258
#define MARK            259     /* %% */
#define TERM            260     /* %token */
#define LEFT            261     /* %left */
#define RIGHT           262     /* %right */
#define NONASSOC        263     /* %nonassoc */
#define UNION           264     /* %union */
#define TYPE            265     /* %type */
#define START           266     /* %start */
#define EXPECT          267     /* %expect */
#define LCURLY          268     /* { */
#define RCURLY          269     /* } */

/* -- Global variables for Reader -- */
extern char *line;
extern int linesize;
extern bucket *error_symbol;

/* reader.c */
void reader(void);

/* lr0.c */
void lr0(void);

/* closure.c */
void set_first_derives(void);
void closure(short *nucleus, int n);
void closure_new(int n); /* If using new style */

/* lalr.c */
void lalr(void);

/* mkpar.c */
void make_parser(void);

/* output.c */
void output(void);

/* symtab.c */
void init_symtab(void);
bucket *lookup(char *name);
bucket *make_bucket(char *name);
void free_symtab(void);

/* verbose.c */
void verbose(void);
void create_output_file(void);

#endif /* _DEFS_H_ */
