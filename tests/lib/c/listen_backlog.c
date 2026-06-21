/* listen_backlog — does connect() to a listening AF_UNIX socket succeed
 * while the server is NOT in accept() (i.e. is the listen backlog real)?
 *
 * POSIX: after listen(fd, N), the kernel queues up to N completed
 * connections; a client connect() succeeds immediately (the connection
 * sits in the backlog) even if the server has not yet called accept().
 * TDE's start_tdeinit_wrapper connects to the tdeinit daemon socket right
 * after the daemon listens but before it loops into accept(); if substrate
 * refuses such a connect with ECONNREFUSED (111), the wrapper thinks
 * tdeinit isn't running and relaunches it — the dcopserver "already
 * running" restart cascade.
 *
 *   host:       cc -O2 listen_backlog.c -pthread -o listen_backlog
 *   substrate:  i386-unknown-substrate-gcc -O2 listen_backlog.c -lpthread -o listen_backlog
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
#include <pthread.h>

static char g_path[96];
static int g_total=0,g_fail=0;
static void chk(int c,const char*n){ g_total++; if(c) printf("[%d] %-40s PASS\n",g_total,n); else {g_fail++; printf("[%d] %-40s FAIL\n",g_total,n);} }

/* server: bind + listen, then SLEEP without accepting; accept only later */
static void *server(void *a){
    int lfd=*(int*)a;
    /* deliberately do NOT accept for 3s — the client must still connect */
    usleep(3000*1000);
    int c=accept(lfd,NULL,NULL);
    if(c>=0){ char b[4]; if(read(c,b,4)>0) (void)write(c,b,4); usleep(200*1000); close(c); }
    return NULL;
}

int main(void){
    signal(SIGPIPE,SIG_IGN);
    setvbuf(stdout,NULL,_IONBF,0);
    snprintf(g_path,sizeof g_path,"/tmp/lbk_%ld",(long)getpid());
    int lfd=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
    strncpy(sa.sun_path,g_path,sizeof(sa.sun_path)-1); unlink(g_path);
    if(bind(lfd,(struct sockaddr*)&sa,sizeof sa)!=0||listen(lfd,5)!=0){ printf("bind/listen fail\n"); return 2; }
    pthread_t st; pthread_create(&st,NULL,server,&lfd);
    usleep(200*1000);   /* server is now listening but sleeping (not accepting) */

    printf("listen_backlog: connect() while server NOT in accept()\n");
    /* 1. a single connect must succeed against the backlog */
    int c=socket(AF_UNIX,SOCK_STREAM,0);
    int r=connect(c,(struct sockaddr*)&sa,sizeof sa);
    int e=errno;
    chk(r==0, "connect_before_accept_succeeds");
    if(r!=0) printf("    (connect errno=%d %s — backlog not honoured)\n", e, strerror(e));

    /* 2. the queued connection must actually work once accepted */
    int ok2=0;
    if(r==0){
        if(write(c,"PING",4)==4){ char b[4]={0}; if(read(c,b,4)==4 && memcmp(b,"PING",4)==0) ok2=1; }
    }
    chk(ok2, "queued_connection_roundtrips");

    /* 3. multiple connects queue up to the backlog without a server accept */
    int extra[3], okq=1;
    for(int i=0;i<3;i++){
        extra[i]=socket(AF_UNIX,SOCK_STREAM,0);
        struct sockaddr_un s2; memset(&s2,0,sizeof s2); s2.sun_family=AF_UNIX;
        strncpy(s2.sun_path,g_path,sizeof(s2.sun_path)-1);
        if(connect(extra[i],(struct sockaddr*)&s2,sizeof s2)!=0) okq=0;
    }
    chk(okq, "backlog_queues_multiple_connects");

    if(c>=0)close(c); for(int i=0;i<3;i++) if(extra[i]>=0) close(extra[i]);
    pthread_join(st,NULL);
    close(lfd); unlink(g_path);
    printf("%s\n", g_fail==0?"RESULT: PASS":"RESULT: FAIL");
    return g_fail==0?0:1;
}
