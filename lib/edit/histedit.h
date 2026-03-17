#ifndef _HISTEDIT_H_
#define _HISTEDIT_H_

#include <stdio.h>
#include <sys/types.h>

/* Line editing structure */
typedef struct editline EditLine;

/* History structure */
typedef struct history History;

/* Line mapping structure */
typedef struct {
    const char *buffer;
    const char *cursor;
    const char *lastchar;
} LineInfo;

/*
 * EditLine functions
 */
EditLine *el_init(const char *prog, FILE *fin, FILE *fout, FILE *ferr);
void      el_end(EditLine *el);
void      el_reset(EditLine *el);

const char *el_gets(EditLine *el, int *count);
int         el_get(EditLine *el, int op, ...);
int         el_set(EditLine *el, int op, ...);
int         el_resize(EditLine *el);

const LineInfo *el_line(EditLine *el);

/*
 * History functions
 */
History *history_init(void);
void     history_end(History *h);

typedef struct {
    int num;
    const char *str;
} HistEvent;

int history(History *h, HistEvent *ev, int op, ...);

/* el_set/el_get operations */
#define EL_PROMPT       0
#define EL_TERMINAL     1
#define EL_EDITOR       2
#define EL_SIGNAL       3
#define EL_BIND         4
#define EL_ECHOTC       5
#define EL_SETTC        6
#define EL_REFRESH      7
#define EL_HIST         8
#define EL_RPROMPT      9
#define EL_CLIENTDATA   10

/* history operations */
#define H_FUNC          0
#define H_SETSIZE       1
#define H_GETSIZE       2
#define H_FIRST         3
#define H_LAST          4
#define H_PREV          5
#define H_NEXT          6
#define H_CURR          7
#define H_SET           8
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

#endif /* _HISTEDIT_H_ */
