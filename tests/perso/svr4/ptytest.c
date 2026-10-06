/*
 * ptytest.c - a System V pseudo-terminal, taken the System V way, under
 * substrate's SVR4 personality.
 *
 * The program is freestanding: it is built with the host's compiler and
 * makes Release 4's system calls itself (`lcall $7,$0`), so it needs no
 * vendor's libc or compiler and no distribution media.  It does what
 * ptsname(), grantpt() and unlockpt() do underneath, opens the slave,
 * pushes the terminal modules, and talks across the pair.
 *
 *     cc -m32 -static -nostdlib -fno-pie -no-pie -fno-stack-protector \
 *        -o ptytest ptytest.c
 *
 * The ELF loader takes an unbranded static program for a Release 4 one
 * when it is run from under /perso/svr4, so copy it there and run it by
 * that path:
 *
 *     mkdir -p /perso/svr4/tmp && cp ptytest /perso/svr4/tmp/
 *     /perso/svr4/tmp/ptytest
 *
 * It prints a line per step and "ptytest: PASS" or "ptytest: FAIL".  With
 * a vendor's tree mounted at /perso/svr4 it also runs that system's
 * /usr/lib/pt_chmod, the set-id helper grantpt() runs, on the master; the
 * step is skipped if there is none.
 */

#define SYS_exit    1
#define SYS_fork    2
#define SYS_read    3
#define SYS_write   4
#define SYS_open    5
#define SYS_close   6
#define SYS_wait    7
#define SYS_pgrpsys 39
#define SYS_ioctl   54
#define SYS_execve  59
#define SYS_xstat   123
#define SYS_fxstat  125

#define O_RDWR      2
#define STAT_VER    2

#define I_PUSH      0x5302
#define I_LOOK      0x5304
#define I_STR       0x5308
#define I_FIND      0x530b
#define ISPTM       0x5001
#define UNLKPT      0x5002
#define TCGETS      0x540d
#define TCSETS      0x540e
#define TIOCGETP    0x7408
#define TIOCLGET    0x747c

#define ECHO        0x0008
#define ICANON      0x0002
#define O_ECHO      0x0008      /* sgttyb */

struct strioctl { int ic_cmd, ic_timout, ic_len; char *ic_dp; };
struct termios { unsigned c_iflag, c_oflag, c_cflag, c_lflag;
                 unsigned char c_cc[19]; };
struct sgttyb { char ispeed, ospeed, erase, kill; int flags; };
struct xstat { unsigned st_dev; int pad1[3]; unsigned st_ino, st_mode,
               st_nlink, st_uid, st_gid, st_rdev; int rest[24]; };

long sys_edx;

/*
 * long sc(nr, ...): the arguments are left where the kernel looks for
 * them, above a return address; carry means the result is an errno, which
 * comes back negative.  The second result (wait's status) is kept.
 */
__asm__(
    ".text\n"
    ".globl sc\n"
    "sc:\n"
    "    pop %edx\n"              /* our return address */
    "    pop %eax\n"              /* the call number */
    "    push %edx\n"
    "    lcall $7,$0\n"
    "    jnc 1f\n"
    "    neg %eax\n"
    "1:  mov %edx,sys_edx\n"
    "    pop %edx\n"
    "    sub $4,%esp\n"           /* the slot the number was in */
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

/* fork(2) returns in both with a process id in EAX -- the child's in the
 * parent, the parent's in the child -- and tells them apart by EDX. */
static long do_fork(void) {
    long pid = sc(SYS_fork);

    return pid < 0 ? pid : sys_edx ? 0 : pid;
}

static int same(const char *a, const char *b, int n) {
    while (n--) if (*a++ != *b++) return 0;
    return 1;
}

void _start(void) {
    static char pts[32] = "/dev/pts/";
    static char *chmod_argv[3] = { "pt_chmod", 0, 0 };
    static char fdarg[8];
    static char *envp[1] = { 0 };
    struct strioctl ioc = { ISPTM, 0, 0, 0 };
    struct termios t;
    struct sgttyb sg;
    struct xstat sb, sb2;
    char buf[64], name[16];
    long m, s, n, pid, minor;
    int i, local;

    put("ptytest: a System V pseudo-terminal\n");
    m = sc(SYS_open, "/dev/ptmx", O_RDWR, 0);
    check("open /dev/ptmx", m, m >= 0);
    if (m < 0) goto done;

    /* ptsname(): what is its minor number, and is it a master? */
    check("I_STR ISPTM on standard output",
          sc(SYS_ioctl, 1, I_STR, &ioc), sc(SYS_ioctl, 1, I_STR, &ioc) < 0);
    check("fxstat master", sc(SYS_fxstat, STAT_VER, m, &sb),
          sc(SYS_fxstat, STAT_VER, m, &sb) == 0);
    minor = sb.st_rdev & 0x3ffff;
    check("master is a character device", sb.st_mode >> 12,
          (sb.st_mode & 0xf000) == 0x2000);
    check("master's minor number", minor, minor >= 0 && minor < 256);
    i = len(pts);
    if (minor >= 100) pts[i++] = '0' + minor / 100;
    if (minor >= 10) pts[i++] = '0' + minor / 10 % 10;
    pts[i++] = '0' + minor % 10;
    pts[i] = 0;
    put("        slave is "); put(pts); put("\n");

    /* Nobody has asked for the slave yet, and it cannot be opened.  Once
     * ptsname() has asked (ISPTM) it is there for grantpt()'s helper to
     * find, as it is on System V from the start. */
    s = sc(SYS_open, pts, O_RDWR, 0);
    check("open the slave before ptsname", s, s < 0);
    check("I_STR ISPTM on the master", sc(SYS_ioctl, m, I_STR, &ioc),
          sc(SYS_ioctl, m, I_STR, &ioc) == 0);
    check("xstat the slave after ptsname",
          sc(SYS_xstat, STAT_VER, pts, &sb2),
          sc(SYS_xstat, STAT_VER, pts, &sb2) == 0);

    /* grantpt(): the vendor's helper, given the master as descriptor 0
     * is how libc runs it; here it is named in the argument. */
    if (sc(SYS_xstat, STAT_VER, "/usr/lib/pt_chmod", &sb2) == 0) {
        fdarg[0] = '0' + m;
        chmod_argv[1] = fdarg;
        pid = do_fork();
        if (pid == 0) {
            sc(SYS_execve, "/usr/lib/pt_chmod", chmod_argv, envp);
            sc(SYS_exit, 99);
        }
        n = sc(SYS_wait);
        check("pt_chmod exit status", (sys_edx >> 8) & 0xff,
              n == pid && ((sys_edx >> 8) & 0xff) == 0);
    } else {
        put("  skip  /usr/lib/pt_chmod (no vendor tree)\n");
    }

    /* unlockpt() */
    ioc.ic_cmd = UNLKPT;
    check("I_STR UNLKPT", sc(SYS_ioctl, m, I_STR, &ioc),
          sc(SYS_ioctl, m, I_STR, &ioc) == 0);
    s = sc(SYS_open, pts, O_RDWR, 0);
    check("open the slave", s, s >= 0);
    if (s < 0) goto done;

    /* The modules: there already, pushing them again is harmless, and
     * anything else is not a module. */
    check("I_FIND ldterm", sc(SYS_ioctl, s, I_FIND, "ldterm"),
          sc(SYS_ioctl, s, I_FIND, "ldterm") == 1);
    check("I_PUSH ptem", sc(SYS_ioctl, s, I_PUSH, "ptem"),
          sc(SYS_ioctl, s, I_PUSH, "ptem") == 0);
    check("I_PUSH ldterm", sc(SYS_ioctl, s, I_PUSH, "ldterm"),
          sc(SYS_ioctl, s, I_PUSH, "ldterm") == 0);
    check("I_PUSH ttcompat", sc(SYS_ioctl, s, I_PUSH, "ttcompat"),
          sc(SYS_ioctl, s, I_PUSH, "ttcompat") == 0);
    check("I_PUSH nosuch", sc(SYS_ioctl, s, I_PUSH, "nosuch"),
          sc(SYS_ioctl, s, I_PUSH, "nosuch") < 0);
    n = sc(SYS_ioctl, s, I_LOOK, name);
    check("I_LOOK", n, n == 0 && same(name, "ttcompat", 9));
    check("I_FIND ptem on the master", sc(SYS_ioctl, m, I_FIND, "ptem"),
          sc(SYS_ioctl, m, I_FIND, "ptem") == 0);

    /* It is a terminal, by termios and by ttcompat's older interface,
     * and the two agree. */
    n = sc(SYS_ioctl, s, TCGETS, &t);
    check("TCGETS", n, n == 0 && (t.c_lflag & ICANON));
    n = sc(SYS_ioctl, s, TIOCGETP, &sg);
    check("TIOCGETP", n,
          n == 0 && !(sg.flags & O_ECHO) == !(t.c_lflag & ECHO));
    n = sc(SYS_ioctl, s, TIOCLGET, &local);
    check("TIOCLGET", n, n == 0);
    t.c_lflag &= ~ECHO;
    check("TCSETS, echo off", sc(SYS_ioctl, s, TCSETS, &t),
          sc(SYS_ioctl, s, TCSETS, &t) == 0);
    n = sc(SYS_ioctl, s, TIOCGETP, &sg);
    check("TIOCGETP sees echo off", sg.flags & O_ECHO,
          n == 0 && !(sg.flags & O_ECHO));

    /* Across the pair, both ways. */
    pid = do_fork();
    if (pid == 0) {
        sc(SYS_close, m);
        sc(SYS_pgrpsys, 3);                     /* setsid() */
        sc(SYS_write, s, "from the slave\n", 15);
        n = sc(SYS_read, s, buf, sizeof(buf));
        sc(SYS_exit, n == 13 && same(buf, "to the slave\n", 13) ? 7 : 8);
    }
    n = sc(SYS_read, m, buf, sizeof(buf));
    /* The line discipline turns the newline into CR LF on the way out. */
    check("master reads what the slave wrote", n,
          n >= 15 && same(buf, "from the slave", 14));
    n = sc(SYS_write, m, "to the slave\n", 13);
    check("master writes", n, n == 13);
    n = sc(SYS_wait);
    check("the slave read it (exit status 7)", (sys_edx >> 8) & 0xff,
          n == pid && ((sys_edx >> 8) & 0xff) == 7);

done:
    put(failures ? "ptytest: FAIL\n" : "ptytest: PASS\n");
    sc(SYS_exit, failures ? 1 : 0);
}
