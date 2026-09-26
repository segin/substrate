/*
 * echod — RFC 862 Echo Service daemon.
 *
 * Listens on TCP port 7 and echoes everything back until EOF.  Every
 * accepted connection is served by its own detached thread, created on
 * accept and gone when the client closes, so any number of clients can
 * be connected at once and there is no fixed pool to size.
 */

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

/* libc stdio does no locking of its own; serialise the log lines. */
static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static void log_line(const char *msg) {
    pthread_mutex_lock(&log_lock);
    fprintf(stdout, "echod: %s\n", msg);
    fflush(stdout);
    pthread_mutex_unlock(&log_lock);
}

static void *echo_conn(void *arg) {
    int c = (int)(intptr_t)arg;
    char buf[1024];
    ssize_t n;

    while ((n = recv(c, buf, sizeof(buf), 0)) > 0) {
        ssize_t off = 0;
        while (off < n) {
            ssize_t w = send(c, buf + off, n - off, 0);
            if (w <= 0) goto done;
            off += w;
        }
    }
done:
    close(c);
    log_line("connection closed");
    return NULL;
}

int main(int argc, char **argv) {
    int port = 7;
    if (argc > 1) port = atoi(argv[1]);

    /* A client that goes away while its echo is being sent makes send()
     * raise SIGPIPE, whose default action would kill the daemon and every
     * other client with it.  Ignored, the send fails EPIPE and only that
     * connection's thread ends. */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(s); return 1;
    }
    if (listen(s, SOMAXCONN) < 0) {
        perror("listen"); close(s); return 1;
    }

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    pthread_mutex_lock(&log_lock);
    fprintf(stdout, "echod: listening on port %d\n", port);
    fflush(stdout);
    pthread_mutex_unlock(&log_lock);

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            perror("accept");
            break;
        }
        log_line("accepted connection");

        pthread_t t;
        int err = pthread_create(&t, &attr, echo_conn, (void *)(intptr_t)c);
        if (err) {
            /* Out of threads or memory: drop this client, keep serving. */
            pthread_mutex_lock(&log_lock);
            fprintf(stderr, "echod: pthread_create: %s\n", strerror(err));
            pthread_mutex_unlock(&log_lock);
            close(c);
        }
    }
    pthread_attr_destroy(&attr);
    close(s);
    return 0;
}
