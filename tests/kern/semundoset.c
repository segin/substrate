/*
 * semundoset — SETVAL and SETALL discard pending SEM_UNDO adjustments.
 *
 * A value set outright replaces whatever came before it, so the undo a
 * process recorded against the old value must not be applied at its exit.
 * The kernel kept the adjustments, and the exiting process's undo then
 * subtracted from the newly set value.  A child does +1 with SEM_UNDO,
 * the parent sets 5 (SETVAL) and then 7 (SETALL, in a second round); once
 * the child exits the value must still be 5 and then 7.  Also checks a
 * SETALL with one out-of-range value sets none.
 *
 * Linux build (System V IPC); runs on the host too.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/wait.h>
#include <unistd.h>

union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

/* Child: +1 with SEM_UNDO on semaphore 0, tell the parent, wait to die. */
static pid_t upper(int sem, int pfd[2], int gfd[2])
{
    pid_t c = fork();
    if (c == 0) {
        struct sembuf op = { 0, 1, SEM_UNDO };
        char b = 0;
        semop(sem, &op, 1);
        write(pfd[1], &b, 1);
        read(gfd[0], &b, 1);
        _exit(0);
    }
    return c;
}

static int round_trip(int sem, int setall, int value)
{
    int pfd[2], gfd[2];
    char b = 0;
    union semun arg;
    unsigned short vals[2] = { (unsigned short)value, 0 };

    pipe(pfd);
    pipe(gfd);
    pid_t c = upper(sem, pfd, gfd);
    read(pfd[0], &b, 1);
    if (setall) {
        arg.array = vals;
        semctl(sem, 0, SETALL, arg);
    } else {
        arg.val = value;
        semctl(sem, 0, SETVAL, arg);
    }
    write(gfd[1], &b, 1);
    waitpid(c, NULL, 0);
    close(pfd[0]); close(pfd[1]); close(gfd[0]); close(gfd[1]);
    return semctl(sem, 0, GETVAL);
}

int main(void)
{
    int sem = semget(IPC_PRIVATE, 2, IPC_CREAT | 0600);
    if (sem < 0) {
        printf("semundoset: semget failed (errno %d): FAIL\n", errno);
        return 2;
    }
    union semun arg;
    arg.val = 0;
    semctl(sem, 0, SETVAL, arg);

    check(round_trip(sem, 0, 5) == 5, "SETVAL 5 survives the child's undo");
    check(round_trip(sem, 1, 7) == 7, "SETALL 7 survives the child's undo");

    unsigned short bad[2] = { 3, 40000 };
    arg.array = bad;
    errno = 0;
    check(semctl(sem, 0, SETALL, arg) == -1 && errno == ERANGE,
          "SETALL with 40000 is ERANGE");
    check(semctl(sem, 0, GETVAL) == 7, "and sets nothing");

    semctl(sem, 0, IPC_RMID);
    printf("semundoset: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
