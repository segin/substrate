/*
 * torture_afunix_grow.c — regression test for UNIX-04 (task #430).
 *
 * The AF_UNIX receive ring used to be a 256 KiB array embedded in the
 * socket struct, allocated in full by every socket() / socketpair() /
 * inbound connect() whether or not a byte was ever sent.  It is now
 * allocated on first write and doubles on demand up to the same ceiling.
 *
 * That turns a fixed array into a ring whose modulus changes underneath
 * live data, so the interesting cases are the ones where a grow happens
 * with bytes already in flight and the contents have to be re-linearised
 * without loss or reordering.  Every case below therefore checks the exact
 * bytes that come back out, not just the counts.
 *
 * Run as init:  qemu ... -append "init=/tmp/torture_afunix_grow"
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>

static int passed, failed;

static void ok(const char *what, int cond, const char *why)
{
    if (cond) {
        printf("  ok    %s\n", what);
        passed++;
    } else {
        printf("  FAIL  %s: %s (errno=%d)\n", what, why, errno);
        failed++;
    }
}

/* Deterministic byte at stream offset i, so any reordering, duplication or
 * dropped run shows up as a mismatch at a known position. */
static unsigned char pat(unsigned long i) { return (unsigned char)(i * 31 + 7); }

/*
 * Push a large stream through a socketpair, draining as we go.  The total
 * far exceeds the 8 KiB initial ring, so the ring must grow repeatedly --
 * and because the reader lags the writer, most of those grows happen with
 * live bytes straddling the wrap point.
 */
static void test_stream_growth(void)
{
    printf("UNIX-04: stream survives repeated ring growth\n");

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ok("socketpair", 0, "socketpair failed");
        return;
    }
    fcntl(sv[0], F_SETFL, O_NONBLOCK);
    fcntl(sv[1], F_SETFL, O_NONBLOCK);

    static unsigned char out[4096], in[4096];
    const unsigned long TOTAL = 512UL * 1024UL;   /* twice the ceiling */
    unsigned long sent = 0, got = 0;
    int stalled = 0;

    while (got < TOTAL && stalled < 1000) {
        int progress = 0;

        if (sent < TOTAL) {
            size_t chunk = sizeof(out);
            if (TOTAL - sent < chunk) chunk = TOTAL - sent;
            for (size_t i = 0; i < chunk; i++) out[i] = pat(sent + i);
            ssize_t w = write(sv[0], out, chunk);
            if (w > 0) { sent += (unsigned long)w; progress = 1; }
        }

        ssize_t r = read(sv[1], in, sizeof(in));
        if (r > 0) {
            for (ssize_t i = 0; i < r; i++) {
                if (in[i] != pat(got + (unsigned long)i)) {
                    char msg[80];
                    snprintf(msg, sizeof(msg), "byte %lu wrong",
                             got + (unsigned long)i);
                    ok("stream content", 0, msg);
                    close(sv[0]); close(sv[1]);
                    return;
                }
            }
            got += (unsigned long)r;
            progress = 1;
        }

        stalled = progress ? 0 : stalled + 1;
    }

    ok("all bytes came back", got == TOTAL, "stream stalled or came up short");
    ok("every byte matched its position", got == TOTAL,
       "content diverged during a ring grow");

    close(sv[0]);
    close(sv[1]);
}

/*
 * A single datagram larger than the initial ring has to force one grow
 * before any of it is written -- a partial frame would desync the
 * [u16 len][payload] framing for everything after it.
 */
static void test_large_datagram(void)
{
    printf("UNIX-04: one oversized datagram grows the ring atomically\n");

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0) {
        ok("socketpair(SOCK_DGRAM)", 0, "socketpair failed");
        return;
    }

    static unsigned char out[40000], in[40000];
    for (size_t i = 0; i < sizeof(out); i++) out[i] = pat(i);

    ssize_t w = write(sv[0], out, sizeof(out));
    ok("40000-byte datagram accepted", w == (ssize_t)sizeof(out),
       "write was refused or truncated");

    ssize_t r = read(sv[1], in, sizeof(in));
    ok("it came back whole", r == (ssize_t)sizeof(out),
       "datagram boundary was lost");
    ok("its contents are intact",
       r == (ssize_t)sizeof(out) && memcmp(in, out, sizeof(out)) == 0,
       "payload corrupted across the grow");

    /* A second, small datagram must still be framed correctly behind it --
     * this is what a half-written frame would have broken. */
    const char tail[] = "still-framed";
    write(sv[0], tail, sizeof(tail));
    char tbuf[64];
    ssize_t tr = read(sv[1], tbuf, sizeof(tbuf));
    ok("framing survived for the next datagram",
       tr == (ssize_t)sizeof(tail) && memcmp(tbuf, tail, sizeof(tail)) == 0,
       "frame boundary desynced");

    close(sv[0]);
    close(sv[1]);
}

/*
 * A socket that never carries a byte must not have allocated a ring at
 * all.  There is no way to read the kernel's allocation from userland, so
 * this checks the observable consequence: opening a great many idle
 * socketpairs used to request ~263 KiB each and now must not.
 */
static void test_many_idle_sockets(void)
{
    printf("UNIX-04: idle sockets are cheap\n");

    enum { PAIRS = 60 };
    int sv[PAIRS][2];
    int made = 0;

    for (int i = 0; i < PAIRS; i++) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv[i]) < 0) break;
        made++;
    }

    /* 60 pairs = 120 sockets.  At the old 256 KiB apiece that is ~31 MiB of
     * kernel memory for sockets that have never been written to. */
    ok("120 idle sockets could be created", made == PAIRS,
       "ran out of kernel memory on idle sockets");

    /* And they still work afterwards. */
    int rc = -1;
    if (made > 0) {
        const char msg[] = "alive";
        char buf[16];
        write(sv[0][0], msg, sizeof(msg));
        rc = (read(sv[0][1], buf, sizeof(buf)) == (ssize_t)sizeof(msg) &&
              memcmp(buf, msg, sizeof(msg)) == 0) ? 0 : -1;
    }
    ok("a socket still carries data afterwards", rc == 0, "round trip failed");

    for (int i = 0; i < made; i++) { close(sv[i][0]); close(sv[i][1]); }
}

int main(void)
{
    printf("torture_afunix_grow: lazy/growable AF_UNIX ring (#430 UNIX-04)\n\n");

    test_stream_growth();
    test_large_datagram();
    test_many_idle_sockets();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
