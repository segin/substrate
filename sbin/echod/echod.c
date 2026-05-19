/*
 * echod — RFC 862 Echo Service daemon.
 *
 * Listens on TCP port 7, echoes everything back until EOF.  No fork
 * per-connection in this simple implementation — single-threaded,
 * handles one client at a time.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

int main(int argc, char **argv) {
    int port = 7;
    if (argc > 1) port = atoi(argv[1]);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = __builtin_bswap16((uint16_t)port);
    addr.sin_addr.s_addr = 0;  /* INADDR_ANY */
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(s); return 1;
    }
    if (listen(s, 5) < 0) {
        perror("listen"); close(s); return 1;
    }
    fprintf(stdout, "echod: listening on port %d\n", port);

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        fprintf(stdout, "echod: accepted connection\n");
        char buf[1024];
        ssize_t n;
        while ((n = recv(c, buf, sizeof(buf), 0)) > 0) {
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = send(c, buf + off, n - off, 0);
                if (w <= 0) break;
                off += w;
            }
        }
        close(c);
        fprintf(stdout, "echod: connection closed\n");
    }
    close(s);
    return 0;
}
