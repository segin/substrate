/*
 * torture_tcp_net.c — two-machine TCP torture client (substrate side).
 *
 * Drives substrate's TCP/IPv4 stack against a real remote peer
 * (tcp_partner running on another host on the LAN), so the path under
 * test is the NIC + driver + IP + TCP, not loopback.  Each scenario
 * runs on a fresh connection and verifies payload integrity byte-for-
 * byte via the shared LCG stream.
 *
 *   build:  i386-unknown-substrate-gcc -o torture_tcp_net torture_tcp_net.c
 *   run:    torture_tcp_net <partner-host> [port]
 *
 * The partner address is ALWAYS an argument — never compiled in.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "tcp_torture.h"

static const char *g_host;
static uint16_t    g_port;
static int failures = 0;

static void ok(const char *name, int pass, const char *detail) {
    printf("  %-22s %s%s%s\n", name, pass ? "PASS" : "FAIL",
           detail && *detail ? " — " : "", detail ? detail : "");
    if (!pass) failures++;
}

/* Open a fresh connection to the partner.  Returns fd or -1. */
static int tt_connect(void) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(g_port);
    sa.sin_addr.s_addr = inet_addr(g_host);
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int send_req(int fd, uint32_t scen, uint32_t len, uint32_t seed, uint32_t flags) {
    struct tt_req req;
    req.magic    = htonl(TT_MAGIC);
    req.scenario = htonl(scen);
    req.length   = htonl(len);
    req.seed     = htonl(seed);
    req.flags    = htonl(flags);
    return tt_writen(fd, &req, sizeof req) == (ssize_t)sizeof req ? 0 : -1;
}

static int read_reply(int fd, struct tt_reply *rep) {
    if (tt_readn(fd, rep, sizeof *rep) != (ssize_t)sizeof *rep) return -1;
    rep->status   = ntohl(rep->status);
    rep->received = ntohl(rep->received);
    return 0;
}

/* SC_ECHO: send N LCG bytes, read them back, verify they match. */
static void sc_echo(uint32_t len, uint32_t seed) {
    int fd = tt_connect();
    if (fd < 0) { ok("echo connect", 0, strerror(errno)); return; }
    char d[48];
    if (send_req(fd, SC_ECHO, len, seed, 0) != 0) { ok("echo", 0, "req"); close(fd); return; }
    if (tt_send_stream(fd, seed, len) != 0)        { ok("echo", 0, "send"); close(fd); return; }
    uint32_t rcv = 0;
    long v = tt_verify_stream(fd, seed, len, &rcv);
    snprintf(d, sizeof d, "%u B, echo back %u B", len, rcv);
    ok("echo round-trip", v == 0, d);
    close(fd);
}

/* SC_DOWNLOAD: partner sends N LCG bytes; verify them. */
static void sc_download(uint32_t len, uint32_t seed) {
    int fd = tt_connect();
    if (fd < 0) { ok("download connect", 0, strerror(errno)); return; }
    char d[48];
    if (send_req(fd, SC_DOWNLOAD, len, seed, 0) != 0) { ok("download", 0, "req"); close(fd); return; }
    uint32_t rcv = 0;
    long v = tt_verify_stream(fd, seed, len, &rcv);
    snprintf(d, sizeof d, "got %u/%u B%s", rcv, len, v > 0 ? " (mismatch)" : "");
    ok("download verify", v == 0, d);
    close(fd);
}

/* SC_UPLOAD: send N LCG bytes; partner verifies + reports. */
static void sc_upload(uint32_t len, uint32_t seed) {
    int fd = tt_connect();
    if (fd < 0) { ok("upload connect", 0, strerror(errno)); return; }
    char d[48];
    if (send_req(fd, SC_UPLOAD, len, seed, 0) != 0) { ok("upload", 0, "req"); close(fd); return; }
    if (tt_send_stream(fd, seed, len) != 0)          { ok("upload", 0, "send"); close(fd); return; }
    struct tt_reply rep;
    if (read_reply(fd, &rep) != 0) { ok("upload", 0, "no reply"); close(fd); return; }
    snprintf(d, sizeof d, "partner got %u/%u B status=%u", rep.received, len, rep.status);
    ok("upload verify", rep.status == 0 && rep.received == len, d);
    close(fd);
}

/* SC_SLOWREAD: send N bytes to a partner that drains slowly — exercises
 * substrate's send-side flow control / zero-window persist timer. */
static void sc_slowread(uint32_t len, uint32_t seed, uint32_t delay_ms) {
    int fd = tt_connect();
    if (fd < 0) { ok("slowread connect", 0, strerror(errno)); return; }
    char d[56];
    if (send_req(fd, SC_SLOWREAD, len, seed, delay_ms) != 0) { ok("slowread", 0, "req"); close(fd); return; }
    if (tt_send_stream(fd, seed, len) != 0)                   { ok("slowread", 0, "send"); close(fd); return; }
    struct tt_reply rep;
    if (read_reply(fd, &rep) != 0) { ok("slowread", 0, "no reply"); close(fd); return; }
    snprintf(d, sizeof d, "drained %u/%u B @ %ums status=%u", rep.received, len, delay_ms, rep.status);
    ok("slowread backpressure", rep.status == 0 && rep.received == len, d);
    close(fd);
}

/* SC_HALFCLOSE: send N bytes then SHUT_WR; read reply then EOF. */
static void sc_halfclose(uint32_t len, uint32_t seed) {
    int fd = tt_connect();
    if (fd < 0) { ok("halfclose connect", 0, strerror(errno)); return; }
    char d[48];
    if (send_req(fd, SC_HALFCLOSE, len, seed, 0) != 0) { ok("halfclose", 0, "req"); close(fd); return; }
    if (tt_send_stream(fd, seed, len) != 0)             { ok("halfclose", 0, "send"); close(fd); return; }
    shutdown(fd, SHUT_WR);
    struct tt_reply rep;
    if (read_reply(fd, &rep) != 0) { ok("halfclose", 0, "no reply"); close(fd); return; }
    snprintf(d, sizeof d, "partner got %u/%u B status=%u", rep.received, len, rep.status);
    ok("halfclose drain", rep.status == 0 && rep.received == len, d);
    close(fd);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <partner-host> [port]\n", argv[0]);
        return 2;
    }
    g_host = argv[1];
    g_port = (uint16_t)((argc > 2) ? atoi(argv[2]) : atoi(TT_PORT));
    printf("torture_tcp_net: partner %s:%u\n\n", g_host, g_port);

    printf("sc1: small echo round-trip\n");
    sc_echo(64, 0x1111);

    printf("sc2: connection churn (40 short echoes)\n");
    {
        int bad = 0;
        for (int i = 0; i < 40; i++) {
            int fd = tt_connect();
            if (fd < 0) { bad++; continue; }
            uint32_t seed = 0x2000u + (uint32_t)i;
            if (send_req(fd, SC_ECHO, 200, seed, 0) != 0 ||
                tt_send_stream(fd, seed, 200) != 0 ||
                tt_verify_stream(fd, seed, 200, NULL) != 0) bad++;
            close(fd);
        }
        char d[32]; snprintf(d, sizeof d, "%d/40 ok", 40 - bad);
        ok("churn 40 conns", bad == 0, d);
    }

    printf("sc3: bulk download (4 MiB)\n");
    sc_download(4u * 1024 * 1024, 0x3333);

    printf("sc4: bulk upload (4 MiB)\n");
    sc_upload(4u * 1024 * 1024, 0x4444);

    printf("sc5: slow-reader backpressure (256 KiB @ 20ms/4K)\n");
    sc_slowread(256u * 1024, 0x5555, 20);

    printf("sc6: half-close (128 KiB then SHUT_WR)\n");
    sc_halfclose(128u * 1024, 0x6666);

    printf("\nResult: %s (%d failure%s)\n",
           failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures;
}
