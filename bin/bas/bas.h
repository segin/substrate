#ifndef BAS_H
#define BAS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* Constants */
#define SZ_SPACE    8192
#define SZ_LINTAB   600   /* 300 statements * 2 words? V7 had 1800 bytes for 300 lines */
#define SZ_SYMTAB   200   /* 200 symbols */
#define SZ_STACK    200   /* Execution stack depth */

/* Types */

/* Opcodes */
enum {
    OP_END = 0,
    OP_PRINT,       /* Print Value (pop double) */
    OP_PRINT_NL,    /* Print Newline */
    OP_GOTO,
    OP_IF,
    OP_FOR,
    OP_NEXT,
    OP_GOSUB,
    OP_RETURN,
    OP_LET,
    OP_CONST,
    OP_VAR_VAL, /* Push value of var */
    OP_VAR_REF, /* Push address of var */
    OP_BUILTIN, /* Built-in function */
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_EQ,
    OP_LT,
    OP_GT,
    OP_STR  /* Print string literal */
};

/* Opcode instruction */
typedef struct {
    int opcode;
    union {
        int i;
        double f;
        char *s;
        int target; /* Jump target offset */
    } arg;
} Instruction;

/* Symbol Table Entry */
typedef struct {
    char name[4];
    double value;
    int defined;
} Symbol;

/* Line Table Entry */
typedef struct {
    int lineno;
    int offset; /* Offset into instructions array */
    char *text; /* Source text for listing */
} Line;

/* Globals */
extern Instruction space[SZ_SPACE];
extern int space_idx;

extern Line lintab[SZ_LINTAB];
extern int lintab_size;

extern Symbol symtab[SZ_SYMTAB];
extern int symtab_size;

/* Global State */
extern int cur_line; /* Current line number during execution */

/* Function Prototypes */
void bas_init(void);
void bas_loop(void);
void bas_error(const char *msg);

int find_line(int lineno);
void add_line(int lineno, int offset, const char *text);

int find_symbol(const char *name);
int add_symbol(const char *name);

/* Compiler */
int compile_line(int lineno, const char *text, int *out_offset);
void list_program(void);

/* Runtime */
void execute_program(void);
void execute_immediate(const char *text);

#endif
