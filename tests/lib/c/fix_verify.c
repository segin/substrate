/* fix_verify — validate two kernel fixes:
 *   1. AF_UNIX write to a disconnected peer returns EPIPE (not 0) — the
 *      X-freeze fix (a 0 return spun the X server's write loop forever).
 *   2. ext2 deferred metadata flush: create + sync + read back many files,
 *      contents intact (the flush-storm coalescing must not corrupt).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>

static int g_total=0, g_fail=0;
static void chk(int c,const char*n){ g_total++; if(c) printf("[%d] %-34s PASS\n",g_total,n); else {g_fail++; printf("[%d] %-34s FAIL\n",g_total,n);} }

#define NFILES 300

int main(void){
    signal(SIGPIPE, SIG_IGN);    /* so a broken-pipe write returns EPIPE, not kill */
    setvbuf(stdout,NULL,_IONBF,0);
    printf("fix_verify: af_unix EPIPE + ext2 deferred flush\n");

    /* 1. AF_UNIX write to a closed peer -> EPIPE, must NOT return 0 or hang */
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) { chk(0,"socketpair"); }
    else {
        close(sv[1]);                       /* disconnect the peer */
        errno = 0;
        ssize_t r = write(sv[0], "hello", 5);
        chk(r < 0 && errno == EPIPE, "afunix_write_closed_peer_EPIPE");
        if (!(r < 0 && errno == EPIPE))
            printf("    (got r=%zd errno=%d %s — a 0/loop is the bug)\n", r, errno, strerror(errno));
        close(sv[0]);
    }

    /* 2. ext2 create + sync + read back NFILES files */
    mkdir("/root/ftest", 0755);
    int ok = 1;
    for (int i = 0; i < NFILES && ok; i++) {
        char path[64]; snprintf(path, sizeof path, "/root/ftest/f%04d", i);
        int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fd < 0) { printf("    create %s failed errno=%d\n", path, errno); ok = 0; break; }
        char c[40]; int len = snprintf(c, sizeof c, "data-for-file-%04d-end", i);
        if (write(fd, c, (size_t)len) != len) ok = 0;
        close(fd);
    }
    chk(ok, "ext2_create_300_files");

    sync();                                  /* flush deferred metadata */

    int rok = 1;
    for (int i = 0; i < NFILES && rok; i++) {
        char path[64]; snprintf(path, sizeof path, "/root/ftest/f%04d", i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) { printf("    reopen %s failed errno=%d\n", path, errno); rok = 0; break; }
        char buf[40] = {0}; ssize_t n = read(fd, buf, sizeof buf); close(fd);
        char exp[40]; int len = snprintf(exp, sizeof exp, "data-for-file-%04d-end", i);
        if (n != len || memcmp(buf, exp, (size_t)len) != 0) {
            printf("    file %d mismatch (n=%zd)\n", i, n); rok = 0;
        }
    }
    chk(rok, "ext2_readback_300_intact");

    /* cleanup so a re-run / fsck sees a tidy tree */
    for (int i = 0; i < NFILES; i++) { char p[64]; snprintf(p,sizeof p,"/root/ftest/f%04d",i); unlink(p); }
    rmdir("/root/ftest");
    sync();

    printf("%s (%d/%d)\n", g_fail?"RESULT: FAIL":"RESULT: PASS", g_total-g_fail, g_total);
    return g_fail?1:0;
}
