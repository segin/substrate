/*
 * poll_relay — model dcopserver's cross-client relay with a poll()-driven
 * broker (NOT blocking I/O, which torture_fork64's broker used and which
 * passed).  A broker poll()s two connected clients and relays a request
 * from client A to client B and B's reply back to A.  All three sides are
 * poll()-driven, exactly like dcopserver + its DCOP clients.
 *
 * `dcop kded` (caller -> dcopserver -> kded -> reply) hangs on substrate;
 * this is the minimal kernel-level reproduction.  A real OS round-trips
 * in milliseconds.
 *
 *   host:       cc -O2 poll_relay.c -pthread -o poll_relay
 *   substrate:  i386-unknown-substrate-gcc -O2 poll_relay.c -lpthread -o poll_relay
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>

static char g_path[96];
static long now_ms(void){ struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1000L+ts.tv_nsec/1000000L; }
static void set_nb(int fd){ fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK); }
static int wr(int fd,const void*b,size_t n){const char*p=b;size_t o=0;while(o<n){ssize_t w=write(fd,p+o,n-o);if(w<0){if(errno==EINTR)continue;if(errno==EAGAIN){usleep(1000);continue;}return -1;}if(!w)return -1;o+=(size_t)w;}return 0;}

/* poll-driven broker: accept A and B; relay A->B and B->A by tag.
 * A registers with "A\n", B with "B\n".  Then A sends a 5-byte "QUERY",
 * broker forwards to B; B sends "REPLY", broker forwards to A. */
static void *broker(void *arg){
    int lfd=*(int*)arg; set_nb(lfd);
    int a=-1,b=-1; char abuf[64]; int alen=0; char bbuf[64]; int blen=0;
    long deadline=now_ms()+25000;
    while(now_ms()<deadline){
        struct pollfd pf[3]; int n=0;
        pf[n].fd=lfd; pf[n].events=POLLIN; n++;
        if(a>=0){pf[n].fd=a;pf[n].events=POLLIN;n++;}
        if(b>=0){pf[n].fd=b;pf[n].events=POLLIN;n++;}
        int r=poll(pf,n,200);
        if(r<=0) continue;
        if(pf[0].revents&POLLIN){ int c=accept(lfd,NULL,NULL); if(c>=0){ set_nb(c); char t[2]={0}; /* read the 2-byte tag (blocking-ish) */ struct pollfd tp={c,POLLIN,0}; if(poll(&tp,1,2000)>0&&read(c,t,2)==2){ if(t[0]=='A')a=c; else b=c; } else close(c);} }
        for(int i=1;i<n;i++){
            if(!(pf[i].revents&POLLIN)) continue;
            int fd=pf[i].fd; char tmp[64]; ssize_t k=read(fd,tmp,sizeof tmp);
            if(k<=0) continue;
            if(fd==a){ /* A's data -> forward to B */ if(b>=0)(void)wr(b,tmp,(size_t)k); else {memcpy(abuf+alen,tmp,(size_t)k);alen+=k;} }
            else if(fd==b){ /* B's data -> forward to A */ if(a>=0)(void)wr(a,tmp,(size_t)k); else {memcpy(bbuf+blen,tmp,(size_t)k);blen+=k;} }
        }
    }
    if(a>=0)close(a); if(b>=0)close(b);
    return NULL;
}

static int conn(void){
    int fd=socket(AF_UNIX,SOCK_STREAM,0); if(fd<0)return -1;
    struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
    strncpy(sa.sun_path,g_path,sizeof(sa.sun_path)-1);
    for(int t=0;t<400;t++){ if(connect(fd,(struct sockaddr*)&sa,sizeof sa)==0)return fd; usleep(3000);}
    close(fd); return -1;
}

/* B (the "service"): connect, register "B", poll for QUERY, send REPLY. */
static void *svc_b(void *arg){ (void)arg;
    int fd=conn(); if(fd<0)return (void*)1;
    if(wr(fd,"B\n",2)!=0){close(fd);return (void*)2;}
    set_nb(fd);
    struct pollfd pf={fd,POLLIN,0};
    int r=poll(&pf,1,15000);
    if(r>0&&(pf.revents&POLLIN)){ char q[16]; ssize_t k=read(fd,q,sizeof q); if(k>0)(void)wr(fd,"REPLY",5); }
    /* keep open a moment so the relay can flush */
    usleep(200*1000); close(fd); return NULL;
}

int main(void){
    signal(SIGPIPE,SIG_IGN);
    snprintf(g_path,sizeof g_path,"/tmp/prelay_%ld",(long)getpid());
    int lfd=socket(AF_UNIX,SOCK_STREAM,0);
    struct sockaddr_un sa; memset(&sa,0,sizeof sa); sa.sun_family=AF_UNIX;
    strncpy(sa.sun_path,g_path,sizeof(sa.sun_path)-1); unlink(g_path);
    if(bind(lfd,(struct sockaddr*)&sa,sizeof sa)!=0||listen(lfd,8)!=0){ printf("bind/listen failed\n"); return 2; }
    pthread_t bt,st; pthread_create(&bt,NULL,broker,&lfd); pthread_create(&st,NULL,svc_b,NULL);
    usleep(300*1000);
    /* A (the "caller"): connect, register, send QUERY, poll for REPLY. */
    int a=conn(); if(a<0){ printf("caller connect failed\n"); return 2; }
    (void)wr(a,"A\n",2);
    usleep(200*1000);                 /* let B register too */
    long t0=now_ms();
    (void)wr(a,"QUERY",5);
    set_nb(a);
    struct pollfd pf={a,POLLIN,0};
    int r=poll(&pf,1,8000);           /* a real OS replies in ms */
    char rep[16]={0}; int got=0;
    if(r>0&&(pf.revents&POLLIN)){ ssize_t k=read(a,rep,sizeof rep); got=(k==5&&memcmp(rep,"REPLY",5)==0); }
    long dt=now_ms()-t0;
    close(a);
    printf("poll_relay: cross-client poll-driven relay round-trip\n");
    if(got) printf("  RESULT: PASS  (reply in %ld ms)\n", dt);
    else    printf("  RESULT: FAIL  (no reply after %ld ms — relay STALLED)\n", dt);
    return got?0:1;
}
