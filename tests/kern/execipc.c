/*
 * execipc — exec drops the old image's IPC handles.
 *
 * A System V shm attachment, a POSIX message-queue descriptor (with an
 * mq_notify registration) and a named semaphore all used to survive exec:
 * shm_nattch stayed up and a shmdt in the new image could unmap whatever
 * now sat at the old address; the mq descriptor stayed usable and the old
 * registration kept signalling the new image; the semaphore stayed open.
 *
 * The first image sets all three up and execs itself with their ids in
 * argv.  The second image checks that the segment has no attachments, the
 * mq descriptor and semaphore id are dead, and the queue takes a new
 * mq_notify registration.  Substrate only (raw SYS_KSEM_CLOSE).
 */
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/syscall.h>

#define MQNAME  "/execipc"
#define SEMNAME "/execipc"

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static int second_image(int shmid, mqd_t mq, int semid)
{
    struct shmid_ds ds;
    check(shmctl(shmid, IPC_STAT, &ds) == 0 && ds.shm_nattch == 0,
          "shm segment has no attachments after exec");
    shmctl(shmid, IPC_RMID, NULL);

    struct mq_attr a;
    errno = 0;
    check(mq_getattr(mq, &a) == -1 && errno == EBADF,
          "mq descriptor closed by exec");

    mqd_t mq2 = mq_open(MQNAME, O_RDWR);
    check(mq2 != (mqd_t)-1, "queue reopens by name");
    if (mq2 != (mqd_t)-1) {
        /* A message into the empty queue fires any registration still in
         * place: SIGUSR1, whose action exec reset to default, kills us. */
        check(mq_send(mq2, "x", 1, 0) == 0, "mq_send into the empty queue");
        check(1, "old mq_notify registration dropped (no SIGUSR1)");
        mq_close(mq2);
    }
    mq_unlink(MQNAME);

    check(syscall(SYS_KSEM_CLOSE, (long)semid) != 0,
          "named semaphore closed by exec");
    sem_unlink(SEMNAME);

    printf("execipc: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc == 4)
        return second_image(atoi(argv[1]), (mqd_t)atoi(argv[2]),
                            atoi(argv[3]));

    int shmid = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    void *p = shmid < 0 ? (void *)-1 : shmat(shmid, NULL, 0);
    check(p != (void *)-1, "shm attach");

    mq_unlink(MQNAME);
    struct mq_attr qa = { .mq_maxmsg = 4, .mq_msgsize = 16 };
    mqd_t mq = mq_open(MQNAME, O_RDWR | O_CREAT, 0600, &qa);
    check(mq != (mqd_t)-1, "mq_open");
    struct sigevent ev = { .sigev_notify = SIGEV_SIGNAL,
                           .sigev_signo = SIGUSR1 };
    check(mq_notify(mq, &ev) == 0, "mq_notify");

    sem_unlink(SEMNAME);
    sem_t *s = sem_open(SEMNAME, O_CREAT, 0600, 1);
    check(s != SEM_FAILED, "sem_open");
    if (fails) {
        printf("execipc: FAIL (setup)\n");
        return 1;
    }

    char a1[16], a2[16], a3[16];
    snprintf(a1, sizeof(a1), "%d", shmid);
    snprintf(a2, sizeof(a2), "%d", (int)mq);
    snprintf(a3, sizeof(a3), "%d", s->__sem_id);
    char *nargv[] = { argv[0], a1, a2, a3, NULL };
    execv(argv[0], nargv);
    perror("execv");
    return 1;
}
