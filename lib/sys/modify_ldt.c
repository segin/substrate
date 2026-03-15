#include <sys/ldt.h>
#include <sys/syscall.h>
#include <unistd.h>

int modify_ldt(int func, void *ptr, unsigned long bytecount) {
    return (int)syscall(SYS_MODIFY_LDT, func, ptr, bytecount);
}
