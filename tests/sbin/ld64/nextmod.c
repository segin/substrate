/*
 * nextmod.c - libld64next.so, which interposes atoi() for rtldnext.c.
 * It is ahead of libc.so.0 in the lookup order, so the program's atoi
 * binds here; dlsym(RTLD_NEXT) from inside this object must then find
 * the next definition - libc's - and not this one again.
 */
#include <dlfcn.h>

int nextmod_calls;

int atoi(const char *s) {
    int (*real)(const char *) = (int (*)(const char *))dlsym(RTLD_NEXT, "atoi");
    nextmod_calls++;
    if (!real || real == atoi) return -1;
    return real(s) + 1000;
}

/* Nothing after this object defines the name. */
void *nextmod_lookup_missing(void) {
    return dlsym(RTLD_NEXT, "nextmod_calls");
}
