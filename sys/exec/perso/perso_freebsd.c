#include "personality.h"
#include "../../arch/i386/syscall.h"

extern int sys_exit(int);
extern int sys_write(int, const char*, int);
extern int sys_getpid(void);

static void *freebsd_syscalls[MAX_SYSCALLS] = {
    [1] = &sys_exit,
    [4] = &sys_write,
    [20] = &sys_getpid,
};

struct personality personality_freebsd = {
    .name = "FreeBSD",
    .syscall_table = freebsd_syscalls,
    .syscall_count = MAX_SYSCALLS
};