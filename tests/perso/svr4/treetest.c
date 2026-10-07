/*
 * treetest.c - two things a System V Release 4 program takes for granted,
 * under substrate's SVR4 personality.  Freestanding, like ptytest.c; build
 * and run it the same way (see README.md), from under /perso/svr4/tmp.
 *
 *   - Address 0 can be read, and reads as zeroes; it cannot be written.
 *     The C compiler and assembler of the period look through null
 *     pointers and depend on it.
 *   - The program works in its own tree: a name created in a directory
 *     that exists under /perso/svr4 lands there, and chdir(2) to such a
 *     directory goes there, so that a relative name afterwards is the same
 *     file as the absolute one.
 *
 * It prints a line per check and "treetest: PASS" or "treetest: FAIL".
 */

#define SYS_exit    1
#define SYS_fork    2
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_wait    7
#define SYS_unlink  10
#define SYS_chdir   12
#define SYS_rmdir   79
#define SYS_mkdir   80
#define SYS_xstat   123

#define O_WRONLY    1
#define O_CREAT     0x100
#define STAT_VER    2

long sys_edx;

__asm__(
    ".text\n"
    ".globl sc\n"
    "sc:\n"
    "    pop %edx\n"
    "    pop %eax\n"
    "    push %edx\n"
    "    lcall $7,$0\n"
    "    jnc 1f\n"
    "    neg %eax\n"
    "1:  mov %edx,sys_edx\n"
    "    pop %edx\n"
    "    sub $4,%esp\n"
    "    jmp *%edx\n");
long sc(int nr, ...);

static int failures;

static int len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void put(const char *s) { sc(SYS_write, 1, s, len(s)); }

static void putn(long v) {
    char b[16];
    int i = 15;
    unsigned long u = v < 0 ? -v : v;

    b[i] = 0;
    do { b[--i] = '0' + u % 10; u /= 10; } while (u);
    if (v < 0) b[--i] = '-';
    put(b + i);
}

static void check(const char *what, long got, int ok) {
    put(ok ? "  ok    " : "  FAIL  ");
    put(what);
    put(" = ");
    putn(got);
    put("\n");
    if (!ok) failures++;
}

void _start(void) {
    static int stat_buf[40];
    volatile unsigned char *null = 0;
    unsigned long sum = 0;
    long pid, n, fd;
    int i;

    put("treetest: page zero, and the personality's tree\n");

    /* Writing there is a fault: the child dies of SIGSEGV (11). */
    pid = sc(SYS_fork);
    if (pid >= 0 && sys_edx) {
        null[0] = 1;
        sc(SYS_exit, 0);
    }
    n = sc(SYS_wait);
    check("a write to address 0 ends in signal", sys_edx & 0x7f,
          n == pid && (sys_edx & 0x7f) == 11);

    for (i = 0; i < 4096; i++) {
        sum += null[i];
    }
    check("the 4096 bytes at address 0, summed", (long)sum, sum == 0);

    /* And still a fault once the page has been read and is in the page
     * table: read-only because the mapping says so, not because it is
     * waiting to be copied. */
    pid = sc(SYS_fork);
    if (pid >= 0 && sys_edx) {
        null[0] = 1;
        sc(SYS_exit, 0);
    }
    n = sc(SYS_wait);
    check("a write after the read ends in signal", sys_edx & 0x7f,
          n == pid && (sys_edx & 0x7f) == 11);

    /* /tmp is this program's own directory under /perso/svr4, so a
     * directory made in it is made there, and can be entered. */
    sc(SYS_unlink, "/tmp/treetest.d/f");
    sc(SYS_rmdir, "/tmp/treetest.d");
    n = sc(SYS_mkdir, "/tmp/treetest.d", 0755);
    check("mkdir /tmp/treetest.d", n, n == 0);
    n = sc(SYS_xstat, STAT_VER, "/perso/svr4/tmp/treetest.d", stat_buf);
    check("it is under /perso/svr4", n, n == 0);
    n = sc(SYS_chdir, "/tmp/treetest.d");
    check("chdir /tmp/treetest.d", n, n == 0);
    fd = sc(SYS_open, "f", O_WRONLY | O_CREAT, 0644);
    check("create f, a relative name", fd, fd >= 0);
    if (fd >= 0) {
        sc(SYS_close, fd);
    }
    n = sc(SYS_xstat, STAT_VER, "/tmp/treetest.d/f", stat_buf);
    check("it is /tmp/treetest.d/f", n, n == 0);
    n = sc(SYS_xstat, STAT_VER, "../treetest.d/f", stat_buf);
    check("and ../treetest.d/f", n, n == 0);
    n = sc(SYS_xstat, STAT_VER, "/perso/svr4/tmp/treetest.d/f", stat_buf);
    check("and under /perso/svr4", n, n == 0);

    sc(SYS_chdir, "/");
    sc(SYS_unlink, "/tmp/treetest.d/f");
    n = sc(SYS_rmdir, "/tmp/treetest.d");
    check("rmdir", n, n == 0);

    put(failures ? "treetest: FAIL\n" : "treetest: PASS\n");
    sc(SYS_exit, failures ? 1 : 0);
}
