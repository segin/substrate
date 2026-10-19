/*
 * ksemmap.c — a process-shared sem_init semaphore belongs to the memory
 * it lives in.
 *
 * libc used to back sem_init(pshared) with an anonymous kernel semaphore
 * of mode 0666, so any process that guessed its small id could post,
 * drain or read it.  Now the kernel binds it to the memory holding the
 * sem_t and gives it no permission bits:
 *
 *   A. a uid 1000 process that attached the same SysV segment (at its own
 *      address) posts through its sem_t, and the parent sees the count;
 *   B. the same process, handed only the id, gets EINVAL from post and
 *      getvalue;
 *   C. a child forked after sem_init (in a shared anonymous mapping) uses
 *      it through the reference it inherits;
 *   D. once the segment a semaphore was bound to is removed, a uid 1000
 *      process whose new segment reuses those frames cannot reach the
 *      semaphore through it, while the creator's own reference still works.
 *
 * Substrate-only; must start as root.
 */
#include <errno.h>
#include <semaphore.h>
#include <stdio.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#define USER 1000

static int fails;

static void check(int cond, const char *what)
{
    printf("%s %s\n", cond ? "ok   " : "FAIL ", what);
    if (!cond)
        fails++;
}

/* The stranger: forked before sem_init, uid 1000, told the id by pipe. */
static int stranger(int shmid, int rfd, int wfd)
{
    int id;
    if (setuid(USER) != 0)
        return 0x40;
    if (read(rfd, &id, sizeof(id)) != sizeof(id))
        return 0x41;
    sem_t *mine = shmat(shmid, NULL, 0);
    if (mine == (void *)-1)
        return 0x42;
    int bad = 0;
    /* B: the bare id, without the memory, opens nothing. */
    bad |= (syscall(SYS_KSEM_POST, (long)id, 0L) != -EINVAL) << 0;
    int v = -1;
    bad |= (syscall(SYS_KSEM_GETVALUE, (long)id, (long)&v, 0L) != -EINVAL) << 1;
    /* A: through the shared memory it works. */
    bad |= (sem_post(mine) != 0) << 2;
    shmdt(mine);
    char done = 1;
    write(wfd, &done, 1);
    return bad;
}

/* D: uid 1000, given the id of a semaphore whose segment is gone, makes a
 * segment of its own (whose frames are likely the freed ones) and tries the
 * id through it. */
static int reuser(int rfd)
{
    int id;
    if (setuid(USER) != 0)
        return 0x40;
    if (read(rfd, &id, sizeof(id)) != sizeof(id))
        return 0x41;
    int shmid = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    if (shmid < 0)
        return 0x42;
    sem_t *mine = shmat(shmid, NULL, 0);
    if (mine == (void *)-1)
        return 0x43;
    int bad = syscall(SYS_KSEM_POST, (long)id, (long)mine) != -EINVAL;
    shmdt(mine);
    shmctl(shmid, IPC_RMID, NULL);
    return bad;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (geteuid() != 0) {
        printf("ksemmap: SKIP (needs root)\n");
        return 0;
    }

    /* D's process is forked first, so it inherits no semaphore. */
    int to_reuser[2];
    if (pipe(to_reuser)) {
        printf("ksemmap: FAIL\n");
        return 1;
    }
    pid_t rpid = fork();
    if (rpid == 0) {
        close(to_reuser[1]);
        _exit(reuser(to_reuser[0]));
    }
    close(to_reuser[0]);

    int shmid = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0666);
    check(shmid >= 0, "shmget");
    int to_child[2], from_child[2];
    if (shmid < 0 || pipe(to_child) || pipe(from_child)) {
        printf("ksemmap: FAIL\n");
        return 1;
    }
    pid_t pid = fork();
    if (pid == 0) {
        close(to_child[1]);
        close(from_child[0]);
        _exit(stranger(shmid, to_child[0], from_child[1]));
    }
    close(to_child[0]);
    close(from_child[1]);

    sem_t *s = shmat(shmid, NULL, 0);
    check(s != (void *)-1, "parent attaches the segment");
    check(sem_init(s, 1, 0) == 0, "sem_init(pshared) in the segment");

    int id = s->__sem_id;
    write(to_child[1], &id, sizeof(id));
    char done;
    read(from_child[0], &done, 1);
    int st = 0;
    waitpid(pid, &st, 0);
    int bad = WIFEXITED(st) ? WEXITSTATUS(st) : 0xff;
    printf("      stranger result bits %#x\n", bad);
    check(!(bad & 3), "uid 1000 with only the id: EINVAL for post and getvalue");
    check(!(bad & ~3), "uid 1000 mapping the segment posts through its sem_t");
    int v = -1;
    sem_getvalue(s, &v);
    check(v == 1, "parent sees the stranger's post");
    sem_destroy(s);
    shmdt(s);
    shmctl(shmid, IPC_RMID, NULL);

    /* C: inheritance across fork, in a shared anonymous mapping. */
    sem_t *a = mmap(NULL, sizeof(*a), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(a != MAP_FAILED && sem_init(a, 1, 0) == 0,
          "sem_init(pshared) in a shared anonymous mapping");
    pid = fork();
    if (pid == 0) {
        if (setuid(USER) != 0)
            _exit(1);
        _exit(sem_post(a) == 0 ? 0 : 2);
    }
    st = 0;
    waitpid(pid, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "forked uid 1000 child posts");
    check(sem_trywait(a) == 0, "parent takes the child's post");
    sem_destroy(a);

    /* D: bind a semaphore to a segment, then remove the segment. */
    int seg = shmget(IPC_PRIVATE, 4096, IPC_CREAT | 0600);
    sem_t *gone = seg >= 0 ? shmat(seg, NULL, 0) : (void *)-1;
    int ok = gone != (void *)-1 && sem_init(gone, 1, 0) == 0;
    check(ok, "sem_init(pshared) in a segment that will be removed");
    int gid = ok ? gone->__sem_id : -1;
    if (gone != (void *)-1)
        shmdt(gone);
    shmctl(seg, IPC_RMID, NULL);    /* no attaches left: freed now */
    write(to_reuser[1], &gid, sizeof(gid));
    st = 0;
    waitpid(rpid, &st, 0);
    check(WIFEXITED(st) && WEXITSTATUS(st) == 0,
          "uid 1000 in a fresh segment cannot reach the removed segment's semaphore");
    check(syscall(SYS_KSEM_POST, (long)gid, 0L) == 0,
          "the creator's own reference still works");
    syscall(SYS_KSEM_CLOSE, (long)gid);

    printf("ksemmap: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
