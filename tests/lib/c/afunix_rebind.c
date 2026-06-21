/* afunix_rebind — reproduce the tdeinit restart pattern: a listener binds a
 * path, is shut down, a NEW listener binds the SAME path, and a client connects.
 * The connect must reach the new listener (not ECONNREFUSED from a stale bound
 * entry / not-LISTENING state).  This is the tdeinit "Shutting down running
 * client" -> rebind -> ksmserver connect -> error 111 loop.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
static int g_total=0,g_fail=0;
static void chk(int c,const char*n){g_total++; if(c)printf("[%d] %-38s PASS\n",g_total,n); else{g_fail++;printf("[%d] %-38s FAIL\n",g_total,n);}}
static int mklisten(const char*p){
    int fd=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
    strncpy(sa.sun_path,p,sizeof(sa.sun_path)-1);
    if(bind(fd,(struct sockaddr*)&sa,sizeof sa)!=0){printf("  bind(%s) errno=%d\n",p,errno);close(fd);return -1;}
    if(listen(fd,5)!=0){printf("  listen errno=%d\n",errno);close(fd);return -1;}
    return fd;
}
static int tryconnect(const char*p){
    int c=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
    strncpy(sa.sun_path,p,sizeof(sa.sun_path)-1);
    int r=connect(c,(struct sockaddr*)&sa,sizeof sa);
    int e=errno; if(r!=0)printf("    connect errno=%d %s\n",e,strerror(e));
    if(r==0)close(c); else close(c);
    return r;
}
int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    char p[64]; snprintf(p,sizeof p,"/tmp/arb_%ld",(long)getpid());
    unlink(p);
    printf("afunix_rebind: stale-rebind (tdeinit restart pattern)\n");

    /* 1. baseline: bind/listen, connect reaches it */
    int a=mklisten(p);
    chk(a>=0 && tryconnect(p)==0, "connect_to_first_listener");

    /* 2. close the first listener WITHOUT unlink (tdeinit terminate path),
     *    then a connect must NOT reach a stale/dead listener */
    close(a);
    int e2=tryconnect(p);
    chk(e2!=0, "connect_after_close_refused_or_enoent");

    /* 3. THE tdeinit case: unlink + rebind a NEW listener on the same path,
     *    connect must reach the NEW one (not stale-binding ECONNREFUSED) */
    unlink(p);
    int b=mklisten(p);
    chk(b>=0, "rebind_same_path");
    chk(b>=0 && tryconnect(p)==0, "connect_reaches_new_listener");

    /* 4. repeat the rebind a few times (the loop churns many times) */
    int loops_ok=1;
    for(int i=0;i<5;i++){
        close(b); unlink(p);
        b=mklisten(p);
        if(b<0 || tryconnect(p)!=0){ loops_ok=0; printf("    loop %d failed\n",i); break; }
    }
    chk(loops_ok, "rebind_loop_5x_stays_connectable");

    if(b>=0)close(b); unlink(p);
    printf("%s (%d/%d)\n", g_fail?"RESULT: FAIL":"RESULT: PASS", g_total-g_fail, g_total);
    return g_fail?1:0;
}
