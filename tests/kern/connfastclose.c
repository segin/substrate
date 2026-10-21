/*
 * connfastclose -- a blocking connect() returns when the server accepts
 * and closes at once.
 *
 * tcp_connect waited for the connection to be exactly ESTABLISHED.  A
 * server that accepts and closes immediately (telnetd forking a session
 * that exits) can carry the client's end on to CLOSE_WAIT before the
 * connecting thread runs again; connect() then never returned, and
 * torture_socket hung in its threaded scenario.  A server process
 * accepts and closes 300 connections as fast as it can while the client
 * connects to it 300 times with blocking connect(); every connect must
 * return 0, and an alarm turns the old hang into a FAIL.  Portable POSIX.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define ROUNDS 300

static int done;

static void on_alarm(int sig)
{
    (void)sig;
    char msg[96];
    int n = snprintf(msg, sizeof msg,
                     "connfastclose: connect() hung after %d rounds: FAIL\n", done);
    write(1, msg, (size_t)n);
    _exit(1);
}

int main(void)
{
    struct sockaddr_in sa;
    socklen_t sl = sizeof sa;
    int one = 1;

    signal(SIGALRM, on_alarm);
    alarm(60);
    int l = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(l, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (l < 0 || bind(l, (struct sockaddr *)&sa, sizeof sa) != 0 ||
        listen(l, 16) != 0 || getsockname(l, (struct sockaddr *)&sa, &sl) != 0) {
        printf("connfastclose: listen failed (errno %d): FAIL\n", errno);
        return 1;
    }

    pid_t srv = fork();
    if (srv == 0) {
        for (int i = 0; i < ROUNDS; i++) {
            int c = accept(l, NULL, NULL);
            if (c >= 0)
                close(c);       /* close at once: the FIN races connect() */
        }
        _exit(0);
    }
    close(l);

    int fails = 0;
    for (done = 0; done < ROUNDS; done++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0 || connect(s, (struct sockaddr *)&sa, sizeof sa) != 0) {
            printf("connfastclose: connect %d failed (errno %d)\n", done, errno);
            fails++;
            if (s >= 0)
                close(s);
            break;
        }
        close(s);
    }
    alarm(0);
    kill(srv, SIGKILL);
    waitpid(srv, NULL, 0);
    printf("connfastclose: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
