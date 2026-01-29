#ifndef _EXEC_PERSO_H
#define _EXEC_PERSO_H

#include <stdint.h>

// Argument Types for Tracing
#define ARG_HEX  0
#define ARG_INT  1
#define ARG_STR  2
#define ARG_PTR  3
#define ARG_LONG 4  /* 64-bit value (consumes 2 slots on i386) */

struct syscall_fmt {
    int nargs;
    int arg_types[6];
};

enum personality_type {
    PERS_NATIVE  = 0,
    PERS_LINUX   = 1,
    PERS_FREEBSD = 2,
    PERS_NETBSD  = 3,
    PERS_OPENBSD = 4,
    PERS_SVR3    = 5,
    PERS_SVR4    = 6,
    PERS_SUNOS   = 7,
    PERS_MAX
};

struct personality {
    const char *name;
    enum personality_type id;
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
extern struct personality personality_netbsd;
extern struct personality personality_openbsd;
extern struct personality personality_sunos;

struct personality *perso_lookup(int id);
const char *perso_name(int id);

#endif
