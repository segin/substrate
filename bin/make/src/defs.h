#ifndef _MAKE_DEFS_H
#define _MAKE_DEFS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define ALLOC(x) (struct x *) calloc(1, sizeof(struct x))

#define NO 0
#define YES 1

#define OUTMAX 4096

typedef time_t TIMETYPE;

struct nameblock {
    struct nameblock *nxtnameblock;
    char *namep;
    struct lineblock *linep;
    int done;
    int septype;
    TIMETYPE modtime;
};

struct lineblock {
    struct lineblock *nxtlineblock;
    struct depblock *depp;
    struct shblock *shp;
};

struct depblock {
    struct depblock *nxtdepblock;
    struct nameblock *depname;
};

struct shblock {
    struct shblock *nxtshblock;
    char *shbp;
};

struct varblock {
    struct varblock *nxtvarblock;
    char *varname;
    char *varval;
    int noreset;
    int used;
};

struct pattern {
    struct pattern *nxtpattern;
    char *patval;
};

// Globals
extern struct nameblock *mainname;
extern struct nameblock *firstname;
extern struct lineblock *sufflist;
extern struct varblock *firstvar;
extern struct pattern *firstpat;

extern int ignerr;
extern int silflag;
extern int noexflag;
extern int keepgoing;
extern int touchflag;
extern int questflag;
extern int dbgflag;

// Prototypes
struct nameblock *srchname(char *name);
struct nameblock *makename(char *name);
void setvar(char *name, char *val);
struct varblock *varptr(char *name);
char *subst(char *a, char *b);
char *copys(char *s);
char *concat(char *a, char *b, char *c);
void fatal(char *s);
void fatal1(char *s, char *a);
int doname(struct nameblock *p, int reclevel, TIMETYPE *tval);
int docom(struct shblock *q);
int dosys(char *comstring, int nohalt);
TIMETYPE exists(char *filename);
void rddescf(char *descfile);

#define ALLDEPS 1
#define SOMEDEPS 2

#endif
