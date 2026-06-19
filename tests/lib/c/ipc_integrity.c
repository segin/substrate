/* ipc_integrity — stream a known pattern through an AF_UNIX socketpair and a
 * pipe, wrapping the kernel ring buffer many times, and verify every byte.
 * Targets the memcpy ring-copy in af_unix.c (afbuf_read/write) and pipe.c:
 * a wrap/offset bug there silently corrupts X/DCOP/shell IPC.
 *
 *   host:       cc -O2 ipc_integrity.c -pthread -o ipc_integrity
 *   substrate:  i386-unknown-substrate-gcc -O2 ipc_integrity.c -lpthread -o ipc_integrity
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>

#define TOTAL (1u << 20)          /* 1 MiB — wraps a 4 KiB ring ~256 times */
static inline unsigned char patt(unsigned long i){ return (unsigned char)((i * 1103515245u + 12345u) >> 16); }

static int g_total=0, g_fail=0;
static void chk(int c,const char*n){ g_total++; if(c) printf("[%d] %-26s PASS\n",g_total,n); else {g_fail++; printf("[%d] %-26s FAIL\n",g_total,n);} }

struct ctx { int fd; };
static void *writer(void *a){
    int fd=((struct ctx*)a)->fd;
    unsigned long off=0;
    unsigned char buf[7001];      /* odd chunk so writes straddle the ring wrap */
    while(off<TOTAL){
        size_t chunk=sizeof buf; if(off+chunk>TOTAL) chunk=TOTAL-off;
        for(size_t k=0;k<chunk;k++) buf[k]=patt(off+k);
        size_t w=0;
        while(w<chunk){ ssize_t r=write(fd,buf+w,chunk-w); if(r<0){if(errno==EINTR)continue; return (void*)1;} w+=(size_t)r; }
        off+=chunk;
    }
    return NULL;
}

/* read TOTAL bytes from fd, verifying each against the pattern */
static int verify_stream(int fd){
    unsigned long off=0; int ok=1;
    unsigned char buf[5003];      /* different odd chunk for the reader */
    while(off<TOTAL){
        size_t want=sizeof buf; if(off+want>TOTAL) want=TOTAL-off;
        ssize_t r=read(fd,buf,want);
        if(r<=0){ if(r<0&&errno==EINTR) continue; ok=0; break; }
        for(ssize_t k=0;k<r;k++) if(buf[k]!=patt(off+(unsigned long)k)){ ok=0; off=TOTAL; break; }
        off+=(unsigned long)r;
    }
    return ok;
}

int main(void){
    signal(SIGPIPE,SIG_IGN);
    setvbuf(stdout,NULL,_IONBF,0);
    printf("ipc_integrity: %u bytes each, wrapping the kernel ring\n", TOTAL);

    /* AF_UNIX socketpair */
    int sp[2];
    if(socketpair(AF_UNIX,SOCK_STREAM,0,sp)!=0){ chk(0,"socketpair"); }
    else {
        struct ctx c={sp[1]}; pthread_t t; pthread_create(&t,NULL,writer,&c);
        int ok=verify_stream(sp[0]);
        pthread_join(t,NULL);
        close(sp[0]); close(sp[1]);
        chk(ok,"afunix_stream_1MB");
    }

    /* pipe */
    int pp[2];
    if(pipe(pp)!=0){ chk(0,"pipe"); }
    else {
        struct ctx c={pp[1]}; pthread_t t; pthread_create(&t,NULL,writer,&c);
        int ok=verify_stream(pp[0]);
        pthread_join(t,NULL);
        close(pp[0]); close(pp[1]);
        chk(ok,"pipe_stream_1MB");
    }

    printf("%s (%d/%d)\n", g_fail?"RESULT: FAIL":"RESULT: PASS", g_total-g_fail, g_total);
    return g_fail?1:0;
}
