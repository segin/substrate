/* ext2_concurrent — hammer the ext2 allocator from several threads at once
 * (each creating files, some in a shared dir) to exercise the fs-level
 * alloc_lock: a double-allocated block/inode would cross-link files and the
 * content readback would mismatch.  Single-CPU still interleaves because an
 * allocation sleeps on the bitmap-block I/O.
 *
 *   substrate: i386-unknown-substrate-gcc -O2 ext2_concurrent.c -lpthread -o ext2_concurrent
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>

#define NTHREADS 4
#define NFILES   40

static int g_fail = 0;

static void *worker(void *arg) {
    int id = (int)(long)arg;
    char dir[32]; snprintf(dir, sizeof dir, "/root/cc%d", id);
    mkdir(dir, 0755);
    /* create */
    for (int i = 0; i < NFILES; i++) {
        char path[64]; snprintf(path, sizeof path, "%s/f%03d", dir, i);
        int fd = open(path, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fd < 0) { __sync_fetch_and_add(&g_fail,1); return NULL; }
        char buf[48]; int len = snprintf(buf, sizeof buf, "thread-%d-file-%03d-payload", id, i);
        if (write(fd, buf, (size_t)len) != len) __sync_fetch_and_add(&g_fail,1);
        close(fd);
    }
    /* read back + verify (a cross-linked/double-allocated block shows as a
     * content mismatch — another thread's payload bleeding in) */
    for (int i = 0; i < NFILES; i++) {
        char path[64]; snprintf(path, sizeof path, "%s/f%03d", dir, i);
        int fd = open(path, O_RDONLY);
        if (fd < 0) { __sync_fetch_and_add(&g_fail,1); continue; }
        char b[48] = {0}; ssize_t n = read(fd, b, sizeof b); close(fd);
        char exp[48]; int len = snprintf(exp, sizeof exp, "thread-%d-file-%03d-payload", id, i);
        if (n != len || memcmp(b, exp, (size_t)len) != 0) __sync_fetch_and_add(&g_fail,1);
    }
    return NULL;
}

int main(void){
    setvbuf(stdout,NULL,_IONBF,0);
    printf("ext2_concurrent: %d threads x %d files\n", NTHREADS, NFILES);
    pthread_t t[NTHREADS];
    for (long i = 0; i < NTHREADS; i++) pthread_create(&t[i], NULL, worker, (void*)i);
    for (int i = 0; i < NTHREADS; i++) pthread_join(t[i], NULL);
    /* cleanup */
    for (int id = 0; id < NTHREADS; id++) {
        for (int i = 0; i < NFILES; i++) { char p[64]; snprintf(p,sizeof p,"/root/cc%d/f%03d",id,i); unlink(p); }
        char d[32]; snprintf(d,sizeof d,"/root/cc%d",id); rmdir(d);
    }
    sync();
    printf("%s (%d mismatches across %d files)\n", g_fail?"RESULT: FAIL":"RESULT: PASS", g_fail, NTHREADS*NFILES);
    return g_fail?1:0;
}
