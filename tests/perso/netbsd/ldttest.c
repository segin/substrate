/*
 * ldttest.c - i386_set_ldt(2) and i386_get_ldt(2), as NetBSD/i386 has them.
 *
 * Built natively on NetBSD/i386 and run there first, then under substrate's
 * NetBSD personality (README.md): the two must print the same.
 *
 *     cc -o ldttest ldttest.c
 *
 * What is expected is what sys/arch/x86/x86/sys_machdep.c does
 * (x86_set_ldt1, x86_get_ldt1): slots below NLDT (17) are the system's and
 * refused with EINVAL; a program may install memory-segment descriptors;
 * a present one must be ring 3 (EACCES otherwise); gates are refused; an
 * empty descriptor is stored not-present; the call returns the first slot
 * set; and a child gets its parent's table.  A selector for an installed
 * data segment, loaded into %fs, addresses memory from the descriptor's
 * base -- which is what the table is for.
 *
 * Prints a line per check and "ldttest: PASS" or "FAIL".
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* <x86/sysarch.h>, spelled out so that the test says what it sends. */
#define X86_GET_LDT 0
#define X86_SET_LDT 1
struct ldt_args {
    int   start;
    void *desc;
    int   num;
};
int sysarch(int, void *);

#define NLDT        17
#define SLOT        (NLDT + 3)
#define LSEL(s)     (((s) << 3) | 4 | 3)    /* local table, ring 3 */

/* Descriptor types (<i386/segments.h>), with the S bit. */
#define SDT_SYS386CGT 12                    /* a call gate */
#define SDT_MEMRWA    19                    /* read/write data, accessed */
#define SDT_ACCESSED  1

static int fails;

static void check(const char *what, long got, int ok) {
    printf("  %s  %s = %ld\n", ok ? "ok  " : "FAIL", what, got);
    if (!ok) fails++;
}

/* A byte-granular segment of `limit`+1 bytes at `base`. */
static void make(unsigned char *d, unsigned long base, unsigned long limit,
                 int type, int dpl, int present) {
    d[0] = limit & 0xff;
    d[1] = (limit >> 8) & 0xff;
    d[2] = base & 0xff;
    d[3] = (base >> 8) & 0xff;
    d[4] = (base >> 16) & 0xff;
    d[5] = (unsigned char)((present ? 0x80 : 0) | (dpl << 5) | type);
    d[6] = (unsigned char)(0x40 | ((limit >> 16) & 0x0f));   /* 32-bit */
    d[7] = (base >> 24) & 0xff;
}

static int set_ldt(int start, unsigned char *d, int num) {
    struct ldt_args a;

    a.start = start; a.desc = d; a.num = num;
    return sysarch(X86_SET_LDT, &a);
}

static int get_ldt(int start, unsigned char *d, int num) {
    struct ldt_args a;

    a.start = start; a.desc = d; a.num = num;
    return sysarch(X86_GET_LDT, &a);
}

static unsigned long through_fs(unsigned short sel) {
    unsigned long v;

    __asm__ volatile("movw %0, %%fs; movl %%fs:0, %1" : : "r"(sel), "r"(0UL));
    __asm__ volatile("movl %%fs:0, %0" : "=r"(v));
    return v;
}

int main(void) {
    static unsigned long block[4] = { 0x11223344UL, 2, 3, 4 };
    unsigned char d[8], back[16];
    pid_t pid;
    int r, st;

    setvbuf(stdout, NULL, _IONBF, 0);
    make(d, (unsigned long)block, sizeof(block) - 1, SDT_MEMRWA, 3, 1);

    errno = 0;
    r = set_ldt(NLDT - 1, d, 1);
    check("a slot below NLDT: EINVAL", errno, r == -1 && errno == EINVAL);

    r = set_ldt(SLOT, d, 1);
    check("a ring-3 data segment: the slot comes back", r, r == SLOT);
    if (r != SLOT) {
        /* Nothing below can be asked of a kernel that will not have it:
         * on NetBSD, EPERM until `sysctl -w machdep.user_ldt=1`. */
        printf("  i386_set_ldt: %s\nldttest: FAIL\n", strerror(errno));
        return 1;
    }

    check("%fs:0 through its selector",
          (long)through_fs(LSEL(SLOT)), through_fs(LSEL(SLOT)) == block[0]);
    block[0] = 0x55667788UL;
    check("  and a new value stored there",
          (long)through_fs(LSEL(SLOT)), through_fs(LSEL(SLOT)) == block[0]);

    memset(back, 0, sizeof(back));
    r = get_ldt(SLOT, back, 1);
    back[5] &= (unsigned char)~SDT_ACCESSED;
    d[5] &= (unsigned char)~SDT_ACCESSED;
    check("get_ldt: one descriptor", r, r == 1);
    check("  the one that was set", back[2] | (back[5] << 8),
          memcmp(back, d, 8) == 0);
    d[5] |= SDT_ACCESSED;

    make(back, (unsigned long)block, 15, SDT_MEMRWA, 0, 1);
    errno = 0;
    r = set_ldt(SLOT + 1, back, 1);
    check("a present ring-0 segment: EACCES", errno,
          r == -1 && errno == EACCES);

    make(back, 0, 0, SDT_SYS386CGT, 3, 1);
    errno = 0;
    r = set_ldt(SLOT + 1, back, 1);
    check("a call gate: EACCES", errno, r == -1 && errno == EACCES);

    memset(back, 0, 8);
    back[5] = 0x80;                 /* type 0, marked present */
    r = set_ldt(SLOT + 2, back, 1);
    check("an empty descriptor is taken", r, r == SLOT + 2);
    memset(back, 0xff, 8);
    get_ldt(SLOT + 2, back, 1);
    check("  and stored not present", back[5] & 0x80, (back[5] & 0x80) == 0);

    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        _exit(through_fs(LSEL(SLOT)) == block[0] ? 0 : 1);
    }
    waitpid(pid, &st, 0);
    check("a forked child has the table", st,
          WIFEXITED(st) && WEXITSTATUS(st) == 0);

    printf("ldttest: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
