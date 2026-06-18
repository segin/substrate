/* fork_sharedconn — does a fork-SHARED AF_UNIX connection survive the
 * other fork closing/exiting?  Models TDEUniqueApplication: a process
 * holds a DCOP (AF_UNIX) connection, fork()s, the PARENT exits (closing
 * its copy of the shared fd), and the CHILD keeps using the SAME fd.  If
 * the kernel refcounts the connection per-fd-table-entry (POSIX), the
 * child's connection survives; if closing one fork's copy tears down the
 * shared socket, the child's connection dies — which is exactly the kded
 * "registered then not accessible" symptom.
 *
 *   host:       cc -O2 fork_sharedconn.c -pthread -o fork_sharedconn
 *   substrate:  i386-unknown-substrate-gcc -O2 fork_sharedconn.c -lpthread -o fork_sharedconn
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

static char g_path[96];
static int wr(int fd,const void*b,size_t n){const char*p=b;size_t o=0;while(o<n){ssize_t w=write(fd,p+o,n-o);if(w<0){if(errno==EINTR)continue;return -1;}if(!w)return -1;o+=(size_t)w;}return 0;}
static int rd(int fd,void*b,size_t n){char*p=b;size_t o=0;while(o<n){ssize_t r=read(fd,p+o,n-o);if(r<0){if(errno==EINTR)continue;return -1;}if(!r)return -1;o+=(size_t)r;}return 0;}

/* echo server: accept one client, echo a 4-byte message back. */
static void *server(void *a){
    int lfd=*(int*)a;
    int c=accept(lfd,NULL,NULL);
    if(c<0)return NULL;
    char b[4];
    if(rd(c,b,4)==0) (void)wr(c,b,4);
    /* keep open so the client can read the echo, then idle */
    usleep(500*1000);
    close(c);
    return NULL;
}

int main(void){
    signal(SIGPIPE,SIG_IGN);
    snprintf(g_path,sizeof g_path,"/tmp/fsc_%ld",(long)getpid());
    int lfd=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
    strncpy(sa.sun_path,g_path,sizeof(sa.sun_path)-1); unlink(g_path);
    if(bind(lfd,(struct sockaddr*)&sa,sizeof sa)!=0||listen(lfd,4)!=0){printf("bind/listen fail\n");return 2;}
    pthread_t st; pthread_create(&st,NULL,server,&lfd);

    /* connect — this is the "DCOP connection" */
    int c=socket(AF_UNIX,SOCK_STREAM,0);
    for(int t=0;t<200;t++){ if(connect(c,(struct sockaddr*)&sa,sizeof sa)==0)break; usleep(3000);}

    /* a pipe so the child waits until the parent has exited */
    int pp[2]; if(pipe(pp)!=0){printf("pipe fail\n");return 2;}

    pid_t p=fork();
    if(p==0){
        /* CHILD: wait for parent to exit (pipe EOF), THEN use the shared fd */
        close(pp[1]);
        char tmp; (void)read(pp[0],&tmp,1);     /* blocks until parent exits -> EOF */
        close(pp[0]);
        int ok = (wr(c,"PING",4)==0);
        char r[4]={0};
        ok = ok && (rd(c,r,4)==0) && memcmp(r,"PING",4)==0;
        close(c);
        _exit(ok?0:1);
    }
    /* PARENT: close our copy of the shared connection + exit, leaving the
     * child to use it.  (Exit via the pipe-close so the child proceeds.) */
    close(pp[0]);
    close(c);                 /* drop the parent's ref to the shared fd */
    close(pp[1]);             /* signal child (EOF) that parent is done */
    int st_;
    pid_t r=waitpid(p,&st_,0);
    int child_ok = (r==p && WIFEXITED(st_) && WEXITSTATUS(st_)==0);
    pthread_join(st,NULL);
    close(lfd); unlink(g_path);
    printf("fork_sharedconn: shared AF_UNIX connection, parent closes, child uses\n");
    printf("  RESULT: %s\n", child_ok ? "PASS (connection refcounted, child kept it)"
                                       : "FAIL (child's shared connection died)");
    return child_ok?0:1;
}
