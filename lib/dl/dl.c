/*
 * dl.c — POSIX dlopen / dlsym / dlclose for Substrate.
 *
 * Thin wrappers over the loader API exported by /sbin/ld.so:
 *   __ldso_dlopen / __ldso_dlsym / __ldso_dlclose
 *
 * libdl deliberately doesn't reimplement loading — the live state
 * (loaded-object list, symbol scope, init/fini bookkeeping) all
 * lives in ld.so, so we just forward.  Static-linked binaries that
 * pull libdl in without ld.so won't have the __ldso_* symbols
 * defined; the weak attribute makes the calls return NULL/-1
 * rather than crashing.
 */

#include <dlfcn.h>

extern void *__ldso_dlopen(const char *path, int flags) __attribute__((weak));
extern void *__ldso_dlsym(void *handle, const char *name) __attribute__((weak));
extern int   __ldso_dlclose(void *handle) __attribute__((weak));

void *dlopen(const char *path, int flags) {
    if (__ldso_dlopen) return __ldso_dlopen(path, flags);
    return 0;
}

void *dlsym(void *handle, const char *name) {
    if (__ldso_dlsym) return __ldso_dlsym(handle, name);
    return 0;
}

int dlclose(void *handle) {
    if (__ldso_dlclose) return __ldso_dlclose(handle);
    return -1;
}

const char *dlerror(void) {
    /* Phase 4e: no error string buffer yet; callers check ret==NULL. */
    return 0;
}
