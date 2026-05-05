#ifndef _HISTEDIT_H_
#define _HISTEDIT_H_

#include <stdio.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIBEDIT_MAJOR 2
#define LIBEDIT_MINOR 11

/* Line editing structure */
typedef struct editline EditLine;
typedef const char *(*el_pfunc_t)(EditLine *);
typedef int (*el_rfunc_t)(EditLine *, char *);
typedef unsigned char (*el_func_t)(EditLine *, int);
typedef void (*el_zfunc_t)(EditLine *, void *);
typedef const char *(*el_afunc_t)(void *, const char *);
typedef void *histdata_t;

/* History structure */
typedef struct history History;

/* Line mapping structure */
typedef struct lineinfo {
    const char *buffer;
    const char *cursor;
    const char *lastchar;
} LineInfo;

#define CC_NORM         0
#define CC_NEWLINE      1
#define CC_EOF          2
#define CC_ARGHACK      3
#define CC_REFRESH      4
#define CC_CURSOR       5
#define CC_ERROR        6
#define CC_FATAL        7
#define CC_REDISPLAY    8
#define CC_REFRESH_BEEP 9

/*
 * EditLine functions.
 *
 * Thread-safety: EditLine is NOT thread-safe.  Each EditLine handle must
 * only be used from a single thread at a time.  No internal locking is
 * performed; the caller is responsible for serialisation if an EditLine
 * handle is shared across threads.
 */
EditLine *el_init(const char *prog, FILE *fin, FILE *fout, FILE *ferr);
EditLine *el_init_fd(const char *prog, FILE *fin, FILE *fout, FILE *ferr,
                     int fdin, int fdout, int fderr);
void      el_end(EditLine *el);
void      el_reset(EditLine *el);

const char *el_gets(EditLine *el, int *count);
int         el_getc(EditLine *el, char *ch);
void        el_push(EditLine *el, const char *str);
void        el_beep(EditLine *el);
int         el_parse(EditLine *el, int argc, const char **argv);
int         el_get(EditLine *el, int op, ...);
int         el_set(EditLine *el, int op, ...);
void        el_resize(EditLine *el);
int         el_gets_continuation(EditLine *el);

const LineInfo *el_line(EditLine *el);
int             el_insertstr(EditLine *el, const char *str);
void            el_deletestr(EditLine *el, int count);
int             el_replacestr(EditLine *el, const char *str);
int             el_deletestr1(EditLine *el, int start, int count);

/*
 * History functions
 */
History *history_init(void);
void     history_end(History *h);

typedef struct HistEvent {
    int num;
    const char *str;
} HistEvent;

int history(History *h, HistEvent *ev, int op, ...);

/*
 * Tokenizer
 */
typedef struct tokenizer Tokenizer;

Tokenizer *tok_init(const char *ifs);
void       tok_end(Tokenizer *tok);
void       tok_reset(Tokenizer *tok);
int        tok_line(Tokenizer *tok, const LineInfo *li, int *argc,
		    const char ***argv, int *cursorc, int *cursoro);
int        tok_str(Tokenizer *tok, const char *str, int *argc,
		   const char ***argv);

/*
 * el_source - read and execute .editrc commands
 */
int el_source(EditLine *el, const char *file);

/* BSD libedit el_set/el_get operations */
#define EL_PROMPT       0
#define EL_TERMINAL     1
#define EL_EDITOR       2
#define EL_SIGNAL       3
#define EL_BIND         4
#define EL_TELLTC       5
#define EL_SETTC        6
#define EL_ECHOTC       7
#define EL_SETTY        8
#define EL_ADDFN        9
#define EL_HIST         10
#define EL_EDITMODE     11
#define EL_RPROMPT      12
#define EL_GETCFN       13
#define EL_CLIENTDATA   14
#define EL_UNBUFFERED   15
#define EL_PREP_TERM    16
#define EL_GETTC        17
#define EL_GETFP        18
#define EL_SETFP        19
#define EL_REFRESH      20
#define EL_PROMPT_ESC   21
#define EL_RPROMPT_ESC  22
#define EL_RESIZE       23
#define EL_ALIAS_TEXT   24
#define EL_SAFEREAD     25

#define EL_BUILTIN_GETCFN ((el_rfunc_t)0)

/* Substrate libedit extension; kept outside the BSD operation range. */
#define EL_SETFN        1000

/* history operations */
#define H_FUNC          0
#define H_SETSIZE       1
#define H_GETSIZE       2
#define H_FIRST         3
#define H_LAST          4
#define H_PREV          5
#define H_NEXT          6
#define H_SET           7
#define H_CURR          8
#define H_ADD           9
#define H_ENTER         10
#define H_APPEND        11
#define H_END           12
#define H_NEXT_STR      13
#define H_PREV_STR      14
#define H_NEXT_EVENT    15
#define H_PREV_EVENT    16
#define H_LOAD          17
#define H_SAVE          18
#define H_CLEAR         19
#define H_SETUNIQUE     20
#define H_GETUNIQUE     21
#define H_DEL           22
#define H_NEXT_EVDATA   23
#define H_DELDATA       24
#define H_REPLACE       25
#define H_SAVE_FP       26
#define H_NSAVE_FP      27

#ifdef __cplusplus
}
#endif

#endif /* _HISTEDIT_H_ */
