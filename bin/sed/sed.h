/*
 * sed.h - internal definitions for the sed stream editor.
 */
#ifndef SED_H
#define SED_H

#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/types.h>

/* ------------------------------------------------------------------ */
/* Dynamic buffer                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} dynbuf_t;

void  db_init(dynbuf_t *db);
void  db_free(dynbuf_t *db);
int   db_reserve(dynbuf_t *db, size_t extra);
int   db_append(dynbuf_t *db, const char *s, size_t n);
int   db_appendc(dynbuf_t *db, char c);
void  db_clear(dynbuf_t *db);
void  db_set(dynbuf_t *db, const char *s, size_t n);
/* Ensures buf is NUL-terminated (doesn't add to len) */
int   db_ensure_nul(dynbuf_t *db);

/* ------------------------------------------------------------------ */
/* Addresses                                                            */
/* ------------------------------------------------------------------ */

typedef enum {
    A_NONE   = 0,
    A_LINE   = 1,   /* specific line number */
    A_LAST   = 2,   /* $ (last line) */
    A_REGEX  = 3,   /* /pattern/ */
    A_STEP   = 4,   /* first~step */
    A_RELOFF = 5,   /* ,+N  — resolved only as addr2 */
    A_RELMUL = 6,   /* ,~N  — resolved only as addr2 */
} addrtype_t;

typedef struct {
    addrtype_t  type;
    long        line;       /* A_LINE value; A_STEP first; A_RELOFF/RELMUL N */
    long        step;       /* A_STEP step */
    char       *pat;        /* A_REGEX: pattern string */
    unsigned    reflags;    /* A_REGEX: compile flags */
    int         icase;      /* I flag: case-insensitive regex addr */
} addr_t;

/* ------------------------------------------------------------------ */
/* Commands                                                             */
/* ------------------------------------------------------------------ */

#define MAX_WRITE_FILES  32
#define DEFAULT_LIST_WRAP 70

typedef struct {
    char       *pat;    /* pattern string */
    unsigned    reflags; /* compile flags */
    char       *repl;   /* replacement template string */
    int         global; /* g flag */
    int         print;  /* p flag */
    int         nth;    /* Nth-occurrence (0 = first or all with g) */
    int         icase;  /* i/I flag */
    int         exec;   /* e flag: execute result as shell command */
    int         wfile;  /* index into write_files[], -1 = none */
} subst_t;

typedef struct {
    unsigned char map[256];
} trans_t;

typedef enum {
    C_APPEND  = 'a',
    C_BRANCH  = 'b',
    C_CHANGE  = 'c',
    C_DELETE  = 'd',
    C_DELETEP = 'D',
    C_EXEC    = 'e',  /* GNU: execute pattern space */
    C_FILE    = 'F',  /* GNU: print current filename */
    C_GET     = 'g',
    C_GETAPP  = 'G',
    C_HOLD    = 'h',
    C_HOLDAPP = 'H',
    C_INSERT  = 'i',
    C_LABEL   = ':',
    C_LIST    = 'l',
    C_NEXT    = 'n',
    C_NEXTAPP = 'N',
    C_PRINT   = 'p',
    C_PRINTP  = 'P',
    C_QUIT    = 'q',
    C_QUITND  = 'Q',  /* BSD: quit without default output */
    C_READ    = 'r',
    C_READLN  = 'R',  /* BSD: read one line */
    C_SUBST   = 's',
    C_TRANS   = 'y',
    C_WRITE   = 'w',
    C_WRITELN = 'W',  /* BSD: write first line of pattern space */
    C_EQUAL   = '=',
    C_EXCH    = 'x',
    C_BRANT   = 't',
    C_BRANTF  = 'T',  /* BSD: branch if NO substitution */
    C_ZAP     = 'z',  /* GNU: clear pattern space */
    C_LBRACE  = '{',
    C_RBRACE  = '}',
} cmdtype_t;

typedef struct cmd_s cmd_t;
struct cmd_s {
    /* address specification */
    int         naddr;      /* 0, 1, or 2 */
    addr_t      addr[2];
    int         negate;     /* ! modifier */

    /* per-command range-tracking state (mutates across input lines) */
    int         in_range;
    long        range_end;  /* computed end line for A_RELOFF / A_RELMUL */

    /* command payload */
    cmdtype_t   type;
    char       *text;       /* a/i/c: text; b/t/T: label; :/r/R/w/W: string */
    subst_t    *subst;
    trans_t    *trans;
    int         num;        /* q/Q: exit code; l: wrap width */

    /* linking */
    cmd_t      *next;
    cmd_t      *target;     /* b/t/T: resolved branch target (NULL = end) */
    cmd_t      *end_block;  /* {: matching } command */
};

/* ------------------------------------------------------------------ */
/* Global execution state                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    /* scripts / options */
    bool        suppress;       /* -n */
    bool        inplace;        /* -i */
    char       *inplace_ext;    /* -i extension */
    bool        separate;       /* -s: treat files separately */
    bool        sandbox;        /* -S: sandbox (no r/R/w/W/e) */
    bool        null_delim;     /* -z: NUL-delimited */
    bool        use_ere;        /* -E/-r: ERE mode */
    int         list_wrap;      /* -l width */

    /* command list */
    cmd_t      *cmds;           /* head of command list */
    cmd_t      *cmds_tail;

    /* write files */
    char       *write_files[MAX_WRITE_FILES];
    FILE       *write_fps[MAX_WRITE_FILES];
    int         write_count;

    /* runtime */
    dynbuf_t    pat;            /* pattern space */
    dynbuf_t    hold;           /* hold space (init = "") */
    dynbuf_t    append_queue;   /* pending a/r output */

    long        lineno;         /* current input line number */
    bool        last_line;      /* true when processing last line */
    bool        subst_flag;     /* set by s///; cleared by t/T/new line */
    bool        no_print;       /* set when d/D/q has started new cycle */
    int         exit_code;      /* for q/Q */

    const char *cur_filename;   /* current input file name */
} sed_state_t;

extern sed_state_t G;

/* ------------------------------------------------------------------ */
/* Parse API (sed_parse.c)                                              */
/* ------------------------------------------------------------------ */

/* Append a script string to the master script buffer */
void script_append(const char *s);
void script_append_file(const char *path);

/* Parse the accumulated script into cmd list in G */
int  script_parse(void);

/* ------------------------------------------------------------------ */
/* Exec API (sed_exec.c)                                                */
/* ------------------------------------------------------------------ */

/* Execute the command list on stdin or the given files */
int  sed_process_file(FILE *fp, const char *name, bool is_last_file);

/* Print a line in `l` format */
void sed_list_print(const char *s, size_t len, int wrap, FILE *out);

/* Apply s/// substitution; returns 1 if matched */
int  sed_do_subst(subst_t *sub, dynbuf_t *pat);

/* ------------------------------------------------------------------ */
/* Error helpers                                                        */
/* ------------------------------------------------------------------ */

void die(const char *fmt, ...) __attribute__((noreturn, format(printf,1,2)));
void warn(const char *fmt, ...) __attribute__((format(printf,1,2)));

#endif /* SED_H */
