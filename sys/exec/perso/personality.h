#ifndef _EXEC_PERSO_H
#define _EXEC_PERSO_H

#include <stdint.h>

struct personality {
    const char *name;
    void **syscall_table;
    uint32_t syscall_count;
};

extern struct personality personality_native;
extern struct personality personality_freebsd;
extern struct personality personality_linux;
extern struct personality personality_svr3;
extern struct personality personality_svr4;

#endif
