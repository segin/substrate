/*
 * preadshared — pread and pwrite leave the descriptor's offset alone, even
 * for another thread reading through it at the same time.
 *
 * The BSD personalities did pread as lseek(off), read, lseek(back): a
 * thread reading the same descriptor sequentially had its offset moved
 * under it.  One thread here reads a patterned file a byte at a time and
 * checks each byte is the one for its position, while another preads and
 * pwrites at offset 0 in a loop.  Also: pread on a pipe is ESPIPE.
 *
 * FreeBSD build (cc -static ... -lpthread); runs on FreeBSD too.
 */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#define SIZE 8192

static int fd;
static volatile int stop;

static void *hammer(void *arg)
{
    char b = 0;
    (void)arg;
    while (!stop) {
        pread(fd, &b, 1, 0);
        pwrite(fd, &b, 1, 0);      /* the byte that is already there */
    }
    return NULL;
}

int main(void)
{
    char path[] = "/tmp/preadshared.XXXXXX";
    unsigned char buf[SIZE];
    int fails = 0, p[2];
    pthread_t t;

    fd = mkstemp(path);
    if (fd < 0) {
        printf("preadshared: mkstemp failed: FAIL\n");
        return 2;
    }
    for (int i = 0; i < SIZE; i++)
        buf[i] = (unsigned char)(i % 251);
    write(fd, buf, SIZE);
    lseek(fd, 0, SEEK_SET);

    pthread_create(&t, NULL, hammer, NULL);
    int bad = 0;
    for (int pass = 0; pass < 4; pass++) {
        lseek(fd, 0, SEEK_SET);
        for (int i = 0; i < SIZE; i++) {
            unsigned char c;
            if (read(fd, &c, 1) != 1 || c != (unsigned char)(i % 251)) {
                bad++;
                break;
            }
        }
    }
    stop = 1;
    pthread_join(t, NULL);
    printf("%s sequential reads kept their place (%d passes broken)\n",
           bad ? "FAIL " : "ok   ", bad);
    fails += bad != 0;

    pipe(p);
    char c;
    errno = 0;
    int ok = pread(p[0], &c, 1, 0) == -1 && errno == ESPIPE;
    printf("%s pread on a pipe is ESPIPE\n", ok ? "ok   " : "FAIL ");
    fails += !ok;

    close(fd);
    unlink(path);
    printf("preadshared: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
