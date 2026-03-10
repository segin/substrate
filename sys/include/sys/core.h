#ifndef _SYS_CORE_H
#define _SYS_CORE_H

#include <stdint.h>
#include <sys/acct.h>

#define CORE_EXEC_PATH_MAX 256
#define CORE_SEGMENT_MAX 8

struct process;
struct registers;

struct core_user_regs {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp, esp;
    uint32_t ds, es, fs, gs;
    uint32_t eip, cs, eflags;
    uint32_t useresp, ss;
};

struct core_segment_desc {
    uint16_t selector;
    uint32_t base;
    uint32_t limit;
    uint8_t access;
    uint8_t granularity;
};

struct core_record {
    int valid;
    int pid;
    int signal;
    int trap_code;
    uintptr_t trap_addr;
    int perso_id;
    uint8_t bitness;
    char comm[AC_COMM_LEN];
    char exec_path[CORE_EXEC_PATH_MAX];
    int has_regs;
    struct core_user_regs regs;
    int segment_count;
    struct core_segment_desc segments[CORE_SEGMENT_MAX];
};

void core_prepare_dump(struct process *p, int sig);
void core_capture_trapframe(struct process *p, const struct registers *regs);
const struct core_record *core_last_record(void);
int coredump(struct process *p);

#endif
