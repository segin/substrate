/*
 * torture_telnetd_iac.c -- telnetd doubles every 0xff it sends.
 *
 * Data from the pty is IAC-stuffed on its way to the client: each 0xff
 * byte goes out twice.  telnetd stuffed into a buffer the size of its read
 * buffer and stopped when it was full, so a full read of 0xff bytes lost
 * its second half.  This test runs telnetd on a loopback port with itself
 * as the "login" program; that writes 1024 bytes of 0xff and exits, and
 * the client must receive 2048 of them after telnetd's option greeting.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define PORT     2323
#define PAYLOAD  1024
#define GREETING 9          /* IAC WILL ECHO, IAC WILL SGA, IAC DO NAWS */

int main(int argc, char **argv) {
    (void)argc;
    if (strcmp(argv[0], "login") == 0) {
        /* Run by telnetd on the pty: one full buffer of 0xff. */
        unsigned char ff[PAYLOAD];
        memset(ff, 0xff, sizeof(ff));
        write(1, ff, sizeof(ff));
        sleep(2);
        return 0;
    }

    printf("torture_telnetd_iac\n");

    char portarg[16];
    snprintf(portarg, sizeof(portarg), "%d", PORT);
    pid_t tpid = fork();
    if (tpid == 0) {
        execl("/sbin/telnetd", "telnetd", "-f", "-p", portarg, "-l", argv[0],
              (char *)NULL);
        _exit(127);
    }
    sleep(1);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(PORT);
    sa.sin_addr.s_addr = htonl(0x7f000001);
    int connected = 0;
    for (int i = 0; i < 20 && !connected; i++) {
        if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) == 0)
            connected = 1;
        else
            usleep(250000);
    }

    static unsigned char got[8192];
    size_t n = 0;
    while (connected && n < sizeof(got)) {
        struct pollfd pfd = { s, POLLIN, 0 };
        if (poll(&pfd, 1, 5000) <= 0)
            break;
        ssize_t r = read(s, got + n, sizeof(got) - n);
        if (r <= 0)
            break;
        n += (size_t)r;
    }
    close(s);
    kill(tpid, SIGKILL);
    waitpid(tpid, NULL, 0);

    size_t ff = 0;
    for (size_t i = GREETING; i < n; i++)
        if (got[i] == 0xff)
            ff++;
    printf("  received %zu bytes, %zu of them 0xff after the greeting\n", n, ff);
    int ok = connected && ff == 2 * PAYLOAD;
    printf("  %s 1024 bytes of 0xff arrive as 2048\n", ok ? "ok  " : "FAIL");
    printf("Result: %s\n", ok ? "PASSED" : "FAILED");
    return ok ? 0 : 1;
}
