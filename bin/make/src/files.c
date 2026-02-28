#include "defs.h"

struct nameblock *hashtab[512];

struct nameblock *srchname(char *name) {
    unsigned int hash = 0;
    char *p = name;
    while (*p) hash = (hash << 5) + *p++;
    hash %= 512;
    
    struct nameblock *nb = hashtab[hash];
    while (nb) {
        if (strcmp(nb->namep, name) == 0) return nb;
        nb = nb->nxtnameblock;
    }
    return NULL;
}

struct nameblock *makename(char *name) {
    struct nameblock *nb = ALLOC(nameblock);
    nb->namep = copys(name);
    
    unsigned int hash = 0;
    char *p = name;
    while (*p) hash = (hash << 5) + *p++;
    hash %= 512;
    
    nb->nxtnameblock = hashtab[hash];
    hashtab[hash] = nb;
    
    if (!mainname && name[0] != '.') mainname = nb;
    
    return nb;
}

char *copys(char *s) {
    return strdup(s);
}

char *concat(char *a, char *b, char *c) {
    strcpy(c, a);
    strcat(c, b);
    return c;
}

TIMETYPE exists(char *filename) {
    struct stat st;
    if (stat(filename, &st) < 0) return 0;
    return st.st_mtime;
}
