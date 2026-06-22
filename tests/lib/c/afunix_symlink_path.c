/* afunix_symlink_path — AF_UNIX must be keyed by the socket file's inode, not
 * the literal path string: a server binding at /tmp/realdir/sock must be
 * reachable by a client connecting through a symlinked parent directory
 * (/tmp/linkdir -> /tmp/realdir).  This is exactly TDE's layout —
 * ~/.trinity/socket-<host> is a symlink to /tmp/tdesocket-<host>, and kwrapper
 * connects via the symlink while tdeinit binds via the resolved path.  Before
 * the inode-keyed connect() fallback this returned ECONNREFUSED and the TDE
 * desktop's window manager never launched. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>

static int bind_listen(const char *path) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    if (s < 0) return -1;
    struct sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
    strncpy(a.sun_path, path, sizeof(a.sun_path) - 1);
    unlink(path);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0) { perror("bind"); close(s); return -1; }
    if (listen(s, 8) != 0) { perror("listen"); close(s); return -1; }
    return s;
}
static int connect_to(const char *path) {
    int s = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a; memset(&a, 0, sizeof a); a.sun_family = AF_UNIX;
    strncpy(a.sun_path, path, sizeof(a.sun_path) - 1);
    int r = connect(s, (struct sockaddr *)&a, sizeof(a.sun_family) + strlen(path) + 1);
    int e = errno; close(s);
    if (r != 0) { printf("  connect('%s') FAILED errno=%d (%s)\n", path, e, strerror(e)); return -1; }
    printf("  connect('%s') OK\n", path);
    return 0;
}

int main(void) {
    int fails = 0;
    mkdir("/tmp/afux_real", 0777);
    unlink("/tmp/afux_link");
    if (symlink("/tmp/afux_real", "/tmp/afux_link") != 0 && errno != EEXIST)
        printf("symlink: errno=%d\n", errno);

    int srv = bind_listen("/tmp/afux_real/sock");
    if (srv < 0) { printf("RESULT: FAIL (server setup)\n"); return 1; }

    printf("server bound at /tmp/afux_real/sock; testing both paths:\n");
    if (connect_to("/tmp/afux_real/sock") != 0) fails++;   /* resolved path */
    if (connect_to("/tmp/afux_link/sock") != 0) fails++;   /* via symlinked dir */

    close(srv);
    unlink("/tmp/afux_real/sock"); unlink("/tmp/afux_link"); rmdir("/tmp/afux_real");
    printf("%s\n", fails ? "RESULT: FAIL" : "RESULT: PASS");
    return fails ? 1 : 0;
}
