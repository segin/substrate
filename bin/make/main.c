#include "defs.h"

char *funny;

void fatal(char *s) {
    fprintf(stderr, "make: %s\n", s);
    exit(1);
}

void fatal1(char *s, char *a) {
    fprintf(stderr, "make: ");
    fprintf(stderr, s, a);
    fprintf(stderr, "\n");
    exit(1);
}

// Stubs for now
struct nameblock *srchname(char *name) { (void)name; return NULL; }
struct nameblock *makename(char *name) { 
    struct nameblock *nb = ALLOC(nameblock);
    nb->namep = copys(name);
    return nb;
}
void eqsign(char *s) { (void)s; }
void subst(char *a, char *b) { strcpy(b, a); }
int unequal(char *a, char *b) { return strcmp(a, b); }
char *copys(char *s) { return strdup(s); }

char **linesptr;
FILE *fin;

int main(int argc, char *argv[]) {
    // Stub
    printf("make: stub implementation\n");
    return 0;
}

