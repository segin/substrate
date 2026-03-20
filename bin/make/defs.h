#ifndef _DEFS_H
#define _DEFS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

#define ALLOC(x) (struct x *) calloc(1, sizeof(struct x))

#define NLEFTS 512
#define INMAX 2048
#define ALLDEPS 1
#define SOMEDEPS 2

#define TERMINAL 1

struct nameblock {
    struct nameblock *nxtnameblock;
    char *namep;
    struct lineblock *linep;
    int done;
    int septype;
    time_t modtime;
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

// Globals
extern char *funny;

// Prototypes
void fatal(char *s);
void fatal1(char *s, char *a);
struct nameblock *srchname(char *name);
struct nameblock *makename(char *name);
void eqsign(char *s);
void subst(char *a, char *b, size_t max_len);
int unequal(char *a, char *b);
char *copys(char *s);

#endif
