/*
 * dlfcn.h — POSIX dynamic-loading interface for Substrate.
 *
 * The actual loader lives in /sbin/ld.so; libdl forwards each
 * call into the linker's __ldso_dl* exports.  Without ld.so
 * loaded (static-linked binary), the wrappers degrade gracefully
 * to NULL-return / -1-return.
 */

#ifndef _DLFCN_H
#define _DLFCN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Mode flags accepted by dlopen(3). */
#define RTLD_LAZY       0x0001
#define RTLD_NOW        0x0002
#define RTLD_GLOBAL     0x0100
#define RTLD_LOCAL      0x0000

/* Special handles for dlsym(3). */
#define RTLD_DEFAULT    ((void *)0)
#define RTLD_NEXT       ((void *)-1)

void *dlopen(const char *path, int flags);
void *dlsym(void *handle, const char *name);
int   dlclose(void *handle);
const char *dlerror(void);

/* GNU extension: identify the DSO + nearest symbol covering `addr`.
 * Returns non-zero on success, 0 if `addr` is not in any loaded DSO.
 * On success info->dli_fname / fbase are always populated; sname /
 * saddr are only set when a symbol's [value, value+size) range
 * contains `addr` (otherwise NULL/0). */
typedef struct {
    const char *dli_fname;
    void       *dli_fbase;
    const char *dli_sname;
    void       *dli_saddr;
} Dl_info;

int dladdr(const void *addr, Dl_info *info);

#ifdef __cplusplus
}
#endif

#endif /* _DLFCN_H */
