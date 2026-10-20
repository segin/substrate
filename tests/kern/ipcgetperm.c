/*
 * ipcgetperm — shmget and semget on an existing key check only the access
 * the caller asks for.
 *
 * Both demanded read and write unconditionally, so another user could not
 * even look up a segment or set that was readable to it: shmget(key, 0, 0)
 * and shmget(key, 0, 0444) failed with EACCES.  Run as root: creates a
 * 0644 segment and set, then as uid 1000 looks them up asking for nothing,
 * then for read (both must work), then for write (must fail).
 *
 * Linux build (System V IPC); runs on the host too.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

#define SHMKEY 0x5e1f0001
#define SEMKEY 0x5e1f0002

int main(void)
{
    int shm = shmget(SHMKEY, 4096, IPC_CREAT | 0644);
    int sem = semget(SEMKEY, 1, IPC_CREAT | 0644);
    if (shm < 0 || sem < 0) {
        printf("ipcgetperm: setup failed (errno %d): FAIL\n", errno);
        return 2;
    }

    pid_t c = fork();
    if (c == 0) {
        int r = 0;
        if (setuid(1000) != 0) _exit(64);
        if (shmget(SHMKEY, 0, 0) != shm) r |= 1;
        if (shmget(SHMKEY, 0, 0444) != shm) r |= 2;
        if (shmget(SHMKEY, 0, 0666) != -1 || errno != EACCES) r |= 4;
        if (semget(SEMKEY, 0, 0) != sem) r |= 8;
        if (semget(SEMKEY, 0, 0444) != sem) r |= 16;
        if (semget(SEMKEY, 0, 0666) != -1 || errno != EACCES) r |= 32;
        _exit(r);
    }
    int st = 0;
    waitpid(c, &st, 0);
    int r = WIFEXITED(st) ? WEXITSTATUS(st) : 255;

    static const char *what[] = {
        "shmget(key, 0, 0) by another user",
        "shmget(key, 0, 0444) by another user",
        "shmget(key, 0, 0666) by another user is EACCES",
        "semget(key, 0, 0) by another user",
        "semget(key, 0, 0444) by another user",
        "semget(key, 0, 0666) by another user is EACCES",
    };
    if (r == 64 || r == 255) {
        printf("FAIL  child could not become uid 1000\n");
    } else {
        for (int i = 0; i < 6; i++)
            printf("%s %s\n", r & (1 << i) ? "FAIL " : "ok   ", what[i]);
    }
    shmctl(shm, IPC_RMID, NULL);
    semctl(sem, 0, IPC_RMID);
    printf("ipcgetperm: %s\n", r ? "FAIL" : "PASS");
    return r ? 1 : 0;
}
