/*
 * dl.c — POSIX dlopen / dlsym / dlclose / dlerror / dladdr for Substrate.
 *
 * Thin wrappers over the loader API exported by /sbin/ld.so:
 *   __ldso_dlopen / __ldso_dlsym / __ldso_dlclose / __ldso_dlerror
 *   / __ldso_dladdr
 *
 * libdl deliberately doesn't reimplement loading — the live state
 * (loaded-object list, symbol scope, init/fini bookkeeping, error
 * string) all lives in ld.so, so we just forward.  Static-linked
 * binaries that pull libdl in without ld.so won't have the
 * __ldso_* symbols defined; the weak attribute makes the calls
 * return NULL/-1 rather than crashing.
 *
 * RTLD_NEXT semantics require the loader to know which DSO called
 * dlsym.  dlsym() captures __builtin_return_address(0) (= the
 * instruction immediately after the caller's `call dlsym`) and
 * forwards it to the loader as caller_pc.
 */

#include <dlfcn.h>

extern void *__ldso_dlopen (const char *path, int flags)            __attribute__((weak));
extern void *__ldso_dlsym  (void *handle, const char *name, void *caller_pc) __attribute__((weak));
extern int   __ldso_dlclose(void *handle)                           __attribute__((weak));
extern const char *__ldso_dlerror(void)                             __attribute__((weak));
extern int   __ldso_dladdr (const void *addr, Dl_info *info)        __attribute__((weak));

void *dlopen(const char *path, int flags) {
    if (__ldso_dlopen) return __ldso_dlopen(path, flags);
    return 0;
}

void *dlsym(void *handle, const char *name) {
    if (__ldso_dlsym) {
        /* Pass the address inside *this* function back to the
         * loader; ld.so will figure out which DSO the caller came
         * from by walking loaded-object ranges. */
        void *pc = __builtin_return_address(0);
        return __ldso_dlsym(handle, name, pc);
    }
    return 0;
}

int dlclose(void *handle) {
    if (__ldso_dlclose) return __ldso_dlclose(handle);
    return -1;
}

const char *dlerror(void) {
    if (__ldso_dlerror) return __ldso_dlerror();
    return 0;
}

int dladdr(const void *addr, Dl_info *info) {
    if (__ldso_dladdr) return __ldso_dladdr(addr, info);
    return 0;
}
