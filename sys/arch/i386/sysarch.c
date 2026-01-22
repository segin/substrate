#include <sys/types.h>
#include <sys/sysarch.h>
#include <errno.h>
#include <kern/console.h>

extern int vm86_init_bsd(void *args);

int sys_sysarch(int op, void *parms) {
    switch (op) {
        case I386_VM86:
            return vm86_init_bsd(parms);
        default:
            kprint("sysarch: unknown op\n");
            return -EINVAL;
    }
}
