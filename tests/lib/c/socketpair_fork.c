/* socketpair_fork — the tdeinit<->tdelauncher channel primitive: a
 * socketpair() shared across fork(), with the child dup2'ing its end onto a
 * fixed low fd (tdeinit puts the launcher socket on LAUNCHER_FD), then
 * bidirectional message exchange.  If substrate drops messages here, tdeinit's
 * launch requests to tdelauncher vanish and the desktop never starts.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>
#define LFD 30
static int g_total=0,g_fail=0;
static void chk(int c,const char*n){g_total++; if(c)printf("[%d] %-40s PASS\n",g_total,n); else{g_fail++;printf("[%d] %-40s FAIL\n",g_total,n);}}
int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    int sp[2];
    if(socketpair(AF_UNIX,SOCK_STREAM,0,sp)!=0){printf("socketpair fail %d\n",errno);return 2;}
    printf("socketpair_fork: cross-fork dup2-to-fixed-fd message exchange\n");
    pid_t pid=fork();
    if(pid==0){
        /* child = "tdelauncher": evacuate its end to LFD like tdeinit does */
        close(sp[0]);
        if(sp[1]!=LFD){ dup2(sp[1],LFD); close(sp[1]); }
        /* send "OK" (like LAUNCHER_OK), then echo 20 request/replies */
        write(LFD,"OK",2);
        for(int i=0;i<20;i++){
            char b[8]={0}; int n=read(LFD,b,4);
            if(n!=4){ _exit(10); }
            write(LFD,"PONG",4);
        }
        _exit(0);
    }
    /* parent = "tdeinit": keep sp[0] */
    close(sp[1]);
    char ok[2]={0}; int n=read(sp[0],ok,2);
    chk(n==2 && memcmp(ok,"OK",2)==0, "child_OK_arrives_after_fork_dup2");
    int roundtrips=0;
    for(int i=0;i<20;i++){
        if(write(sp[0],"PING",4)!=4) break;
        char b[8]={0}; if(read(sp[0],b,4)!=4) break;
        if(memcmp(b,"PONG",4)!=0) break;
        roundtrips++;
    }
    chk(roundtrips==20, "20_launch_request_roundtrips");
    if(roundtrips!=20) printf("    (only %d/20 roundtrips — channel drops messages)\n",roundtrips);
    int st=0; waitpid(pid,&st,0);
    chk(WIFEXITED(st)&&WEXITSTATUS(st)==0, "child_clean_exit");
    close(sp[0]);
    printf("%s (%d/%d)\n", g_fail?"RESULT: FAIL":"RESULT: PASS", g_total-g_fail, g_total);
    return g_fail?1:0;
}
