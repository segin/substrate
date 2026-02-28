#ifndef DEMANGLE_INTERNAL_H
#define DEMANGLE_INTERNAL_H

char *demangle_itanium(const char *mangled, int options);
char *demangle_rust(const char *mangled, int options);
char *demangle_dlang(const char *mangled, int options);

#endif /* DEMANGLE_INTERNAL_H */
