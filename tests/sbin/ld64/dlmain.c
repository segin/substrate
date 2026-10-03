/*
 * dlmain.c - dlopen / dlsym / dlclose / dlerror / dladdr of a 64-bit
 * shared object (libld64mod.so, built from dlmod.c) through libdl.so.0
 * and /sbin/ld64.so.
 */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "libld64mod.so";
    int bad = 0;

    /* A failure must be reported, not crash. */
    if (dlopen("libld64-no-such-object.so", RTLD_NOW) != NULL) bad = 1;
    const char *err = dlerror();
    printf("dlopen(missing): %s\n", err ? err : "(no error)");
    if (!err) bad = 1;

    /* A 32-bit object is refused, not mapped: /lib holds the i386
     * libraries of the same root. */
    if (dlopen("/lib/libsys.so.0", RTLD_NOW) != NULL) {
        printf("dlopen of a 32-bit object succeeded\n");
        bad = 1;
    }
    err = dlerror();
    printf("dlopen(32-bit): %s\n", err ? err : "(no error)");

    void *h = dlopen(path, RTLD_NOW);
    if (!h) {
        printf("dlopen(%s) failed: %s\n", path, dlerror());
        return 1;
    }
    printf("dlopen(%s) ok\n", path);

    int (*add)(int, int) = (int (*)(int, int))dlsym(h, "dlmod_add");
    const char *(*greet)(void) = (const char *(*)(void))dlsym(h, "dlmod_greeting");
    unsigned long (*len)(const char *) =
        (unsigned long (*)(const char *))dlsym(h, "dlmod_strlen");
    int (*tls_next)(void) = (int (*)(void))dlsym(h, "dlmod_tls_next");
    int *value = (int *)dlsym(h, "dlmod_value");
    if (!add || !greet || !len || !tls_next || !value) {
        printf("dlsym failed: %s\n", dlerror());
        return 1;
    }
    printf("dlmod_add(40, 2)=%d\n", add(40, 2));
    printf("dlmod_greeting()=%s\n", greet());
    printf("dlmod_strlen(\"dynamic\")=%lu\n", len("dynamic"));
    printf("dlmod_value=%d\n", *value);
    int t1 = tls_next(), t2 = tls_next();
    printf("dlmod_tls_next()=%d,%d\n", t1, t2);
    if (add(40, 2) != 42 || strcmp(greet(), "hello from dlmod") != 0 ||
        len("dynamic") != 7 || *value != 4242 || t1 != 42 || t2 != 43)
        bad = 1;

    /* The whole-scope lookup finds libc; a missing name is an error. */
    if (dlsym(RTLD_DEFAULT, "printf") != (void *)printf) {
        printf("dlsym(RTLD_DEFAULT, printf) mismatch\n");
        bad = 1;
    }
    if (dlsym(h, "dlmod_no_such_symbol") != NULL || !dlerror()) bad = 1;

    Dl_info info;
    if (dladdr((void *)add, &info) && info.dli_sname)
        printf("dladdr: %s in %s\n", info.dli_sname, info.dli_fname);
    else
        bad = 1;

    if (dlclose(h) != 0) {
        printf("dlclose failed: %s\n", dlerror());
        bad = 1;
    }
    printf("dl: %s\n", bad ? "FAIL" : "OK");
    return bad;
}
