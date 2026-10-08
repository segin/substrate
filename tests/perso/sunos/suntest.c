/*
 * suntest.c - substrate's SunOS personality, checked from inside.
 *
 * A freestanding program that is a Sun386i one in every way the kernel
 * can tell: i386 COFF with its text in the second page, and SunOS 4.0's
 * system calls made as its libc makes them -- `int $0xff`, the number in
 * %eax, the arguments on the stack above a return address, the carry flag
 * and an error number for a failure.  It needs no SunOS file: not libc,
 * not ld.so, not the distribution.  build.sh beside this builds it and
 * README.md says how to run it; it has to run on substrate, from
 * /perso/sunos/tmp.
 *
 * What it checks is what the system's own programs were found to depend
 * on (docs/specs/personality_targets.md), each against the manual page or
 * header that says so: the second results in %edx, the BSD struct stat
 * and dirent, mmap(2) of /dev/zero and where it lands, brk(2), a signal
 * taken and returned from as _sigtramp does it, 4.3BSD's error numbers,
 * and files made in the personality's own tree.
 *
 * It prints a line per check and "suntest: PASS" or "suntest: FAIL".
 */

#define SYS_exit        1
#define SYS_fork        2
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_wait4       7
#define SYS_unlink      10
#define SYS_chdir       12
#define SYS_brk         17
#define SYS_getpid      20
#define SYS_getuid      24
#define SYS_kill        37
#define SYS_stat        38
#define SYS_dup         41
#define SYS_pipe        42
#define SYS_ioctl       54
#define SYS_fstat       62
#define SYS_getpagesize 64
#define SYS_mmap        71
#define SYS_munmap      73
#define SYS_sigvec      108
#define SYS_sigblock    109
#define SYS_sigsetmask  110
#define SYS_mkdir       136
#define SYS_rmdir       137
#define SYS_getrlimit   144
#define SYS_statfs      157
#define SYS_getdents    174
#define SYS_nosuch      149         /* a number SunOS 4.0 does not use */

#define O_RDONLY        0
#define O_WRONLY        1
#define O_RDWR          2
#define O_CREAT         0x200
#define O_TRUNC         0x400
#define O_EXCL          0x800

#define ENOENT          2
#define EBADF           9
#define EEXIST          17
#define EINVAL          22
#define ENOTTY          25

#define SIGSEGV         11
#define SIGUSR1         30

#define PROT_RW         3
#define MAP_PRIVATE     2
#define MAP_NEW         0x80000000U
#define MMAP_FLOOR      0x40000000U /* where the system places a mapping */

#define TIOCGETP        0x40067408U /* _IOR(t, 8, struct sgttyb) */
#define DUP_TO          0x40        /* dup(): the second argument is dup2's */
#define RLIMIT_STACK    3
#define RLIM_INFINITY   0x7fffffff

/* <sys/stat.h>: 64 bytes. */
struct sun_stat {
    short          st_dev;
    unsigned long  st_ino;
    unsigned short st_mode;
    short          st_nlink, st_uid, st_gid, st_rdev;
    long           st_size, st_atime, st_spare1, st_mtime, st_spare2;
    long           st_ctime, st_spare3, st_blksize, st_blocks, st_spare4[2];
};

/* <sys/dirent.h>. */
struct sun_dirent {
    long           d_off;
    unsigned long  d_fileno;
    unsigned short d_reclen, d_namlen;
    char           d_name[1];
};

/* <sys/signal.h>, under sun386. */
struct sun_sigcontext {
    int sc_onstack, sc_mask, sc_sp, sc_pc, sc_ps, sc_eax, sc_edx;
};
struct sun_sigvec {
    void (*sv_handler)(void);
    int  sv_mask, sv_flags;
};

long sys_edx;                   /* %edx after the last call */
int  sig_seen, sig_code_ok;
struct sun_sigcontext *sig_scp;

/*
 * sc(number, arguments...): the call, as a libc stub makes it.  The
 * number is taken off so that the arguments sit above the return address.
 * A failure comes back negated.
 */
__asm__(
    ".text\n"
    ".globl sc\n"
    "sc:\n"
    "    pop %edx\n"
    "    pop %eax\n"
    "    push %edx\n"
    "    int $0xff\n"
    "    jnc 1f\n"
    "    neg %eax\n"
    "1:  mov %edx,sys_edx\n"
    "    pop %edx\n"
    "    sub $4,%esp\n"
    "    jmp *%edx\n"
    /*
     * What sigvec() is given: entered with the signal's number, a code,
     * the sigcontext's address and a fault address on the stack and no
     * return address, and left through sigcleanup (139) with the stack
     * pointer at the third of those.  libc's _sigtramp is this around a
     * call of the program's handler.
     */
    ".globl tramp\n"
    "tramp:\n"
    "    push %eax\n"
    "    mov 4(%esp),%eax\n"
    "    mov %eax,sig_seen\n"
    "    mov 12(%esp),%eax\n"
    "    mov %eax,sig_scp\n"
    "    pop %eax\n"
    "    add $8,%esp\n"
    "    mov $139,%eax\n"
    "    int $0xff\n");
long sc(int nr, ...);
void tramp(void);

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

static int same(const char *a, const char *b, int n) {
    while (n--) if (*a++ != *b++) return 0;
    return 1;
}

void start_c(void) {
    static struct sun_stat st;
    static char buf[1024];
    static long lim[2];
    static long fsbuf[16];
    struct sun_sigvec sv;
    volatile unsigned char *null = 0;
    volatile unsigned long keep;
    unsigned char *m;
    long pid, n, fd, fd2, end, i, found;
    int status;

    put("suntest: SunOS 4.0 (Sun386i) calls\n");

    /* getpid(2), getuid(2): the second result is getppid(), geteuid(). */
    pid = sc(SYS_getpid);
    n = sys_edx;                /* before the next call replaces it */
    check("getpid", pid, pid > 0);
    check("  getppid in %edx", n, n > 0 && n != pid);
    n = sc(SYS_getuid);
    check("getuid, geteuid in %edx", sys_edx, n >= 0 && sys_edx == n);
    n = sc(SYS_getpagesize);
    check("getpagesize", n, n == 4096);

    /* Errors: the carry flag and 4.3BSD's number. */
    n = sc(SYS_open, "/no/such/file", O_RDONLY, 0);
    check("open of nothing: ENOENT", n, n == -ENOENT);
    n = sc(SYS_close, 99);
    check("close of nothing: EBADF", n, n == -EBADF);
    n = sc(SYS_nosuch);
    check("a call SunOS does not have: EINVAL", n, n == -EINVAL);

    /* Address 0 is not the program's: its first page is the second. */
    pid = sc(SYS_fork);
    if (pid >= 0 && sys_edx) {
        keep = null[0];
        sc(SYS_exit, (int)keep);
    }
    check("fork: the parent has %edx 0 and the child's pid", pid,
          pid > 0 && sys_edx == 0);
    status = 0;
    n = sc(SYS_wait4, 0, &status, 0, 0);
    check("wait4(0): any child", n, n == pid);
    check("  a read of address 0 ended it by signal", status & 0x7f,
          (status & 0x7f) == SIGSEGV);

    /* The program works in its own tree (/perso/sunos). */
    sc(SYS_unlink, "/tmp/suntest.d/f");
    sc(SYS_rmdir, "/tmp/suntest.d");
    n = sc(SYS_mkdir, "/tmp/suntest.d", 0755);
    check("mkdir /tmp/suntest.d", n, n == 0);
    n = sc(SYS_stat, "/perso/sunos/tmp/suntest.d", &st);
    check("  it is under /perso/sunos", n, n == 0);
    check("  struct stat: a directory", st.st_mode & 0170000,
          (st.st_mode & 0170000) == 0040000);
    fd = sc(SYS_open, "/tmp/suntest.d/f", O_WRONLY | O_CREAT | O_EXCL, 0644);
    check("create /tmp/suntest.d/f", fd, fd >= 0);
    n = sc(SYS_write, fd, "twelve bytes", 12);
    check("  write", n, n == 12);
    sc(SYS_close, fd);
    n = sc(SYS_open, "/tmp/suntest.d/f", O_WRONLY | O_CREAT | O_EXCL, 0644);
    check("  again with O_EXCL: EEXIST", n, n == -EEXIST);
    n = sc(SYS_stat, "/perso/sunos/tmp/suntest.d/f", &st);
    check("  struct stat: st_size", st.st_size, n == 0 && st.st_size == 12);
    check("  struct stat: a regular file, mode 644", st.st_mode,
          (st.st_mode & 0170777) == 0100644);
    n = sc(SYS_chdir, "/tmp/suntest.d");
    check("chdir /tmp/suntest.d", n, n == 0);
    fd = sc(SYS_open, "f", O_RDONLY, 0);
    n = sc(SYS_read, fd, buf, sizeof(buf));
    check("  read f, a relative name", n, n == 12 && same(buf, "twelve", 6));
    n = sc(SYS_ioctl, fd, TIOCGETP, buf);
    check("  ioctl TIOCGETP on a file: ENOTTY", n, n == -ENOTTY);

    /* statfs(2): "any file within the mounted filesystem". */
    n = sc(SYS_statfs, "/tmp/suntest.d/f", fsbuf);
    check("statfs of a file: f_bsize", fsbuf[1], n == 0 && fsbuf[1] > 0);

    /* dup(2) with the bit that makes it dup2(). */
    fd2 = sc(SYS_dup, fd | DUP_TO, 9);
    check("dup(fd | 0100, 9)", fd2, fd2 == 9);
    n = sc(SYS_fstat, 9, &st);
    check("  fstat of it: st_size", st.st_size, n == 0 && st.st_size == 12);
    sc(SYS_close, 9);
    sc(SYS_close, fd);

    /* getdents(2): the directory holds ".", ".." and "f". */
    fd = sc(SYS_open, "/tmp/suntest.d", O_RDONLY, 0);
    n = sc(SYS_getdents, fd, buf, sizeof(buf));
    found = 0;
    for (i = 0; i < n; ) {
        struct sun_dirent *d = (struct sun_dirent *)(buf + i);

        if (d->d_reclen == 0 || (d->d_reclen & 3)) break;
        if (d->d_namlen == 1 && d->d_name[0] == 'f' && d->d_fileno != 0) {
            found = 1;
        }
        i += d->d_reclen;
    }
    check("getdents: bytes", n, n > 0 && i == n);
    check("  it has f", found, found == 1);
    n = sc(SYS_getdents, fd, buf, sizeof(buf));
    check("  and then the end", n, n == 0);
    sc(SYS_close, fd);

    /* pipe(2): both descriptors come back in registers. */
    fd = sc(SYS_pipe);
    fd2 = sys_edx;
    check("pipe: read end in %eax", fd, fd >= 0);
    check("  write end in %edx", fd2, fd2 >= 0 && fd2 != fd);
    sc(SYS_write, fd2, "through", 7);
    n = sc(SYS_read, fd, buf, sizeof(buf));
    check("  data through it", n, n == 7 && same(buf, "through", 7));
    sc(SYS_close, fd);
    sc(SYS_close, fd2);

    /* mmap(2) of /dev/zero is how memory is had; brk(2) "returns 0". */
    fd = sc(SYS_open, "/dev/zero", O_RDWR, 0);
    check("open /dev/zero", fd, fd >= 0);
    m = (unsigned char *)sc(SYS_mmap, 0, 8192, PROT_RW,
                            MAP_PRIVATE | MAP_NEW, fd, 0);
    check("mmap: placed clear of the program",
          (long)((unsigned long)m >> 20),
          (unsigned long)m >= MMAP_FLOOR && ((unsigned long)m & 4095) == 0);
    if ((unsigned long)m >= MMAP_FLOOR) {
        check("  it reads as zeroes", m[0] + m[8191], m[0] + m[8191] == 0);
        m[8191] = 7;
        check("  and can be written", m[8191], m[8191] == 7);
        n = sc(SYS_munmap, m, 8192);
        check("munmap", n, n == 0);
    }
    sc(SYS_close, fd);
    {
        extern char _end[];

        end = ((long)_end + 4095) & ~4095L;
        n = sc(SYS_brk, end + 8192);
        check("brk: 0 on success", n, n == 0);
        ((char *)end)[8191] = 1;
        check("  the new memory is there", ((char *)end)[8191], 1);
    }
    n = sc(SYS_getrlimit, RLIMIT_STACK, lim);
    check("getrlimit: a limit, or RLIM_INFINITY", lim[1],
          n == 0 && lim[1] > 0 && lim[1] <= RLIM_INFINITY);

    /* Signals: sigvec(2), the handler's arguments, sigcleanup. */
    sv.sv_handler = tramp;
    sv.sv_mask = 0;
    sv.sv_flags = 0;
    n = sc(SYS_sigvec, SIGUSR1, &sv, 0);
    check("sigvec SIGUSR1", n, n == 0);
    n = sc(SYS_sigblock, 1 << (SIGUSR1 - 1));
    check("sigblock: the mask before", n, n == 0);
    sc(SYS_kill, sc(SYS_getpid), SIGUSR1);
    check("  blocked, it is not taken", sig_seen, sig_seen == 0);
    n = sc(SYS_sigsetmask, 0);
    check("sigsetmask: the mask before", n, n == 1 << (SIGUSR1 - 1));
    check("  unblocked, the handler ran with its number", sig_seen,
          sig_seen == SIGUSR1);
    check("  and was given a sigcontext on the stack",
          (long)((unsigned long)sig_scp >> 28), sig_scp != 0);
    n = sc(SYS_sigblock, 0);
    check("  and the mask is the one before it", n, n == 0);

    sc(SYS_chdir, "/");
    sc(SYS_unlink, "/tmp/suntest.d/f");
    n = sc(SYS_rmdir, "/tmp/suntest.d");
    check("rmdir", n, n == 0);

    put(failures ? "suntest: FAIL\n" : "suntest: PASS\n");
    sc(SYS_exit, failures ? 1 : 0);
}

/* The entry: the stack holds argc, argv and the environment, which this
 * program does not look at. */
__asm__(
    ".section .text.start,\"ax\"\n"
    ".globl _start\n"
    "_start:\n"
    "    call start_c\n"
    "    hlt\n");
