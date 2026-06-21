/* fork_close_sharedfd — a connected AF_UNIX socket fd shared across fork():
 * when the CHILD closes its inherited copy and exits, the PARENT's copy must
 * stay fully usable and the PEER must NOT see EOF.  tdeinit forks helper
 * processes (TDELauncher/...) that inherit its X-server connection fd and
 * close() it via close_fds(); if that tears the connection down for tdeinit
 * itself, tdeinit's X IO-error handler fires kill(0,SIGHUP) and the whole
 * desktop session dies — exactly the starttde "tdeinit dies after TDELauncher"
 * symptom.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>
static int g_total=0,g_fail=0;
static void chk(int c,const char*n){g_total++; if(c)printf("[%d] %-44s PASS\n",g_total,n); else{g_fail++;printf("[%d] %-44s FAIL\n",g_total,n);}}
int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    int sv[2];
    if(socketpair(AF_UNIX,SOCK_STREAM,0,sv)){printf("socketpair fail\n");return 2;}
    /* sv[0] = "tdeinit's X-conn end", sv[1] = "Xfbdev's end" */
    printf("fork_close_sharedfd: child closes inherited fd, parent must survive\n");
    pid_t pid=fork();
    if(pid==0){
        /* child inherited sv[0] and sv[1]; close them like close_fds() does */
        close(sv[0]);
        close(sv[1]);
        _exit(0);
    }
    int st; waitpid(pid,&st,0);   /* child has closed its copies and exited */

    /* parent still holds sv[0] and sv[1]: the connection must be intact */
    int w = write(sv[0],"PING",4);
    chk(w==4, "parent_write_after_child_close");
    char b[8]={0}; int r = read(sv[1],b,4);
    chk(r==4 && memcmp(b,"PING",4)==0, "parent_read_after_child_close");
    if(r!=4) printf("    (read r=%d errno=%d — connection torn down by child close)\n", r, errno);
    /* peer (sv[1]) must NOT be at EOF: a non-blocking peek style check */
    int w2 = write(sv[1],"PONG",4);
    char b2[8]={0}; int r2 = (w2==4)? read(sv[0],b2,4):-1;
    chk(r2==4 && memcmp(b2,"PONG",4)==0, "peer_not_EOF_bidirectional");

    close(sv[0]); close(sv[1]);
    printf("%s (%d/%d)\n", g_fail?"RESULT: FAIL":"RESULT: PASS", g_total-g_fail, g_total);
    return g_fail?1:0;
}
