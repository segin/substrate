/*
 * dlmod.c - the shared object dlmain.c loads with dlopen(): a function,
 * data that needs a RELATIVE relocation, a constructor and destructor,
 * a reference back into libc, and a __thread variable (general-dynamic,
 * reached through __tls_get_addr with a 16-byte tls_index).
 */
#include <stdio.h>
#include <string.h>

static const char *const greeting = "hello from dlmod";
static int constructed;
__thread int dlmod_tls = 41;

__attribute__((constructor))
static void dlmod_init(void) {
    constructed = 1;
    printf("dlmod: constructor ran\n");
}

__attribute__((destructor))
static void dlmod_fini(void) {
    printf("dlmod: destructor ran\n");
}

int dlmod_add(int a, int b) {
    return a + b + (constructed ? 0 : 1000);
}

const char *dlmod_greeting(void) {
    return greeting;
}

unsigned long dlmod_strlen(const char *s) {
    return strlen(s);
}

int dlmod_tls_next(void) {
    return ++dlmod_tls;
}

int dlmod_value = 4242;
