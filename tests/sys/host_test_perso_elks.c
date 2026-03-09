#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

int stub_sys_exit(int a) { last_name = "exit"; last_i0 = a; return 11; }
int stub_sys_fork(void) { return -1; }
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
int stub_sys_kill(int a, int b) { (void)a; (void)b; return -1; }
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

#include "../../sys/exec/perso/perso_elks.c"

static void setup_ds(process_t *proc, gdt_entry_t *ldt, size_t size) {
    memset(proc, 0, sizeof(*proc));
    memset(ldt, 0, sizeof(gdt_entry_t) * 4);

    ldt[ELKS_LDT_DS_INDEX].limit_low = (uint16_t)(size - 1U);
    ldt[ELKS_LDT_DS_INDEX].access = 0xF2U;
    ldt[ELKS_LDT_DS_INDEX].granularity = 0x00U;

    proc->ldt = ldt;
    proc->ldt_entry_count = 4;
}

int main(void) {
    process_t proc;
    gdt_entry_t ldt[4];
    int (*fn)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

    setup_ds(&proc, ldt, 256);
    current_process = &proc;
    current_thread = NULL;

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_exit];
    if (fn(7, 0, 0, 0, 0, 0, 0, 0) != 11 || strcmp(last_name, "exit") != 0 || last_i0 != 7) {
        fprintf(stderr, "FAIL: ELKS exit wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_read];
    if (fn(3, 0x20, 5, 0, 0, 0, 0, 0) != 22 || strcmp(last_name, "read") != 0 ||
        last_i0 != 3 || last_ptr != 0x20U || last_i1 != 5) {
        fprintf(stderr, "FAIL: ELKS read wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_write];
    if (fn(4, 0x20, 5, 0, 0, 0, 0, 0) != 33 || strcmp(last_name, "write") != 0 ||
        last_i0 != 4 || last_ptr != 0x20U || last_i1 != 5) {
        fprintf(stderr, "FAIL: ELKS write wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_open];
    if (fn(0x40, 1, 2, 0, 0, 0, 0, 0) != 44 || strcmp(last_name, "open") != 0 ||
        last_ptr != 0x40U || last_i0 != 1 || last_i1 != 2) {
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
        last_i0 != 12 || last_ptr != 0x30U || last_i1 != 7) {
        fprintf(stderr, "FAIL: ELKS waitpid wrapper wrong\n");
        return 1;
    }

    fn = (void *)personality_elks.syscall_table[ELKS_SYS_brk];
    if (fn(0x60, 0, 0, 0, 0, 0, 0, 0) != 0x60 || strcmp(last_name, "brk") != 0 || last_ptr != 0x60U) {
        fprintf(stderr, "FAIL: ELKS brk wrapper wrong\n");
        return 1;
    }
    if (fn(0x200, 0, 0, 0, 0, 0, 0, 0) != -ENOMEM) {
        fprintf(stderr, "FAIL: ELKS brk bounds check wrong\n");
        return 1;
    }

    puts("host_test_perso_elks: ok");
    return 0;
}
