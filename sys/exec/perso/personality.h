#ifndef _EXEC_PERSO_H
#define _EXEC_PERSO_H

#include <stdint.h>

// Argument Types for Tracing
#define ARG_HEX 0
#define ARG_INT 1
#define ARG_STR 2
#define ARG_PTR 3

struct syscall_fmt {
    int nargs;
    int arg_types[6];
};

struct personality {
    const char *name;
    void **syscall_table;
    const char **syscall_names;
    struct syscall_fmt *syscall_fmts;
    uint32_t syscall_count;
};

extern struct personality personality_native;
extern struct personality personality_freebsd;
extern struct personality personality_linux;
extern struct personality personality_svr3;
extern struct personality personality_svr4;

#endif
