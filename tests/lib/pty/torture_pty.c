/*
 * torture_pty.c — Unix98 PTY torture battery (~50 scenarios).
 *
 * Substrate's PTY layer lives in sys/drivers/console/pty.c with
 * userspace helpers posix_openpt/grantpt/unlockpt/ptsname in
 * lib/c/src/stdlib.c.  This test exercises the full clone-device
 * path: posix_openpt(O_RDWR) -> grantpt -> unlockpt -> ptsname ->
 * open("/dev/pts/N").
 *
 * This is a KERNEL-HANG-HUNTING suite.  Substrate has an intermittent
 * lost-wakeup bug on blocking tty/pty read/write under kernel
 * preemption.  Every test runs in its own forked child guarded by an
 * alarm(2) watchdog: a hung child is SIGKILLed and reported as HANG so
 * one wedged test never wedges the whole run.  alarm() fires off the
 * timer IRQ, independent of the (buggy) sched_wakeup path, so the
 * watchdog still works even when the wakeup machinery is stuck.
 *
 * Builds against host libc by default (most Linuxes ship Unix98 PTYs
 * out of the box, so we can sanity-check the test logic itself).
 * Cross-builds against substrate's libc + libpthread with CROSS=PREFIX.
 *
 * Child exit-code convention (consumed by run_one):
 *   fn() returns  0  -> child _exit(0)  -> PASS
 *   fn() returns  1  -> child _exit(2)  -> SKIP
 *   fn() returns -1  -> child _exit(1)  -> FAIL
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#define TEST_TIMEOUT 6
static int tests_run, tests_pass, tests_fail, tests_hang, tests_skip;
typedef int (*testfn)(void);
static void alrm_noop(int s){ (void)s; }
static void run_one(const char *name, testfn fn) {
    fprintf(stdout, "[%2d] %-32s ", ++tests_run, name); fflush(stdout);
    pid_t pid = fork();
    if (pid < 0) { fprintf(stdout,"FORK-FAIL errno=%d\n",errno); tests_fail++; return; }
    if (pid == 0) { int rc = fn(); fflush(stdout); _exit(rc==0?0:(rc==1?2:1)); }
    struct sigaction sa={0},old; sa.sa_handler=alrm_noop; sigaction(SIGALRM,&sa,&old);
    alarm(TEST_TIMEOUT);
    int st; pid_t r = waitpid(pid,&st,0);
    alarm(0); sigaction(SIGALRM,&old,NULL);
    if (r != pid) {
        if (waitpid(pid,&st,WNOHANG) != pid) {
            kill(pid,SIGKILL); waitpid(pid,&st,0);
            fprintf(stdout,"HANG (killed after %ds)\n",TEST_TIMEOUT); tests_hang++; return;
        }
    }
    if (WIFSIGNALED(st)){ fprintf(stdout,"CRASH sig=%d\n",WTERMSIG(st)); tests_fail++; }
    else if (WEXITSTATUS(st)==0){ fprintf(stdout,"PASS\n"); tests_pass++; }
    else if (WEXITSTATUS(st)==2){ fprintf(stdout,"SKIP\n"); tests_skip++; }
    else { fprintf(stdout,"FAIL\n"); tests_fail++; }
}
#define RUN(name) run_one(#name, test_##name)
#define TEST(name) static int test_##name(void)
#define CHECK(cond,msg) do{ if(!(cond)){ fprintf(stdout,"\n    [%s:%d] %s errno=%d(%s) ",__FILE__,__LINE__,(msg),errno,strerror(errno)); return -1; } }while(0)
#define SKIP(m) do{(void)(m);return 1;}while(0)

/* helper: open a master/slave pair; returns 0 on success */
static int open_pty(int *mfd, int *sfd) {
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0) return -1;
    if (grantpt(m) != 0 || unlockpt(m) != 0) { close(m); return -1; }
    char *n = ptsname(m);
    if (!n) { close(m); return -1; }
    int s = open(n, O_RDWR | O_NOCTTY);
    if (s < 0) { close(m); return -1; }
    *mfd = m; *sfd = s; return 0;
}

/* ------------------------------------------------------------------ *
 * Shared helpers                                                      *
 * ------------------------------------------------------------------ */

/* Portable raw-mode setup.  cfmakeraw(3) is a BSD/GNU extension not
 * present in substrate's <termios.h>, so apply the equivalent flag
 * masks by hand (same as the classic cfmakeraw definition). */
static void make_raw(struct termios *t) {
    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR |
                    IGNCR | ICRNL | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
}

/* Put a tty fd into fully-raw, byte-at-a-time mode. */
static int set_raw(int fd) {
    struct termios t;
    if (tcgetattr(fd, &t) != 0) return -1;
    make_raw(&t);
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSANOW, &t);
}

/* Read exactly n bytes (or until EOF/error).  Returns bytes read. */
static ssize_t read_full(int fd, void *buf, size_t n) {
    size_t got = 0;
    char *p = buf;
    while (got < n) {
        ssize_t r = read(fd, p + got, n - got);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) break;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

/* Write exactly n bytes.  Returns bytes written or -1. */
static ssize_t write_full(int fd, const void *buf, size_t n) {
    size_t put = 0;
    const char *p = buf;
    while (put < n) {
        ssize_t w = write(fd, p + put, n - put);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        put += (size_t)w;
    }
    return (ssize_t)put;
}

/* Streaming, chunk-boundary-independent fold: callers thread the
 * accumulator through so the result depends only on the byte sequence,
 * not on how it was split across read()/write() calls. */
static uint32_t cksum_fold(uint32_t acc, const unsigned char *p, size_t n) {
    for (size_t i = 0; i < n; i++) acc = acc * 131u + p[i];
    return acc;
}

/* ------------------------------------------------------------------ *
 * Clone-path / device-naming tests                                    *
 * ------------------------------------------------------------------ */

TEST(ptmx_open) {
    int fd = posix_openpt(O_RDWR);
    CHECK(fd >= 0, "posix_openpt(O_RDWR)");
    close(fd);
    return 0;
}

TEST(ptmx_open_noctty) {
    int fd = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(fd >= 0, "posix_openpt(O_RDWR|O_NOCTTY)");
    close(fd);
    return 0;
}

TEST(grant_unlock) {
    int fd = posix_openpt(O_RDWR);
    CHECK(fd >= 0, "posix_openpt");
    CHECK(grantpt(fd) == 0, "grantpt");
    CHECK(unlockpt(fd) == 0, "unlockpt");
    close(fd);
    return 0;
}

TEST(ptsname_format) {
    int fd = posix_openpt(O_RDWR);
    CHECK(fd >= 0, "posix_openpt");
    CHECK(grantpt(fd) == 0, "grantpt");
    CHECK(unlockpt(fd) == 0, "unlockpt");
    char *name = ptsname(fd);
    CHECK(name != NULL, "ptsname non-NULL");
    CHECK(strncmp(name, "/dev/pts/", 9) == 0, "ptsname starts with /dev/pts/");
    int n = -1;
    CHECK(sscanf(name + 9, "%d", &n) == 1 && n >= 0, "/dev/pts/<n> parses");
    close(fd);
    return 0;
}

TEST(slave_open) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    close(s); close(m);
    return 0;
}

TEST(slave_open_no_noctty) {
    /* Open the slave without O_NOCTTY.  We are not a session leader,
     * so no controlling tty is acquired; the open should still work. */
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(m >= 0, "posix_openpt");
    CHECK(grantpt(m) == 0, "grantpt");
    CHECK(unlockpt(m) == 0, "unlockpt");
    char *n = ptsname(m);
    CHECK(n != NULL, "ptsname");
    int s = open(n, O_RDWR);
    CHECK(s >= 0, "open slave without O_NOCTTY");
    close(s); close(m);
    return 0;
}

TEST(tiocgptn_matches_ptsname) {
#ifdef TIOCGPTN
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    CHECK(m >= 0, "posix_openpt");
    CHECK(grantpt(m) == 0, "grantpt");
    CHECK(unlockpt(m) == 0, "unlockpt");
    unsigned int ptn = 0;
    if (ioctl(m, TIOCGPTN, &ptn) != 0) { close(m); SKIP("TIOCGPTN unsupported"); }
    char *name = ptsname(m);
    CHECK(name != NULL, "ptsname");
    int from_name = atoi(name + 9);
    CHECK((unsigned int)from_name == ptn, "TIOCGPTN matches ptsname index");
    close(m);
    return 0;
#else
    SKIP("TIOCGPTN not defined");
#endif
}

TEST(distinct_indices) {
    int m[4] = {-1,-1,-1,-1};
    int idx[4];
    int rc = -1;
    for (int i = 0; i < 4; i++) {
        m[i] = posix_openpt(O_RDWR | O_NOCTTY);
        if (m[i] < 0 || grantpt(m[i]) != 0 || unlockpt(m[i]) != 0) goto done;
        char *n = ptsname(m[i]);
        if (!n) goto done;
        idx[i] = atoi(n + 9);
    }
    for (int i = 0; i < 4; i++)
        for (int j = i + 1; j < 4; j++)
            if (idx[i] == idx[j]) goto done;
    rc = 0;
done:
    for (int i = 0; i < 4; i++) if (m[i] >= 0) close(m[i]);
    return rc;
}

/* ------------------------------------------------------------------ *
 * Data movement (raw mode), both directions, varied sizes             *
 * ------------------------------------------------------------------ */

/* Each of these writes the payload from a helper thread so the
 * blocking reader on the other end is forced to sleep and be woken —
 * exercising the lost-wakeup path. */

struct pump { int fd; const unsigned char *buf; size_t n; int ok; };
static void *pump_writer(void *a) {
    struct pump *p = a;
    p->ok = (write_full(p->fd, p->buf, p->n) == (ssize_t)p->n);
    return NULL;
}

static int xfer_via_thread(int wfd, int rfd, size_t sz) {
    unsigned char *out = malloc(sz), *in = malloc(sz);
    int rc = -1;
    pthread_t th;
    struct pump p = { wfd, NULL, sz, 0 };
    int started = 0;
    if (!out || !in) goto done;
    for (size_t i = 0; i < sz; i++) out[i] = (unsigned char)(i * 17 + 3);
    p.buf = out;
    if (pthread_create(&th, NULL, pump_writer, &p) != 0) goto done;
    started = 1;
    if (read_full(rfd, in, sz) != (ssize_t)sz) { pthread_join(th, NULL); started = 0; goto done; }
    pthread_join(th, NULL); started = 0;
    if (!p.ok) goto done;
    if (memcmp(out, in, sz) != 0) goto done;
    rc = 0;
done:
    if (started) pthread_join(th, NULL);
    free(out); free(in);
    return rc;
}

#define XFER_TEST(name, dir_w, dir_r, sz)                       \
TEST(name) {                                                     \
    int m, s;                                                    \
    CHECK(open_pty(&m, &s) == 0, "open pair");                  \
    CHECK(set_raw(m) == 0, "raw master");                       \
    CHECK(set_raw(s) == 0, "raw slave");                        \
    int rc = xfer_via_thread(dir_w, dir_r, (sz));               \
    close(s); close(m);                                         \
    CHECK(rc == 0, "transfer/verify " #sz);                     \
    return 0;                                                    \
}

XFER_TEST(m2s_1,    m, s, 1)
XFER_TEST(m2s_16,   m, s, 16)
XFER_TEST(m2s_256,  m, s, 256)
XFER_TEST(m2s_4095, m, s, 4095)
XFER_TEST(m2s_4096, m, s, 4096)
XFER_TEST(m2s_9000, m, s, 9000)
XFER_TEST(s2m_1,    s, m, 1)
XFER_TEST(s2m_16,   s, m, 16)
XFER_TEST(s2m_256,  s, m, 256)
XFER_TEST(s2m_4095, s, m, 4095)
XFER_TEST(s2m_4096, s, m, 4096)
XFER_TEST(s2m_9000, s, m, 9000)

TEST(raw_all_byte_values) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(m) == 0, "raw master");
    CHECK(set_raw(s) == 0, "raw slave");
    /* 256-byte payload 0x00..0xFF, threaded write so reader blocks. */
    unsigned char out[256], in[256];
    for (int i = 0; i < 256; i++) out[i] = (unsigned char)i;
    struct pump p = { m, out, sizeof(out), 0 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, pump_writer, &p) == 0, "thread");
    ssize_t got = read_full(s, in, sizeof(in));
    pthread_join(th, NULL);
    close(s); close(m);
    CHECK(p.ok, "writer ok");
    CHECK(got == (ssize_t)sizeof(in), "read all 256 bytes");
    CHECK(memcmp(out, in, sizeof(out)) == 0, "binary payload identical");
    return 0;
}

/* ------------------------------------------------------------------ *
 * Cooked / canonical line discipline                                  *
 * ------------------------------------------------------------------ */

static int set_canon(int s) {
    struct termios t;
    if (tcgetattr(s, &t) != 0) return -1;
    t.c_lflag |= ICANON;
    t.c_lflag &= ~(ECHO | ISIG | IEXTEN);
    t.c_iflag &= ~(ICRNL | INLCR | IXON | IXOFF | ISTRIP | IGNCR);
    t.c_oflag &= ~OPOST;
    t.c_cc[VMIN]  = 1;
    t.c_cc[VTIME] = 0;
    return tcsetattr(s, TCSANOW, &t);
}

TEST(icanon_full_line) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_canon(s) == 0, "canon slave");
    const char msg[] = "hello\n";
    struct pump p = { m, (const unsigned char *)msg, sizeof(msg) - 1, 0 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, pump_writer, &p) == 0, "thread");
    char buf[64] = {0};
    ssize_t n = read(s, buf, sizeof(buf));
    pthread_join(th, NULL);
    close(s); close(m);
    CHECK(p.ok, "writer ok");
    CHECK(n >= 6, "slave canonical read short");
    CHECK(memcmp(buf, "hello\n", 6) == 0, "canonical line payload");
    return 0;
}

TEST(icanon_partial_then_newline) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_canon(s) == 0, "canon slave");
    /* Write a partial line; a non-blocking read on the slave must not
     * deliver it (no newline yet). */
    int sflags = fcntl(s, F_GETFL);
    CHECK(fcntl(s, F_SETFL, sflags | O_NONBLOCK) == 0, "nonblock slave");
    CHECK(write_full(m, "partial", 7) == 7, "write partial");
    char buf[64];
    /* Give the line discipline a moment; loop a few non-blocking reads. */
    int delivered = 0;
    for (int i = 0; i < 50; i++) {
        ssize_t n = read(s, buf, sizeof(buf));
        if (n > 0) { delivered = 1; break; }
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;
        usleep(2000);
    }
    CHECK(!delivered, "partial line wrongly delivered before newline");
    /* Now complete the line with a newline; blocking read must deliver. */
    CHECK(fcntl(s, F_SETFL, sflags) == 0, "block slave");
    CHECK(write_full(m, "\n", 1) == 1, "write newline");
    ssize_t n = read(s, buf, sizeof(buf));
    close(s); close(m);
    CHECK(n == 8, "full line delivered after newline");
    CHECK(memcmp(buf, "partial\n", 8) == 0, "line payload");
    return 0;
}

TEST(icanon_erase) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    t.c_lflag |= ICANON;
    t.c_lflag &= ~(ECHO | ISIG | IEXTEN);
    t.c_iflag &= ~(ICRNL | INLCR | IXON | IXOFF);
    t.c_oflag &= ~OPOST;
    unsigned char erase = t.c_cc[VERASE];
    if (erase == 0 || erase == 0xff) { close(s); close(m); SKIP("no VERASE"); }
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr");
    /* "ab" <erase> "c" \n  -> canonical read should yield "ac\n". */
    unsigned char seq[5] = { 'a', 'b', erase, 'c', '\n' };
    struct pump p = { m, seq, sizeof(seq), 0 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, pump_writer, &p) == 0, "thread");
    char buf[16] = {0};
    ssize_t n = read(s, buf, sizeof(buf));
    pthread_join(th, NULL);
    close(s); close(m);
    CHECK(p.ok, "writer ok");
    if (n != 3 || memcmp(buf, "ac\n", 3) != 0) SKIP("erase semantics differ");
    return 0;
}

TEST(echo_master_loopback) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    /* Slave in canonical mode with ECHO on: bytes written to the master
     * are echoed back to the master by the line discipline. */
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    t.c_lflag |= (ICANON | ECHO);
    t.c_lflag &= ~(ISIG | IEXTEN);
    t.c_iflag &= ~(ICRNL | INLCR | IXON | IXOFF);
    t.c_oflag &= ~OPOST;
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr");
    /* Drain the slave in a thread so the echo isn't backpressured. */
    CHECK(write_full(m, "xyz\n", 4) == 4, "write master");
    /* Read the echo back from the master.  Use non-blocking-ish poll. */
    int mflags = fcntl(m, F_GETFL);
    fcntl(m, F_SETFL, mflags | O_NONBLOCK);
    char buf[32];
    size_t got = 0;
    for (int i = 0; i < 200 && got < 3; i++) {
        ssize_t n = read(m, buf + got, sizeof(buf) - got);
        if (n > 0) got += (size_t)n;
        else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) break;
        else usleep(2000);
    }
    close(s); close(m);
    if (got < 3 || memcmp(buf, "xyz", 3) != 0) SKIP("echo semantics differ");
    return 0;
}

/* ------------------------------------------------------------------ *
 * termios get/set round-trips                                         *
 * ------------------------------------------------------------------ */

TEST(termios_roundtrip_basic) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios a, b;
    CHECK(tcgetattr(s, &a) == 0, "tcgetattr 1");
    CHECK(tcsetattr(s, TCSANOW, &a) == 0, "tcsetattr");
    CHECK(tcgetattr(s, &b) == 0, "tcgetattr 2");
    close(s); close(m);
    CHECK(a.c_iflag == b.c_iflag && a.c_oflag == b.c_oflag &&
          a.c_cflag == b.c_cflag && a.c_lflag == b.c_lflag,
          "termios stable across set/get");
    return 0;
}

TEST(termios_lflag_bits) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    t.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHOK | ISIG | IEXTEN | NOFLSH);
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr clear");
    struct termios g;
    CHECK(tcgetattr(s, &g) == 0, "tcgetattr 2");
    close(s); close(m);
    CHECK((g.c_lflag & (ICANON | ECHO | ISIG | IEXTEN)) == 0,
          "lflag bits cleared round-trip");
    return 0;
}

TEST(termios_iflag_oflag_bits) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    t.c_iflag &= ~(ICRNL | INLCR | IXON | IXOFF | ISTRIP | IGNCR | BRKINT);
    t.c_oflag &= ~OPOST;
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr");
    struct termios g;
    CHECK(tcgetattr(s, &g) == 0, "tcgetattr 2");
    close(s); close(m);
    CHECK((g.c_iflag & (ICRNL | IXON | ISTRIP)) == 0, "iflag cleared");
    CHECK((g.c_oflag & OPOST) == 0, "oflag OPOST cleared");
    return 0;
}

TEST(termios_cflag_bits) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    t.c_cflag &= ~CSIZE;
    t.c_cflag |= CS8 | CREAD | CLOCAL;
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr");
    struct termios g;
    CHECK(tcgetattr(s, &g) == 0, "tcgetattr 2");
    close(s); close(m);
    CHECK((g.c_cflag & CSIZE) == CS8, "CS8 round-trip");
    return 0;
}

TEST(termios_baud_roundtrip) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    if (cfsetispeed(&t, B9600) != 0 || cfsetospeed(&t, B9600) != 0) {
        close(s); close(m); SKIP("cfset speed unsupported");
    }
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr");
    struct termios g;
    CHECK(tcgetattr(s, &g) == 0, "tcgetattr 2");
    speed_t i = cfgetispeed(&g), o = cfgetospeed(&g);
    close(s); close(m);
    if (i != B9600 || o != B9600) SKIP("baud not honored on ptys");
    return 0;
}

TEST(termios_tcsadrain) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    t.c_lflag &= ~ICANON;
    CHECK(tcsetattr(s, TCSADRAIN, &t) == 0, "tcsetattr TCSADRAIN");
    struct termios g;
    CHECK(tcgetattr(s, &g) == 0, "tcgetattr 2");
    close(s); close(m);
    CHECK((g.c_lflag & ICANON) == 0, "TCSADRAIN applied");
    return 0;
}

TEST(termios_cc_roundtrip) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    t.c_cc[VMIN]  = 7;
    t.c_cc[VTIME] = 3;
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr");
    struct termios g;
    CHECK(tcgetattr(s, &g) == 0, "tcgetattr 2");
    close(s); close(m);
    CHECK(g.c_cc[VMIN] == 7 && g.c_cc[VTIME] == 3, "c_cc round-trip");
    return 0;
}

/* ------------------------------------------------------------------ *
 * VMIN / VTIME                                                        *
 * ------------------------------------------------------------------ */

TEST(vmin0_vtime_timeout) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    make_raw(&t);
    t.c_cc[VMIN]  = 0;
    t.c_cc[VTIME] = 1;   /* 0.1s */
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr");
    /* No data available: read should return 0 after the VTIME window. */
    char buf[8];
    ssize_t n = read(s, buf, sizeof(buf));
    close(s); close(m);
    CHECK(n == 0, "VMIN=0,VTIME=1 read should return 0 on timeout");
    return 0;
}

TEST(vmin_n_waits) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    make_raw(&t);
    t.c_cc[VMIN]  = 4;
    t.c_cc[VTIME] = 0;
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr");
    /* Writer delivers 4 bytes from a thread; read must return >=4. */
    static const unsigned char four[4] = { 'a','b','c','d' };
    struct pump p = { m, four, 4, 0 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, pump_writer, &p) == 0, "thread");
    char buf[8];
    ssize_t n = read(s, buf, sizeof(buf));
    pthread_join(th, NULL);
    close(s); close(m);
    CHECK(p.ok, "writer ok");
    CHECK(n >= 4, "VMIN=4 delivered all bytes");
    return 0;
}

/* ------------------------------------------------------------------ *
 * Window size                                                         *
 * ------------------------------------------------------------------ */

TEST(winsize_roundtrip) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct winsize set = { 40, 132, 1056, 640 };
    if (ioctl(m, TIOCSWINSZ, &set) != 0) {
        close(s); close(m);
        if (errno == ENOTTY || errno == EINVAL) SKIP("TIOCSWINSZ unsupported");
        CHECK(0, "TIOCSWINSZ");
    }
    struct winsize got = {0,0,0,0};
    CHECK(ioctl(s, TIOCGWINSZ, &got) == 0, "TIOCGWINSZ slave");
    close(s); close(m);
    CHECK(got.ws_row == set.ws_row && got.ws_col == set.ws_col,
          "rows/cols round-trip");
    CHECK(got.ws_xpixel == set.ws_xpixel && got.ws_ypixel == set.ws_ypixel,
          "xpix/ypix round-trip");
    return 0;
}

TEST(winsize_sigwinch) {
    /* SIGWINCH delivery requires the slave to be a controlling tty of a
     * session with a foreground pgrp.  Set that up in a child. */
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    pid_t child = fork();
    CHECK(child >= 0, "fork");
    if (child == 0) {
        close(m);
        if (setsid() < 0) _exit(3);
#ifdef TIOCSCTTY
        if (ioctl(s, TIOCSCTTY, 0) != 0) _exit(3);
#else
        _exit(3);
#endif
        /* Block SIGWINCH and wait for it synchronously; bound with
         * alarm() so the child can't hang if it never arrives. */
        sigset_t w; sigemptyset(&w); sigaddset(&w, SIGWINCH);
        sigprocmask(SIG_BLOCK, &w, NULL);
        signal(SIGALRM, alrm_noop);
        alarm(4);
        int sig = 0;
        if (sigwait(&w, &sig) == 0 && sig == SIGWINCH) _exit(0);
        _exit(1);
    }
    close(s);
    usleep(50000);
    struct winsize set = { 50, 100, 0, 0 };
    ioctl(m, TIOCSWINSZ, &set);
    int st;
    waitpid(child, &st, 0);
    close(m);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return 0;
    SKIP("SIGWINCH/ctty not supported");
}

/* ------------------------------------------------------------------ *
 * Control chars -> signals to a session-leader child                  *
 * ------------------------------------------------------------------ */

/* Child becomes a session leader with the slave as its ctty, then
 * blocks reading the slave.  The parent injects a control char via the
 * master and checks that the child terminates with the expected signal.
 * exit codes: child uses _exit so WIFSIGNALED reflects the tty signal. */
/* Bounded waitpid: poll for up to ~2s for the child to change state.
 * 'flags' may include WUNTRACED.  Returns the waitpid() result; *st is
 * the status if a state change was observed, else *st is left as-is and
 * the function returns 0 (timeout). */
static pid_t wait_bounded(pid_t pid, int *st, int flags) {
    for (int i = 0; i < 1000; i++) {
        pid_t r = waitpid(pid, st, flags | WNOHANG);
        if (r == pid) return r;
        if (r < 0) return r;
        usleep(2000);
    }
    return 0;
}

static int ctrl_signal_test(int ctrl_char_idx, int expect_sig) {
    int m, s;
    if (open_pty(&m, &s) != 0) return -1;
    /* Slave in canonical+ISIG mode. */
    struct termios t;
    if (tcgetattr(s, &t) != 0) { close(m); close(s); return -1; }
    t.c_lflag |= (ICANON | ISIG);
    t.c_lflag &= ~ECHO;
    if (tcsetattr(s, TCSANOW, &t) != 0) { close(m); close(s); return -1; }
    unsigned char cc = t.c_cc[ctrl_char_idx];
    if (cc == 0 || cc == 0xff) { close(m); close(s); return 1; /* SKIP */ }

    pid_t child = fork();
    if (child < 0) { close(m); close(s); return -1; }
    if (child == 0) {
        close(m);
        if (setsid() < 0) _exit(42);
#ifdef TIOCSCTTY
        if (ioctl(s, TIOCSCTTY, 0) != 0) _exit(42);
#else
        _exit(42);
#endif
        /* default dispositions for the signals we test */
        signal(expect_sig, SIG_DFL);
        /* self-bound: never outlive the test even if the signal is
         * silently dropped by the target's line discipline. */
        signal(SIGALRM, SIG_DFL);
        alarm(4);
        char b[16];
        for (;;) {
            ssize_t r = read(s, b, sizeof(b));
            if (r <= 0 && errno != EINTR) _exit(7);
        }
    }
    close(s);
    usleep(50000);
    /* Inject the control char via the master. */
    if (write_full(m, &cc, 1) != 1) { kill(child, SIGKILL); waitpid(child, NULL, 0); close(m); return -1; }
    int st = 0;
    pid_t r = wait_bounded(child, &st, 0);
    if (r != child) { kill(child, SIGKILL); waitpid(child, NULL, 0); close(m); return 1; /* SKIP */ }
    close(m);
    if (WIFEXITED(st) && WEXITSTATUS(st) == 42) return 1; /* ctty setup unsupported */
    if (WIFSIGNALED(st) && WTERMSIG(st) == expect_sig) return 0;
    return 1; /* signal not delivered as expected -> SKIP, don't FAIL */
}

TEST(ctrl_intr_sigint)  { return ctrl_signal_test(VINTR, SIGINT); }
TEST(ctrl_quit_sigquit) { return ctrl_signal_test(VQUIT, SIGQUIT); }
TEST(ctrl_susp_sigtstp) {
    /* SIGTSTP default action is stop, not terminate; observe via WUNTRACED. */
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    t.c_lflag |= (ICANON | ISIG);
    t.c_lflag &= ~ECHO;
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr");
    unsigned char cc = t.c_cc[VSUSP];
    if (cc == 0 || cc == 0xff) { close(m); close(s); SKIP("no VSUSP"); }
    pid_t child = fork();
    CHECK(child >= 0, "fork");
    if (child == 0) {
        close(m);
        if (setsid() < 0) _exit(42);
#ifdef TIOCSCTTY
        if (ioctl(s, TIOCSCTTY, 0) != 0) _exit(42);
#else
        _exit(42);
#endif
        signal(SIGTSTP, SIG_DFL);
        signal(SIGALRM, SIG_DFL);
        alarm(4);
        char b[16];
        for (;;) { ssize_t r = read(s, b, sizeof(b)); if (r <= 0 && errno != EINTR) _exit(7); }
    }
    close(s);
    usleep(50000);
    write_full(m, &cc, 1);
    int st = 0;
    pid_t r = wait_bounded(child, &st, WUNTRACED);
    int stopped = (r == child) && WIFSTOPPED(st) && WSTOPSIG(st) == SIGTSTP;
    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
    close(m);
    if (!stopped) SKIP("SIGTSTP via VSUSP not supported");
    return 0;
}

/* ------------------------------------------------------------------ *
 * EOF / hangup behavior                                               *
 * ------------------------------------------------------------------ */

TEST(veof_returns_zero) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct termios t;
    CHECK(tcgetattr(s, &t) == 0, "tcgetattr");
    t.c_lflag |= ICANON;
    t.c_lflag &= ~(ECHO | ISIG);
    CHECK(tcsetattr(s, TCSANOW, &t) == 0, "tcsetattr");
    unsigned char eof = t.c_cc[VEOF];
    if (eof == 0 || eof == 0xff) { close(m); close(s); SKIP("no VEOF"); }
    struct pump p = { m, &eof, 1, 0 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, pump_writer, &p) == 0, "thread");
    char buf[8];
    ssize_t n = read(s, buf, sizeof(buf));
    pthread_join(th, NULL);
    close(s); close(m);
    if (n != 0) SKIP("VEOF EOF semantics differ");
    return 0;
}

/* Closer thread: sleep briefly then close the fd, to wake a blocked
 * peer with a hangup.  The fd is passed by value through an int box. */
static void *delayed_closer(void *a) {
    int fd = *(int *)a;
    usleep(50000);
    close(fd);
    return NULL;
}

TEST(master_close_eof) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(s) == 0, "raw slave");
    /* Close master from a thread while slave is blocked in read. */
    int mbox = m;
    pthread_t th;
    CHECK(pthread_create(&th, NULL, delayed_closer, &mbox) == 0, "thread");
    char buf[8];
    ssize_t n = read(s, buf, sizeof(buf));
    pthread_join(th, NULL);
    close(s);
    CHECK(n == 0 || (n < 0 && errno == EIO),
          "slave read after master close -> EOF/EIO");
    return 0;
}

TEST(slave_close_eof) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(m) == 0, "raw master");
    int sbox = s;
    pthread_t th;
    CHECK(pthread_create(&th, NULL, delayed_closer, &sbox) == 0, "thread");
    char buf[8];
    ssize_t n = read(m, buf, sizeof(buf));
    pthread_join(th, NULL);
    close(m);
    CHECK(n == 0 || (n < 0 && (errno == EIO || errno == EAGAIN)),
          "master read after slave close -> EOF/EIO");
    return 0;
}

/* ------------------------------------------------------------------ *
 * flush / drain / flow                                                *
 * ------------------------------------------------------------------ */

TEST(tcflush_input) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(s) == 0, "raw slave");
    CHECK(write_full(m, "junk", 4) == 4, "write");
    usleep(20000);
    CHECK(tcflush(s, TCIFLUSH) == 0, "tcflush TCIFLUSH");
    /* After flushing input, a non-blocking read should find nothing. */
    int fl = fcntl(s, F_GETFL);
    fcntl(s, F_SETFL, fl | O_NONBLOCK);
    char buf[8];
    ssize_t n = read(s, buf, sizeof(buf));
    close(s); close(m);
    CHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
          "input flushed (read returns EAGAIN)");
    return 0;
}

TEST(tcflush_output) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(tcflush(m, TCOFLUSH) == 0, "tcflush TCOFLUSH");
    close(s); close(m);
    return 0;
}

TEST(tcflush_both) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(tcflush(s, TCIOFLUSH) == 0, "tcflush TCIOFLUSH");
    close(s); close(m);
    return 0;
}

/* Drain bytes off a non-blocking fd until it would block, then return;
 * lets a peer's output flush without the drainer itself blocking. */
static void *short_drainer(void *a) {
    int fd = *(int *)a;
    char b[256];
    for (int i = 0; i < 1000; i++) {
        ssize_t r = read(fd, b, sizeof(b));
        if (r > 0) continue;
        if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { usleep(1000); continue; }
        break;
    }
    return NULL;
}

TEST(tcdrain_slave) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(s) == 0, "raw slave");
    /* Reader on the master (non-blocking) so the slave's output can
     * drain without wedging the drainer thread on an empty pipe. */
    int mfl = fcntl(m, F_GETFL);
    CHECK(fcntl(m, F_SETFL, mfl | O_NONBLOCK) == 0, "nonblock master");
    int mbox = m;
    pthread_t th;
    CHECK(pthread_create(&th, NULL, short_drainer, &mbox) == 0, "thread");
    CHECK(write_full(s, "drain-me", 8) == 8, "write slave");
    int rc = tcdrain(s);
    /* tcdrain may be ENOTSUP on some pty impls; accept 0 or ENOSYS-ish. */
    if (rc != 0 && errno != ENOTTY && errno != ENOSYS && errno != EINVAL) {
        pthread_join(th, NULL); close(s); close(m);
        CHECK(0, "tcdrain");
    }
    pthread_join(th, NULL);
    close(s); close(m);
    return 0;
}

TEST(tcflow_supported) {
#if defined(TCOOFF) && defined(TCOON)
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    int rc1 = tcflow(s, TCOOFF);
    int rc2 = tcflow(s, TCOON);
    close(s); close(m);
    if (rc1 != 0 || rc2 != 0) SKIP("tcflow unsupported on ptys");
    return 0;
#else
    SKIP("tcflow constants not defined");
#endif
}

/* ------------------------------------------------------------------ *
 * O_NONBLOCK                                                          *
 * ------------------------------------------------------------------ */

TEST(nonblock_master_eagain) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    int fl = fcntl(m, F_GETFL);
    CHECK(fcntl(m, F_SETFL, fl | O_NONBLOCK) == 0, "set nonblock");
    char buf[8];
    ssize_t n = read(m, buf, sizeof(buf));
    close(s); close(m);
    CHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
          "nonblock master read with no data -> EAGAIN");
    return 0;
}

TEST(nonblock_slave_eagain) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(s) == 0, "raw slave");
    int fl = fcntl(s, F_GETFL);
    CHECK(fcntl(s, F_SETFL, fl | O_NONBLOCK) == 0, "set nonblock");
    char buf[8];
    ssize_t n = read(s, buf, sizeof(buf));
    close(s); close(m);
    CHECK(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
          "nonblock slave read with no data -> EAGAIN");
    return 0;
}

/* ------------------------------------------------------------------ *
 * poll / select                                                       *
 * ------------------------------------------------------------------ */

#include <poll.h>

TEST(poll_master_pollin) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(s) == 0, "raw slave");
    static const unsigned char b[4] = { 'p','o','l','l' };
    struct pump p = { s, b, 4, 0 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, pump_writer, &p) == 0, "thread");
    struct pollfd pfd = { m, POLLIN, 0 };
    int r = poll(&pfd, 1, 4000);
    pthread_join(th, NULL);
    CHECK(r == 1, "poll returned ready");
    CHECK(pfd.revents & POLLIN, "POLLIN set on master");
    char buf[8];
    (void)read(m, buf, sizeof(buf));
    close(s); close(m);
    return 0;
}

TEST(poll_master_pollout) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    struct pollfd pfd = { m, POLLOUT, 0 };
    int r = poll(&pfd, 1, 2000);
    close(s); close(m);
    CHECK(r == 1 && (pfd.revents & POLLOUT), "master writable via poll");
    return 0;
}

TEST(select_slave_readable) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(s) == 0, "raw slave");
    static const unsigned char b[3] = { 's','e','l' };
    struct pump p = { m, b, 3, 0 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, pump_writer, &p) == 0, "thread");
    fd_set rd; FD_ZERO(&rd); FD_SET(s, &rd);
    struct timeval tv = { 4, 0 };
    int r = select(s + 1, &rd, NULL, NULL, &tv);
    pthread_join(th, NULL);
    int ready = (r == 1 && FD_ISSET(s, &rd));
    char buf[8];
    (void)read(s, buf, sizeof(buf));
    close(s); close(m);
    CHECK(ready, "select reports slave readable");
    return 0;
}

/* ------------------------------------------------------------------ *
 * Many PTYs / churn                                                   *
 * ------------------------------------------------------------------ */

TEST(eight_pairs_roundtrip) {
    int m[8], s[8];
    int idx[8];
    int rc = -1;
    int n = 0;
    for (; n < 8; n++) {
        if (open_pty(&m[n], &s[n]) != 0) goto done;
        char *name = ptsname(m[n]);
        if (!name) goto done;
        idx[n] = atoi(name + 9);
        if (set_raw(m[n]) != 0 || set_raw(s[n]) != 0) { n++; goto done; }
    }
    /* distinct indices */
    for (int i = 0; i < 8; i++)
        for (int j = i + 1; j < 8; j++)
            if (idx[i] == idx[j]) goto done;
    /* round-trip a byte on each pair */
    for (int i = 0; i < 8; i++) {
        unsigned char w = (unsigned char)(0xA0 + i), r;
        if (write_full(m[i], &w, 1) != 1) goto done;
        if (read_full(s[i], &r, 1) != 1 || r != w) goto done;
    }
    rc = 0;
done:
    for (int i = 0; i < n; i++) { close(m[i]); close(s[i]); }
    return rc;
}

TEST(open_close_churn) {
    for (int i = 0; i < 200; i++) {
        int m, s;
        if (open_pty(&m, &s) != 0) {
            CHECK(0, "open_pty during churn");
        }
        close(s); close(m);
    }
    return 0;
}

/* ------------------------------------------------------------------ *
 * fork + login-shape echo loop                                        *
 * ------------------------------------------------------------------ */

TEST(login_shape_echo) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(m) == 0, "raw master");
    CHECK(set_raw(s) == 0, "raw slave");
    pid_t child = fork();
    CHECK(child >= 0, "fork");
    if (child == 0) {
        close(m);
        /* tiny echo loop: read from slave, write it back */
        char b[64];
        for (;;) {
            ssize_t r = read(s, b, sizeof(b));
            if (r <= 0) break;
            if (write_full(s, b, (size_t)r) != r) break;
        }
        close(s);
        _exit(0);
    }
    close(s);
    int rc = 0;
    for (int i = 0; i < 300; i++) {
        char line[24];
        int len = snprintf(line, sizeof(line), "L%d\n", i);
        if (write_full(m, line, (size_t)len) != len) { rc = -1; break; }
        char in[24];
        if (read_full(m, in, (size_t)len) != len) { rc = -1; break; }
        if (memcmp(line, in, (size_t)len) != 0) { rc = -1; break; }
    }
    close(m);
    kill(child, SIGKILL);
    waitpid(child, NULL, 0);
    CHECK(rc == 0, "login-shape echo round-trips");
    return 0;
}

/* ------------------------------------------------------------------ *
 * STRESS: lost-wakeup hunters                                          *
 * ------------------------------------------------------------------ */

/* Ping-pong single bytes between master and slave.  The slave side runs
 * in a thread; both sides block on read and must be woken by the peer's
 * write.  This hammers the tty read/write wakeup path that exhibits the
 * lost-wakeup bug. */
struct pingpong { int rfd, wfd; int rounds; int ok; };
static void *pong_thread(void *a) {
    struct pingpong *pp = a;
    unsigned char b;
    for (int i = 0; i < pp->rounds; i++) {
        if (read_full(pp->rfd, &b, 1) != 1) { pp->ok = 0; return NULL; }
        if (write_full(pp->wfd, &b, 1) != 1) { pp->ok = 0; return NULL; }
    }
    pp->ok = 1;
    return NULL;
}

TEST(stress_pingpong_bytes) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(m) == 0, "raw master");
    CHECK(set_raw(s) == 0, "raw slave");
    const int ROUNDS = 20000;
    struct pingpong pp = { s, s, ROUNDS, 0 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, pong_thread, &pp) == 0, "thread");
    int rc = 0;
    unsigned char b;
    for (int i = 0; i < ROUNDS; i++) {
        b = (unsigned char)(i & 0xff);
        if (write_full(m, &b, 1) != 1) { rc = -1; break; }
        unsigned char r;
        if (read_full(m, &r, 1) != 1) { rc = -1; break; }
        if (r != b) { rc = -1; break; }
    }
    pthread_join(th, NULL);
    close(s); close(m);
    CHECK(rc == 0, "pingpong data integrity");
    CHECK(pp.ok, "pong thread completed");
    return 0;
}

/* Bulk 1 MiB master->slave; slave reader thread drains and counts. */
struct sink { int fd; size_t want; size_t got; uint32_t sum; };
static void *sink_thread(void *a) {
    struct sink *sk = a;
    unsigned char buf[4096];
    while (sk->got < sk->want) {
        ssize_t r = read(sk->fd, buf, sizeof(buf));
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) break;
        sk->sum = cksum_fold(sk->sum, buf, (size_t)r);
        sk->got += (size_t)r;
    }
    return NULL;
}

TEST(stress_bulk_1mib) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(m) == 0, "raw master");
    CHECK(set_raw(s) == 0, "raw slave");
    const size_t TOTAL = 1024 * 1024;
    struct sink sk = { s, TOTAL, 0, 0 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, sink_thread, &sk) == 0, "thread");
    unsigned char chunk[4096];
    for (size_t i = 0; i < sizeof(chunk); i++) chunk[i] = (unsigned char)(i * 7 + 1);
    int rc = 0;
    size_t sent = 0;
    while (sent < TOTAL) {
        size_t n = TOTAL - sent; if (n > sizeof(chunk)) n = sizeof(chunk);
        if (write_full(m, chunk, n) != (ssize_t)n) { rc = -1; break; }
        sent += n;
    }
    pthread_join(th, NULL);
    close(s); close(m);
    CHECK(rc == 0, "bulk write completed");
    CHECK(sk.got == TOTAL, "slave drained all 1MiB");
    return 0;
}

/* Two threads: one writes the master, one reads the slave; verify the
 * full 256 KiB stream by checksum. */
struct prod { int fd; size_t total; int ok; };
static void *producer_thread(void *a) {
    struct prod *pr = a;
    unsigned char chunk[2048];
    size_t sent = 0;
    uint32_t seed = 0;
    while (sent < pr->total) {
        size_t n = pr->total - sent; if (n > sizeof(chunk)) n = sizeof(chunk);
        for (size_t i = 0; i < n; i++) { seed = seed * 1103515245 + 12345; chunk[i] = (unsigned char)(seed >> 16); }
        if (write_full(pr->fd, chunk, n) != (ssize_t)n) { pr->ok = 0; return NULL; }
        sent += n;
    }
    pr->ok = 1;
    return NULL;
}

TEST(stress_concurrent_256k) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(m) == 0, "raw master");
    CHECK(set_raw(s) == 0, "raw slave");
    const size_t TOTAL = 256 * 1024;
    /* Recompute expected checksum the same way the producer generates it. */
    uint32_t expect = 0, seed = 0;
    {
        size_t left = TOTAL;
        unsigned char chunk[2048];
        while (left) {
            size_t n = left > sizeof(chunk) ? sizeof(chunk) : left;
            for (size_t i = 0; i < n; i++) { seed = seed * 1103515245 + 12345; chunk[i] = (unsigned char)(seed >> 16); }
            expect = cksum_fold(expect, chunk, n);
            left -= n;
        }
    }
    struct prod pr = { m, TOTAL, 0 };
    struct sink sk = { s, TOTAL, 0, 0 };
    pthread_t tw, tr;
    CHECK(pthread_create(&tr, NULL, sink_thread, &sk) == 0, "reader thread");
    CHECK(pthread_create(&tw, NULL, producer_thread, &pr) == 0, "writer thread");
    pthread_join(tw, NULL);
    pthread_join(tr, NULL);
    close(s); close(m);
    CHECK(pr.ok, "producer completed");
    CHECK(sk.got == TOTAL, "consumer drained all bytes");
    CHECK(sk.sum == expect, "stream checksum matches");
    return 0;
}

/* Repeated short blocking round-trips on a fresh pair each iteration:
 * stresses setup/teardown of the wakeup state. */
TEST(stress_setup_teardown) {
    for (int i = 0; i < 400; i++) {
        int m, s;
        if (open_pty(&m, &s) != 0) CHECK(0, "open_pty");
        if (set_raw(m) != 0 || set_raw(s) != 0) { close(m); close(s); CHECK(0, "raw"); }
        unsigned char w = (unsigned char)i, r;
        struct pump p = { m, &w, 1, 0 };
        pthread_t th;
        if (pthread_create(&th, NULL, pump_writer, &p) != 0) { close(m); close(s); CHECK(0, "thread"); }
        ssize_t got = read_full(s, &r, 1);
        pthread_join(th, NULL);
        close(s); close(m);
        if (got != 1 || r != w) CHECK(0, "round-trip integrity");
    }
    return 0;
}

/* Many concurrent pairs each doing a blocking ping-pong simultaneously,
 * maximizing contention on the scheduler wakeup path. */
struct lane { int m, s; int rounds; int ok; };
static void *lane_slave(void *a) {
    struct lane *ln = a;
    unsigned char b;
    for (int i = 0; i < ln->rounds; i++) {
        if (read_full(ln->s, &b, 1) != 1) { ln->ok = 0; return NULL; }
        if (write_full(ln->s, &b, 1) != 1) { ln->ok = 0; return NULL; }
    }
    return NULL;
}
static void *lane_master(void *a) {
    struct lane *ln = a;
    for (int i = 0; i < ln->rounds; i++) {
        unsigned char w = (unsigned char)i, r;
        if (write_full(ln->m, &w, 1) != 1) { ln->ok = 0; return NULL; }
        if (read_full(ln->m, &r, 1) != 1 || r != w) { ln->ok = 0; return NULL; }
    }
    return NULL;
}

TEST(stress_multilane_pingpong) {
    const int LANES = 6;
    const int ROUNDS = 3000;
    struct lane ln[6];
    pthread_t ms[6], ss[6];
    int opened = 0;
    int rc = -1;
    for (int i = 0; i < LANES; i++) {
        if (open_pty(&ln[i].m, &ln[i].s) != 0) goto done;
        opened++;
        if (set_raw(ln[i].m) != 0 || set_raw(ln[i].s) != 0) goto done;
        ln[i].rounds = ROUNDS;
        ln[i].ok = 1;
    }
    for (int i = 0; i < LANES; i++) {
        if (pthread_create(&ss[i], NULL, lane_slave, &ln[i]) != 0) goto join_partial;
    }
    for (int i = 0; i < LANES; i++) {
        if (pthread_create(&ms[i], NULL, lane_master, &ln[i]) != 0) { /* join slaves */
            for (int j = 0; j < LANES; j++) pthread_join(ss[j], NULL);
            goto done;
        }
    }
    for (int i = 0; i < LANES; i++) { pthread_join(ms[i], NULL); pthread_join(ss[i], NULL); }
    rc = 0;
    for (int i = 0; i < LANES; i++) if (!ln[i].ok) rc = -1;
    goto done;
join_partial:
    /* should not normally happen; best-effort cleanup */
    rc = -1;
done:
    for (int i = 0; i < opened; i++) { close(ln[i].m); close(ln[i].s); }
    if (rc != 0) CHECK(0, "multilane pingpong");
    return 0;
}

/* Slow-drip writer: write one byte, sleep, repeat, while the reader is
 * blocked the whole time — each byte must wake the reader exactly once. */
struct drip { int fd; int count; int ok; };
static void *drip_writer(void *a) {
    struct drip *d = a;
    for (int i = 0; i < d->count; i++) {
        unsigned char b = (unsigned char)i;
        if (write_full(d->fd, &b, 1) != 1) { d->ok = 0; return NULL; }
        usleep(500);
    }
    d->ok = 1;
    return NULL;
}

TEST(stress_slow_drip) {
    int m, s;
    CHECK(open_pty(&m, &s) == 0, "open pair");
    CHECK(set_raw(m) == 0, "raw master");
    CHECK(set_raw(s) == 0, "raw slave");
    const int COUNT = 500;
    struct drip d = { m, COUNT, 0 };
    pthread_t th;
    CHECK(pthread_create(&th, NULL, drip_writer, &d) == 0, "thread");
    int rc = 0;
    for (int i = 0; i < COUNT; i++) {
        unsigned char r;
        if (read_full(s, &r, 1) != 1 || r != (unsigned char)i) { rc = -1; break; }
    }
    pthread_join(th, NULL);
    close(s); close(m);
    CHECK(d.ok, "drip writer ok");
    CHECK(rc == 0, "each dripped byte received in order");
    return 0;
}

/* ------------------------------------------------------------------ */

int main(void) {
    signal(SIGPIPE, SIG_IGN);

    fprintf(stdout, "torture_pty: Unix98 PTY torture battery\n");
    fprintf(stdout, "fork-per-test watchdog, %ds timeout/test\n", TEST_TIMEOUT);
    fprintf(stdout, "----------------------------------------------------\n");

    /* clone path / naming */
    RUN(ptmx_open);
    RUN(ptmx_open_noctty);
    RUN(grant_unlock);
    RUN(ptsname_format);
    RUN(slave_open);
    RUN(slave_open_no_noctty);
    RUN(tiocgptn_matches_ptsname);
    RUN(distinct_indices);

    /* data movement raw */
    RUN(m2s_1);
    RUN(m2s_16);
    RUN(m2s_256);
    RUN(m2s_4095);
    RUN(m2s_4096);
    RUN(m2s_9000);
    RUN(s2m_1);
    RUN(s2m_16);
    RUN(s2m_256);
    RUN(s2m_4095);
    RUN(s2m_4096);
    RUN(s2m_9000);
    RUN(raw_all_byte_values);

    /* canonical / echo */
    RUN(icanon_full_line);
    RUN(icanon_partial_then_newline);
    RUN(icanon_erase);
    RUN(echo_master_loopback);

    /* termios */
    RUN(termios_roundtrip_basic);
    RUN(termios_lflag_bits);
    RUN(termios_iflag_oflag_bits);
    RUN(termios_cflag_bits);
    RUN(termios_baud_roundtrip);
    RUN(termios_tcsadrain);
    RUN(termios_cc_roundtrip);

    /* VMIN/VTIME */
    RUN(vmin0_vtime_timeout);
    RUN(vmin_n_waits);

    /* winsize */
    RUN(winsize_roundtrip);
    RUN(winsize_sigwinch);

    /* control chars -> signals */
    RUN(ctrl_intr_sigint);
    RUN(ctrl_quit_sigquit);
    RUN(ctrl_susp_sigtstp);

    /* EOF / hangup */
    RUN(veof_returns_zero);
    RUN(master_close_eof);
    RUN(slave_close_eof);

    /* flush / drain / flow */
    RUN(tcflush_input);
    RUN(tcflush_output);
    RUN(tcflush_both);
    RUN(tcdrain_slave);
    RUN(tcflow_supported);

    /* nonblock */
    RUN(nonblock_master_eagain);
    RUN(nonblock_slave_eagain);

    /* poll / select */
    RUN(poll_master_pollin);
    RUN(poll_master_pollout);
    RUN(select_slave_readable);

    /* many ptys / churn */
    RUN(eight_pairs_roundtrip);
    RUN(open_close_churn);

    /* login-shape */
    RUN(login_shape_echo);

    /* stress: lost-wakeup hunters */
    RUN(stress_pingpong_bytes);
    RUN(stress_bulk_1mib);
    RUN(stress_concurrent_256k);
    RUN(stress_setup_teardown);
    RUN(stress_multilane_pingpong);
    RUN(stress_slow_drip);

    fprintf(stdout, "----------------------------------------------------\n");
    fprintf(stdout, "Result: %d/%d passed", tests_pass, tests_run);
    if (tests_fail) fprintf(stdout, ", %d FAILED", tests_fail);
    if (tests_hang) fprintf(stdout, ", %d HANG", tests_hang);
    if (tests_skip) fprintf(stdout, ", %d skipped", tests_skip);
    fprintf(stdout, "\n");

    return (tests_fail || tests_hang) ? 1 : 0;
}
