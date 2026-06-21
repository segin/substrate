/* afunix_backlog_leak — does a queued connection whose CLIENT closes before
 * the server accept()s it leak the listener's backlog slot?  tdeinit listens
 * but is often slow to accept under load; clients connect and some go away.
 * If the backlog accounting leaks, after enough cycles the listener refuses
 * ALL further connects with ECONNREFUSED (111) — the tdeinit respawn loop.
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
static void chk(int c,const char*n){g_total++; if(c)printf("[%d] %-44s PASS\n",g_total,n); else{g_fail++;printf("[%d] %-44s FAIL\n",g_total,n);}}
int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    char p[64]; snprintf(p,sizeof p,"/tmp/abl_%ld",(long)getpid()); unlink(p);
    int lfd=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
    strncpy(sa.sun_path,p,sizeof(sa.sun_path)-1);
    if(bind(lfd,(struct sockaddr*)&sa,sizeof sa)||listen(lfd,32)){printf("bind/listen fail %d\n",errno);return 2;}
    printf("afunix_backlog_leak: connect+close (no server accept) x100\n");

    /* 100 clients connect then immediately close, server NEVER accepts.
     * Each should succeed (backlog has room since closed ones must free up). */
    int connect_fails=0;
    for(int i=0;i<100;i++){
        int c=socket(AF_UNIX,SOCK_STREAM,0);
        if(connect(c,(struct sockaddr*)&sa,sizeof sa)!=0){ connect_fails++; if(connect_fails<=3) printf("    cycle %d connect errno=%d\n",i,errno); }
        close(c);   /* close before any accept — does the slot free? */
    }
    chk(connect_fails==0, "100x_connect_then_close_all_succeed");
    if(connect_fails) printf("    (%d/100 connects refused — backlog leaked)\n",connect_fails);

    /* after the churn, the server must still be connectable + acceptable */
    int c=socket(AF_UNIX,SOCK_STREAM,0);
    int rc=connect(c,(struct sockaddr*)&sa,sizeof sa);
    chk(rc==0, "still_connectable_after_churn");
    int af=accept(lfd,NULL,NULL);
    chk(af>=0, "still_acceptable_after_churn");
    if(af>=0)close(af); close(c);

    close(lfd); unlink(p);
    printf("%s (%d/%d)\n", g_fail?"RESULT: FAIL":"RESULT: PASS", g_total-g_fail, g_total);
    return g_fail?1:0;
}
