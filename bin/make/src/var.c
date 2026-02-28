#include "defs.h"

struct varblock *firstvar = NULL;

struct varblock *varptr(char *name) {
    struct varblock *vp;
    for (vp = firstvar; vp; vp = vp->nxtvarblock) {
        if (strcmp(vp->varname, name) == 0) return vp;
    }
    vp = ALLOC(varblock);
    vp->nxtvarblock = firstvar;
    firstvar = vp;
    vp->varname = copys(name);
    vp->varval = NULL;
    return vp;
}

void setvar(char *name, char *val) {
    struct varblock *vp = varptr(name);
    if (vp->varval) free(vp->varval);
    vp->varval = copys(val);
}

static int depth = 0;

char *subst(char *a, char *b) {
    if (++depth > 100) fatal("infinitely recursive macro");
    
    char *start_b = b;
    if (!a) { *b = 0; depth--; return start_b; }

    while (*a) {
        if (*a == '$') {
            a++;
            if (*a == '$') {
                *b++ = '$';
                a++;
                continue;
            }
            char vname[100];
            char *v = vname;
            if (*a == '(') {
                a++;
                while (*a && *a != ')') *v++ = *a++;
                if (*a == ')') a++;
            } else if (*a == '{') {
                a++;
                while (*a && *a != '}') *v++ = *a++;
                if (*a == '}') a++;
            } else {
                if (*a) *v++ = *a++;
            }
            *v = 0;
            
            struct varblock *vp = varptr(vname);
            if (vp->varval) {
                // Recursive expansion
                char *expanded = malloc(OUTMAX);
                subst(vp->varval, expanded);
                char *e = expanded;
                while (*e) *b++ = *e++;
                free(expanded);
            }
        } else {
            *b++ = *a++;
        }
    }
    *b = 0;
    depth--;
    return start_b;
}