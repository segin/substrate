/*
 * ldtfork — an LDT survives fork, and processes without one switch in
 * between.
 *
 * Sets LDT entry 0 over a variable, reads it back through %fs, then forks
 * children that read it through the copied LDT while a process with no
 * LDT (exec of /bin/true) runs between each.  Covers ldt_clone_process
 * and the switch to a process without an LDT, which on x86_64 now also
 * clears the GDT's LDT descriptor.
 *
 * Linux build (modify_ldt); i386 userland.
 */
#define _GNU_SOURCE
#include <asm/ldt.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static volatile unsigned int cell = 0x1dc0ffee;

static unsigned int read_fs0(void)
{
    unsigned int v;
    __asm__ volatile("movw %w1, %%fs\n\tmovl %%fs:0, %0" : "=r"(v) : "r"(7) : "memory");
    return v;
}

int main(void)
{
    struct user_desc d;
    int fails = 0;

    memset(&d, 0, sizeof(d));
    d.entry_number = 0;
    d.base_addr = (unsigned int)(unsigned long)&cell;
    d.limit = 0xfff;
    d.seg_32bit = 1;
    if (syscall(SYS_modify_ldt, 1, &d, sizeof(d)) != 0) {
        printf("ldtfork: modify_ldt failed: FAIL\n");
        return 1;
    }
    if (read_fs0() != cell) {
        printf("ldtfork: %%fs:0 does not read the cell: FAIL\n");
        return 1;
    }

    for (int i = 0; i < 40; i++) {
        pid_t t = fork();
        if (t == 0) {
            execl("/bin/true", "true", (char *)NULL);
            _exit(0);
        }
        pid_t c = fork();
        if (c == 0) {
            _exit(read_fs0() == 0x1dc0ffee ? 0 : 1);
        }
        int st = 0, st2 = 0;
        waitpid(c, &st, 0);
        waitpid(t, &st2, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
            fails++;
    }
    if (read_fs0() != cell)
        fails++;
    printf("ldtfork: %d of 40 children lost the LDT: %s\n", fails,
           fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
