/*
 * rtldnext.c - dlsym(RTLD_NEXT) under /sbin/ld64.so.  libld64next.so
 * (nextmod.c) interposes atoi() and forwards to the next definition.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

extern int   nextmod_calls;
extern void *nextmod_lookup_missing(void);

int main(void) {
    int bad = 0;

    int v = atoi("42");
    printf("atoi(\"42\")=%d interposer calls=%d\n", v, nextmod_calls);
    if (v != 1042 || nextmod_calls != 1) bad = 1;

    /* From the executable, the next definition is the interposer.  It is
     * recognised by what it does, not by its address: in a non-PIE
     * program &atoi is the program's own PLT entry. */
    int (*next)(const char *) = (int (*)(const char *))dlsym(RTLD_NEXT, "atoi");
    int w = next ? next("5") : -1;
    printf("RTLD_NEXT from the program: atoi(\"5\")=%d interposer calls=%d\n",
           w, nextmod_calls);
    if (w != 1005 || nextmod_calls != 2) bad = 1;

    if (nextmod_lookup_missing() != NULL || !dlerror()) {
        printf("RTLD_NEXT found a name nothing later defines\n");
        bad = 1;
    }

    printf("rtldnext: %s\n", bad ? "FAIL" : "OK");
    return bad;
}
