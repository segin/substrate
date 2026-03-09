#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <sys/proc.h>
#include <sys/ldt.h>
#include <sys/times.h>
#include <exec/formats/elks_aout.h>

process_t *current_process;
thread_t *current_thread;

static const char *last_name;
static uintptr_t last_ptr;
static int last_i0;
static int last_i1;
static char last_log[128];
static char last_exec_path[128];
static char last_exec_argv0[128];
static char last_exec_argv1[128];
static char last_exec_env0[128];
static int last_sigaction_sig;
static sig_t last_sigaction_handler;
static int last_kill_pid;
static int last_kill_sig;
static int last_sigexit_sig;
static struct sigaction stub_oldact;
static uint8_t *ds_mem;
static size_t ds_mem_size;

#define sys_exit   stub_sys_exit
#define sys_fork   stub_sys_fork
#define sys_read   stub_sys_read
#define sys_write  stub_sys_write
#define sys_open   stub_sys_open
#define sys_close  stub_sys_close
#define sys_waitpid stub_sys_waitpid
#define sys_creat  stub_sys_creat
#define sys_link   stub_sys_link
#define sys_unlink stub_sys_unlink
#define sys_execve stub_sys_execve
#define sys_chdir  stub_sys_chdir
#define sys_time   stub_sys_time
#define sys_mknod  stub_sys_mknod
#define sys_chmod  stub_sys_chmod
#define sys_lchown stub_sys_lchown
#define sys_lseek  stub_sys_lseek
#define sys_getpid stub_sys_getpid
#define sys_mount  stub_sys_mount
#define sys_umount stub_sys_umount
#define sys_setuid stub_sys_setuid
#define sys_getuid stub_sys_getuid
#define sys_stime  stub_sys_stime
#define sys_alarm  stub_sys_alarm
#define sys_fstat  stub_sys_fstat
#define sys_pause  stub_sys_pause
#define sys_access stub_sys_access
#define sys_sync   stub_sys_sync
#define sys_kill   stub_sys_kill
#define sys_mkdir  stub_sys_mkdir
#define sys_rmdir  stub_sys_rmdir
#define sys_dup    stub_sys_dup
#define sys_pipe   stub_sys_pipe
#define sys_times  stub_sys_times
#define sys_brk    stub_sys_brk
#define sys_setgid stub_sys_setgid
#define sys_getgid stub_sys_getgid
#define sys_signal stub_sys_signal
#define sys_ioctl  stub_sys_ioctl
#define sys_fcntl  stub_sys_fcntl
#define sys_umask  stub_sys_umask
#define sys_stat   stub_sys_stat
#define sys_dup2   stub_sys_dup2
#define sys_getppid stub_sys_getppid
#define sys_getpgrp stub_sys_getpgrp
#define kern_sigaction stub_kern_sigaction
#define sigexit stub_sigexit
#define kern_execve stub_kern_execve
#define kprint      stub_kprint

int stub_sys_exit(int a) { last_name = "exit"; last_i0 = a; return 11; }
int stub_sys_fork(void) { last_name = "fork"; return 12; }
int stub_sys_read(int a, char *b, int c) { last_name = "read"; last_i0 = a; last_ptr = (uintptr_t)b; last_i1 = c; return 22; }
int stub_sys_write(int a, const char *b, int c) { last_name = "write"; last_i0 = a; last_ptr = (uintptr_t)b; last_i1 = c; return 33; }
int stub_sys_open(const char *a, int b, int c) { last_name = "open"; last_ptr = (uintptr_t)a; last_i0 = b; last_i1 = c; return 44; }
int stub_sys_close(int a) { last_name = "close"; last_i0 = a; return 55; }
int stub_sys_waitpid(int a, int *b, int c) { last_name = "waitpid"; last_i0 = a; last_ptr = (uintptr_t)b; last_i1 = c; return 66; }
int stub_sys_creat(const char *a, int b) { (void)a; (void)b; return -1; }
int stub_sys_link(const char *a, const char *b) { (void)a; (void)b; return -1; }
int stub_sys_unlink(const char *a) { (void)a; return -1; }
int stub_sys_execve(const char *a, char **b, char **c) { (void)a; (void)b; (void)c; return -1; }
int stub_sys_chdir(const char *a) { (void)a; return -1; }
time_t stub_sys_time(time_t *a) { (void)a; return 0; }
int stub_sys_mknod(const char *a, int b, int c) { (void)a; (void)b; (void)c; return -1; }
int stub_sys_chmod(const char *a, int b) { (void)a; (void)b; return -1; }
int stub_sys_lchown(const char *a, int b, int c) { (void)a; (void)b; (void)c; return -1; }
int64_t stub_sys_lseek(int a, uint32_t b, uint32_t c, int d) { (void)a; (void)b; (void)c; (void)d; return -1; }
int stub_sys_getpid(void) { return -1; }
int stub_sys_mount(const char *a, const char *b, const char *c, unsigned long d, void *e) { (void)a; (void)b; (void)c; (void)d; (void)e; return -1; }
int stub_sys_umount(const char *a) { (void)a; return -1; }
int stub_sys_setuid(int a) { (void)a; return -1; }
int stub_sys_getuid(void) { return -1; }
int stub_sys_stime(time_t *a) { (void)a; return -1; }
unsigned int stub_sys_alarm(unsigned int a) { (void)a; return 0; }
int stub_sys_fstat(int a, void *b) { (void)a; (void)b; return -1; }
int stub_sys_pause(void) { return -1; }
int stub_sys_access(const char *a, int b) { (void)a; (void)b; return -1; }
int stub_sys_sync(void) { return 0; }
int stub_sys_kill(int a, int b) { last_kill_pid = a; last_kill_sig = b; return 99; }
int stub_sys_mkdir(const char *a, int b) { (void)a; (void)b; return -1; }
int stub_sys_rmdir(const char *a) { (void)a; return -1; }
int stub_sys_dup(int a) { (void)a; return -1; }
int stub_sys_pipe(int *a) { (void)a; return -1; }
clock_t stub_sys_times(struct tms *a) { (void)a; return 0; }
int stub_sys_brk(uint32_t a) { last_name = "brk"; last_ptr = a; return (int)a; }
int stub_sys_setgid(int a) { (void)a; return -1; }
int stub_sys_getgid(void) { return -1; }
int stub_sys_signal(int a, void *b) { (void)a; (void)b; return -1; }
int stub_sys_ioctl(int a, uint32_t b, void *c) { (void)a; (void)b; (void)c; return -1; }
int stub_sys_fcntl(int a, int b, int c) { (void)a; (void)b; (void)c; return -1; }
int stub_sys_umask(int a) { (void)a; return -1; }
int stub_sys_stat(const char *a, void *b) { (void)a; (void)b; return -1; }
int stub_sys_dup2(int a, int b) { (void)a; (void)b; return -1; }
int stub_sys_getppid(void) { return -1; }
int stub_sys_getpgrp(void) { return -1; }
int stub_kern_sigaction(int sig, const struct sigaction *act, struct sigaction *oact) {
    last_sigaction_sig = sig;
    last_sigaction_handler = act ? act->sa_handler : SIG_ERR;
    if (oact) {
        *oact = stub_oldact;
    }
    return 0;
}
void stub_sigexit(process_t *p, int sig) {
    (void)p;
    last_sigexit_sig = sig;
}
int stub_kern_execve(const char *path, char *const argv[], char *const envp[]) {
    last_name = "execve";
    strncpy(last_exec_path, path ? path : "", sizeof(last_exec_path) - 1);
    last_exec_path[sizeof(last_exec_path) - 1] = '\0';
    strncpy(last_exec_argv0, (argv && argv[0]) ? argv[0] : "", sizeof(last_exec_argv0) - 1);
    last_exec_argv0[sizeof(last_exec_argv0) - 1] = '\0';
    strncpy(last_exec_argv1, (argv && argv[1]) ? argv[1] : "", sizeof(last_exec_argv1) - 1);
    last_exec_argv1[sizeof(last_exec_argv1) - 1] = '\0';
    strncpy(last_exec_env0, (envp && envp[0]) ? envp[0] : "", sizeof(last_exec_env0) - 1);
    last_exec_env0[sizeof(last_exec_env0) - 1] = '\0';
    return 77;
}
void stub_kprint(const char *msg) {
    strncpy(last_log, msg ? msg : "", sizeof(last_log) - 1);
    last_log[sizeof(last_log) - 1] = '\0';
}
void *kmalloc(size_t size) { return malloc(size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

#include "../../sys/exec/perso/perso_elks.c"

static void setup_ds(process_t *proc, gdt_entry_t *ldt, size_t size) {
    uint32_t base;

    memset(proc, 0, sizeof(*proc));
    memset(ldt, 0, sizeof(gdt_entry_t) * 4);
    memset(ds_mem, 0, size);

    base = (uint32_t)(uintptr_t)ds_mem;

    ldt[ELKS_LDT_DS_INDEX].limit_low = (uint16_t)(size - 1U);
    ldt[ELKS_LDT_DS_INDEX].base_low = (uint16_t)(base & 0xFFFFU);
    ldt[ELKS_LDT_DS_INDEX].base_middle = (uint8_t)((base >> 16) & 0xFFU);
    ldt[ELKS_LDT_DS_INDEX].base_high = (uint8_t)((base >> 24) & 0xFFU);
    ldt[ELKS_LDT_DS_INDEX].access = 0xF2U;
    ldt[ELKS_LDT_DS_INDEX].granularity = 0x00U;

    ldt[ELKS_LDT_CS_INDEX].limit_low = (uint16_t)(size - 1U);
    ldt[ELKS_LDT_CS_INDEX].base_low = (uint16_t)(base & 0xFFFFU);
    ldt[ELKS_LDT_CS_INDEX].base_middle = (uint8_t)((base >> 16) & 0xFFU);
    ldt[ELKS_LDT_CS_INDEX].base_high = (uint8_t)((base >> 24) & 0xFFU);
    ldt[ELKS_LDT_CS_INDEX].access = 0xFAU;
    ldt[ELKS_LDT_CS_INDEX].granularity = 0x00U;

    ldt[ELKS_LDT_SS_INDEX].limit_low = (uint16_t)(size - 1U);
    ldt[ELKS_LDT_SS_INDEX].base_low = (uint16_t)(base & 0xFFFFU);
    ldt[ELKS_LDT_SS_INDEX].base_middle = (uint8_t)((base >> 16) & 0xFFU);
    ldt[ELKS_LDT_SS_INDEX].base_high = (uint8_t)((base >> 24) & 0xFFU);
    ldt[ELKS_LDT_SS_INDEX].access = 0xF2U;
    ldt[ELKS_LDT_SS_INDEX].granularity = 0x00U;

    proc->ldt = ldt;
    proc->ldt_entry_count = 4;
}

int main(void) {
    process_t proc;
    thread_t thread;
    gdt_entry_t ldt[4];
    int (*fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    uintptr_t ds_base;
    uint16_t cs_sel = (uint16_t)((ELKS_LDT_CS_INDEX << 3) | 4U | 3U);
    uint16_t ss_sel = (uint16_t)((ELKS_LDT_SS_INDEX << 3) | 4U | 3U);

    ds_mem_size = 4096;
#ifdef MAP_32BIT
    ds_mem = mmap(NULL, ds_mem_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
#else
    ds_mem = mmap(NULL, ds_mem_size, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    if (ds_mem == MAP_FAILED) {
        perror("mmap");
        return 1;
    }

    memset(&thread, 0, sizeof(thread));
    setup_ds(&proc, ldt, ds_mem_size);
    current_process = &proc;
    current_thread = &thread;
    elks_personality_init();
    ds_base = (uintptr_t)ds_mem;

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_exit];
    if (fn(7, 0, 0, 0, 0, 0, 0, 0) != 11 || strcmp(last_name, "exit") != 0 || last_i0 != 7) {
        fprintf(stderr, "FAIL: ELKS exit wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_read];
    if (fn(3, 0x20, 5, 0, 0, 0, 0, 0) != 22 || strcmp(last_name, "read") != 0 ||
        last_i0 != 3 || last_ptr != ds_base + 0x20U || last_i1 != 5) {
        fprintf(stderr, "FAIL: ELKS read wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_write];
    if (fn(4, 0x20, 5, 0, 0, 0, 0, 0) != 33 || strcmp(last_name, "write") != 0 ||
        last_i0 != 4 || last_ptr != ds_base + 0x20U || last_i1 != 5) {
        fprintf(stderr, "FAIL: ELKS write wrapper wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x40), "/tmp/file");
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_open];
    if (fn(0x40, 1, 2, 0, 0, 0, 0, 0) != 44 || strcmp(last_name, "open") != 0 ||
        last_ptr != ds_base + 0x40U || last_i0 != 1 || last_i1 != 2) {
        fprintf(stderr, "FAIL: ELKS open wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_close];
    if (fn(9, 0, 0, 0, 0, 0, 0, 0) != 55 || strcmp(last_name, "close") != 0 || last_i0 != 9) {
        fprintf(stderr, "FAIL: ELKS close wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_waitpid];
    if (fn(12, 0x30, 7, 0, 0, 0, 0, 0) != 66 || strcmp(last_name, "waitpid") != 0 ||
        last_i0 != 12 || last_ptr != ds_base + 0x30U || last_i1 != 7) {
        fprintf(stderr, "FAIL: ELKS waitpid wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_brk];
    if (fn(0x60, 0, 0, 0, 0, 0, 0, 0) != 0x60 || strcmp(last_name, "brk") != 0 ||
        last_ptr != ds_base + 0x60U) {
        fprintf(stderr, "FAIL: ELKS brk wrapper wrong\n");
        return 1;
    }
    if (fn(0x2000, 0, 0, 0, 0, 0, 0, 0) != -ENOMEM) {
        fprintf(stderr, "FAIL: ELKS brk bounds check wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_fork];
    if (fn(0, 0, 0, 0, 0, 0, 0, 0) != 12 || strcmp(last_name, "fork") != 0) {
        fprintf(stderr, "FAIL: ELKS fork wrapper wrong\n");
        return 1;
    }

    strcpy((char *)(ds_mem + 0x20), "/bin/sh");
    {
        uint16_t *stack = (uint16_t *)(void *)(ds_mem + 0x80);
        char *strings = (char *)(ds_mem + 0x80 + 12);

        stack[0] = 2;
        stack[1] = 12;
        stack[2] = 15;
        stack[3] = 0;
        stack[4] = 18;
        stack[5] = 0;

        strcpy(strings + 0, "sh");
        strcpy(strings + 3, "-c");
        strcpy(strings + 6, "TERM=ansi");
    }
    memset(last_exec_path, 0, sizeof(last_exec_path));
    memset(last_exec_argv0, 0, sizeof(last_exec_argv0));
    memset(last_exec_argv1, 0, sizeof(last_exec_argv1));
    memset(last_exec_env0, 0, sizeof(last_exec_env0));
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_execve];
    if (fn(0x20, 0x80, 32, 0, 0, 0, 0, 0) != 77 || strcmp(last_name, "execve") != 0) {
        fprintf(stderr, "FAIL: ELKS execve wrapper wrong return path\n");
        return 1;
    }
    if (strcmp(last_exec_path, "/bin/sh") != 0 ||
        strcmp(last_exec_argv0, "sh") != 0 ||
        strcmp(last_exec_argv1, "-c") != 0 ||
        strcmp(last_exec_env0, "TERM=ansi") != 0) {
        fprintf(stderr, "FAIL: ELKS execve stack decoding wrong\n");
        return 1;
    }

    last_kill_pid = -1;
    last_kill_sig = -1;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_kill];
    if (fn(77, 2, 0, 0, 0, 0, 0, 0) != 99 ||
        last_kill_pid != 77 || last_kill_sig != SIGINT) {
        fprintf(stderr, "FAIL: ELKS kill translation wrong\n");
        return 1;
    }
    if (fn(77, 99, 0, 0, 0, 0, 0, 0) != -EINVAL) {
        fprintf(stderr, "FAIL: ELKS kill invalid signal translation wrong\n");
        return 1;
    }

    stub_oldact.sa_handler = SIG_DFL;
    last_sigaction_sig = -1;
    last_sigaction_handler = SIG_ERR;
    fn = (void *)personality_elks.syscall_table[ELKS_SYS_signal];
    if (fn(2, 0, 0, 0, 0, 0, 0, 0) != 0 ||
        last_sigaction_sig != SIGINT ||
        last_sigaction_handler != SIG_DFL) {
        fprintf(stderr, "FAIL: ELKS signal(SIG_DFL) translation wrong\n");
        return 1;
    }

    stub_oldact.sa_handler = SIG_IGN;
    last_sigaction_sig = -1;
    last_sigaction_handler = SIG_ERR;
    if (fn(3, 1, 0, 0, 0, 0, 0, 0) != 1 ||
        last_sigaction_sig != SIGQUIT ||
        last_sigaction_handler != SIG_IGN) {
        fprintf(stderr, "FAIL: ELKS signal(SIG_IGN) translation wrong\n");
        return 1;
    }

    stub_oldact.sa_handler = (sig_t)(uintptr_t)(ds_base + 0x88U);
    last_sigaction_sig = -1;
    last_sigaction_handler = SIG_ERR;
    if (fn(14, 0x44, cs_sel, 0, 0, 0, 0, 0) != 2 ||
        last_sigaction_sig != SIGALRM ||
        (uintptr_t)last_sigaction_handler != ds_base + 0x44U) {
        fprintf(stderr, "FAIL: ELKS custom signal handler translation wrong\n");
        return 1;
    }

    if (fn(16, 0, 0, 0, 0, 0, 0, 0) != 2) {
        fprintf(stderr, "FAIL: ELKS SIGURG disposition translation wrong\n");
        return 1;
    }

    {
        registers_t regs;
        uint16_t *words;

        memset(&regs, 0, sizeof(regs));
        memset(ds_mem + 0x180, 0, 16);
        last_sigexit_sig = 0;
        regs.eip = 0x1234;
        regs.cs = cs_sel;
        regs.useresp = 0x0186;
        regs.ss = ss_sel;
        regs.eflags = 0x00000602U | (1U << 10);

        personality_elks.sendsig((void *)(uintptr_t)(ds_base + 0x0044U), 14, 0, 0, &regs);
        if (last_sigexit_sig != 0) {
            fprintf(stderr, "FAIL: ELKS sendsig faulted\n");
            return 1;
        }
        if (regs.eip != 0x0044U || regs.cs != cs_sel || regs.useresp != 0x0180U) {
            fprintf(stderr, "FAIL: ELKS sendsig register rewrite wrong\n");
            return 1;
        }
        if (regs.eflags & (1U << 10)) {
            fprintf(stderr, "FAIL: ELKS sendsig did not clear DF\n");
            return 1;
        }

        words = (uint16_t *)(void *)(ds_mem + 0x0180);
        if (words[0] != 0x1234U || words[1] != cs_sel || words[2] != 14U) {
            fprintf(stderr, "FAIL: ELKS sendsig frame wrong\n");
            return 1;
        }
    }

    current_thread->syscall_num = 127;
    memset(last_log, 0, sizeof(last_log));
    fn = (void *)personality_elks.syscall_table[127];
    if (fn(0, 0, 0, 0, 0, 0, 0, 0) != -ENOSYS) {
        fprintf(stderr, "FAIL: ELKS unsupported syscall did not return ENOSYS\n");
        return 1;
    }
    if (strstr(last_log, "unsupported syscall") == NULL) {
        fprintf(stderr, "FAIL: ELKS unsupported syscall did not log\n");
        return 1;
    }

    munmap(ds_mem, ds_mem_size);
    puts("host_test_perso_elks: ok");
    return 0;
}
