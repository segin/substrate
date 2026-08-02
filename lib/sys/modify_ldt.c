#include <errno.h>
#include <unistd.h>

#include <sys/ldt.h>
#include <sys/syscall.h>
#include <sysret.h>

int modify_ldt(int func, void *ptr, unsigned long bytecount) {
    return (int)__sysret(syscall(SYS_MODIFY_LDT, func, ptr, bytecount));
}
