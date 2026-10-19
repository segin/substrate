/*
 * shmrange — shmat at a caller address must fit the user address space.
 *
 * kern_shmat checked only that an explicit shmaddr was page-aligned, then
 * tore down [shmaddr, shmaddr + size) and mapped the segment there.  A
 * segment placed across the top of the user space (0xC0000000) reached
 * into kernel addresses, and one near 4 GiB wrapped.  Both must now fail
 * with EINVAL, and an in-range explicit address must still attach.
 *
 * Runs on the host too, where the addresses mean something else: there
 * it only checks that the in-range attach works.
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define TOP   0xC0000000UL
#define PAGES 2
#define SIZE  (PAGES * 4096UL)

static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

#ifdef __substrate__
static int attach_einval(int id, uintptr_t addr)
{
    errno = 0;
    void *p = shmat(id, (void *)addr, 0);
    if (p != (void *)-1) {
        printf("      attached at %p\n", p);
        shmdt(p);
        return 0;
    }
    return errno == EINVAL;
}
#endif

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    int id = shmget(IPC_PRIVATE, SIZE, IPC_CREAT | 0600);
    check(id >= 0, "shmget two pages");
    if (id < 0) {
        printf("shmrange: FAIL\n");
        return 1;
    }

#ifdef __substrate__
    check(attach_einval(id, TOP - 4096),
          "shmat across the top of user space -> EINVAL");
    check(attach_einval(id, 0xFFFFF000UL),
          "shmat that wraps past 4 GiB -> EINVAL");
#endif

    /* The top of user space holds the stack, which an explicit shmaddr
     * would replace, so the in-range attach goes somewhere free. */
    void *want = (void *)0x70000000UL;
    void *p = shmat(id, want, 0);
    int ok = p == want;
    check(ok, "shmat at a free in-range address");
    if (ok) {
        memset(p, 0x5a, SIZE);
        check(((volatile unsigned char *)p)[SIZE - 1] == 0x5a,
              "segment is usable");
        shmdt(p);
    }

    shmctl(id, IPC_RMID, NULL);
    printf("shmrange: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
