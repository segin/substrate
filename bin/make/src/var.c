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

#define APPEND_CHAR(c) do { \
    if (len >= max_len - 1) fatal("macro expansion exceeded maximum buffer size"); \
    *b++ = (c); \
    len++; \
} while (0)

char *subst(char *a, char *b, size_t max_len) {
    if (++depth > 100) fatal("infinitely recursive macro");
    
    char *start_b = b;
    size_t len = 0;
    if (!a || max_len == 0) {
        if (max_len > 0) *b = 0;
        depth--;
        return start_b;
    }

    while (*a) {
        if (*a == '$') {
            a++;
            if (*a == '$') {
                APPEND_CHAR('$');
                a++;
                continue;
            }
            char vname[100];
            char *v = vname;
            if (*a == '(') {
                a++;
                while (*a && *a != ')' && (v - vname) < 99) *v++ = *a++;
                if (*a == ')') a++;
            } else if (*a == '{') {
                a++;
                while (*a && *a != '}' && (v - vname) < 99) *v++ = *a++;
                if (*a == '}') a++;
            } else {
                if (*a && (v - vname) < 99) *v++ = *a++;
            }
            *v = 0;
            
            struct varblock *vp = varptr(vname);
            if (vp->varval) {
                // Recursive expansion
                char *expanded = malloc(OUTMAX);
                if (!expanded) fatal("malloc failed");
                subst(vp->varval, expanded, OUTMAX);
                char *e = expanded;
                while (*e) APPEND_CHAR(*e++);
                free(expanded);
            }
        } else {
            APPEND_CHAR(*a++);
        }
    }
    *b = 0;
    depth--;
    return start_b;
}