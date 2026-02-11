#ifndef _EXEC_PERSO_H
#define _EXEC_PERSO_H

#include <stdint.h>

#ifndef MAX_SYSCALLS
#define MAX_SYSCALLS 600
#endif

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
    PERS_LINUX   = 3,
    PERS_SVR4    = 4,
    PERS_SVR3    = 5,
    PERS_SOLARIS = 6,
    PERS_FREEBSD = 9,
    PERS_NETBSD  = 2,
    PERS_OPENBSD = 12,
    /* Values >= 128 reserved for non-ELF personalities */
    PERS_SUNOS   = 129,
    PERS_MAX     = 256
};

struct personality {
    const char *name;
    enum personality_type id;
    void **syscall_table;
    const char **syscall_names;
    struct syscall_fmt *syscall_fmts;
    uint32_t syscall_count;

    /* Signal hooks */
    void (*sendsig)(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
    int (*sigreturn)(void *regs);
    int (*rt_sigreturn)(void *regs);
};

extern struct personality personality_native;
extern struct personality personality_freebsd;
extern struct personality personality_linux;
extern struct personality personality_svr3;
extern struct personality personality_svr4;
extern struct personality personality_netbsd;
extern struct personality personality_openbsd;
extern struct personality personality_solaris;

struct personality *perso_lookup(int id);
const char *perso_name(int id);

#endif
