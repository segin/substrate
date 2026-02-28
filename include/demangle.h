#ifndef SUBSTRATE_DEMANGLE_H
#define SUBSTRATE_DEMANGLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEMANGLE_NO_PARAMS  (1u << 0)
#define DEMANGLE_NO_VERBOSE (1u << 1)
#define DEMANGLE_TYPES      (1u << 2)

#define DEMANGLE_AUTO       (1u << 8)
#define DEMANGLE_ITANIUM    (1u << 9)
#define DEMANGLE_RUST       (1u << 10)
#define DEMANGLE_DLANG      (1u << 11)

char *demangle(const char *mangled, int options);
int demangle_buf(const char *mangled, char *buf, size_t bufsz, int options);
void demangle_free(char *str);
const char *demangle_version(void);

#ifdef __cplusplus
}
#endif

#endif /* SUBSTRATE_DEMANGLE_H */
