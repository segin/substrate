/* afunix_deadlistener — after the process that bound+listened on an AF_UNIX
 * path DIES (not close(): the whole process exits), a connect() to that path
 * must fail with ECONNREFUSED — the binding is gone, only the stale socket
 * file remains.  If substrate lets the connect SUCCEED, then anything that
 * probes "is a server still here?" by connecting (tdeinit's defer/takeover
 * check) wrongly believes a dead server is alive.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
static int g_total=0,g_fail=0;
static void chk(int c,const char*n){g_total++; if(c)printf("[%d] %-40s PASS\n",g_total,n); else{g_fail++;printf("[%d] %-40s FAIL\n",g_total,n);}}
static int tryconn(const char*p){
    int c=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
    strncpy(sa.sun_path,p,sizeof(sa.sun_path)-1);
    int r=connect(c,(struct sockaddr*)&sa,sizeof sa); int e=errno; close(c);
    if(r!=0) printf("    connect errno=%d %s\n",e,strerror(e));
    return r;
}
int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    char p[64]; snprintf(p,sizeof p,"/tmp/adl_%ld",(long)getpid()); unlink(p);
    printf("afunix_deadlistener: connect after the listener PROCESS dies\n");
    int pfd[2]; pipe(pfd);
    pid_t pid=fork();
    if(pid==0){
        /* child: bind+listen, tell parent we're ready, then sleep then _exit
         * (process death — kernel closes the listen fd) */
        int lfd=socket(AF_UNIX,SOCK_STREAM,0);
        struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
        strncpy(sa.sun_path,p,sizeof(sa.sun_path)-1);
        if(bind(lfd,(struct sockaddr*)&sa,sizeof sa)||listen(lfd,5)){ _exit(2); }
        write(pfd[1],"R",1);
        sleep(2);
        _exit(0);   /* process death, NOT close() */
    }
    char b; read(pfd[0],&b,1);   /* wait until child is listening */
    chk(tryconn(p)==0, "connect_while_listener_alive");
    int st; waitpid(pid,&st,0);  /* child has now DIED */
    /* the socket file still exists on disk, but no process is listening */
    int r=tryconn(p); int e=errno;
    chk(r!=0 && e==ECONNREFUSED, "connect_after_death_ECONNREFUSED");
    if(r==0) printf("    *** connect SUCCEEDED against a DEAD listener — substrate bug ***\n");
    unlink(p);
    printf("%s (%d/%d)\n", g_fail?"RESULT: FAIL":"RESULT: PASS", g_total-g_fail, g_total);
    return g_fail?1:0;
}
