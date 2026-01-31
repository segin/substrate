#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/vm86.h>
#include <sys/mman.h>
#include <errno.h>

// Direct syscall invocation for testing until libc wrapper is ready
// Syscall 113 (Linux vm86) - based on our implementation plan/Linux standard
#define SYS_vm86 113

// Macros for syscall invocation (i386)
long syscall1(int number, void *arg1) {
    long ret;
    __asm__ volatile ("int $0x80"
        : "=a" (ret)
        : "0" (number), "b" (arg1)
        : "memory");
    return ret;
}

int main(void) {
    printf("VM86 Test Program\n");

    // 1. Map memory at 0x0000 (1MB Identity Map)
    // We need at least the first page for IVT
    void *ptr = mmap((void*)0, 0x100000, PROT_READ | PROT_WRITE | PROT_EXEC, 
                     MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    
    if (ptr == MAP_FAILED) {
        printf("mmap failed: %d\n", errno);
        return 1;
    }
    printf("Mapped 1MB at 0x%p\n", ptr);
    
    // 2. Setup Real Mode Code
    // We'll write code at 0x1000:0000 (Linear 0x10000)
    // Code:
    //   mov ax, 0x1234
    //   int 0x21    ; Trigger GPF (which we emulate in kernel or monitor)
    //   mov bx, 0x5678
    //   hlt         ; Trigger GPF (Privileged) -> Exit
    
    unsigned char code[] = {
        0xB8, 0x34, 0x12,       // mov ax, 0x1234
        0xCD, 0x21,             // int 0x21
        0xBB, 0x78, 0x56,       // mov bx, 0x5678
        0xF4                    // hlt
    };
    
    unsigned char *dest = (unsigned char*)0x10000;
    memcpy(dest, code, sizeof(code));
    
    // 3. Setup VM86 Context
    struct vm86_struct info;
    memset(&info, 0, sizeof(info));
    
    info.regs.eip = 0x0000;
    info.regs.cs  = 0x1000;
    info.regs.ss  = 0x1000;
    info.regs.esp = 0xFFFE; // Top of segment
    info.regs.ds  = 0x1000;
    info.regs.es  = 0x1000;
    info.regs.fs  = 0x1000;
    info.regs.gs  = 0x1000;
    info.regs.eflags = 0x20000 | 0x200; // VM | IF
    
    // 4. Call VM86
    printf("Entering VM86 mode...\n");
    int ret = syscall1(SYS_vm86, &info);
    
    // 5. Check Result
    // VM86 syscall returns when an unhandled GPF/Interrupt occurs
    // My implementation: sys_vm86 doesn't return (it jumps).
    // The *handler* (vm86_gpf_handler) handles specific opcodes.
    // If it handles INT 0x21, it should resume.
    // The `hlt` instruction is NOT handled by my handler stub, so it should trigger a GPF -> Monitor?
    // Wait, my `vm86_gpf_handler` returns if it emulates.
    // It prints "VM86: Unhandled opcode" if not 0xCD.
    // If unhandled, it should probably KILL the process or SIGNAL it.
    // Currently it just returns (loops?).
    
    printf("VM86 returned: %d\n", ret);
    printf("EAX (from VM86): 0x%lx\n", info.regs.eax);
    
    return 0;
}
