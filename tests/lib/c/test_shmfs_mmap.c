/*
 * test_shmfs_mmap.c — POSIX shared-memory (/dev/shm) mmap correctness.
 *
 * Verifies the shmfs_node_mmap fix: a mmap of a /dev/shm object must
 *
 *   1. return a USER virtual address (NOT a kernel direct-map pointer
 *      at >= 0xC0000000),
 *   2. be writable + readable through that pointer,
 *   3. be SHARED — a write through one mapping is visible through a
 *      second, independent mapping of the same object (in this process
 *      and across a fork),
 *   4. not leak physical memory across many
 *      create/ftruncate/mmap/munmap/unlink cycles.
 *
 * Built for the substrate target.  Output is UNBUFFERED so a headless
 * serial capture sees every line even if the kernel wedges.
 */

#include <sys/mman.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>

#define SHM_NAME "/shmtest"
#define SZ      (64 * 1024)         /* 16 pages */
#define KVA_FLOOR 0xC0000000u       /* kernel direct-map base */

static int g_pass = 0, g_fail = 0;

static void ok(const char *what, int cond) {
    if (cond) { g_pass++; printf("  PASS  %s\n", what); }
    else      { g_fail++; printf("  FAIL  %s\n", what); }
}

/* MemFree in kB from /proc/meminfo, or -1 if unavailable. */
static long memfree_kb(void) {
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[160];
    long v = -1;
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "MemFree: %ld", &v) == 1) break;
    }
    fclose(f);
    return v;
}

/* Create + size + map a fresh object.  Returns the mapping (or NULL),
 * stores the fd through *fd_out so the caller can close/unlink. */
static volatile unsigned char *make_mapping(const char *name, int *fd_out) {
    int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
    if (fd < 0) { printf("    shm_open(%s): errno=%d\n", name, errno); return NULL; }
    if (ftruncate(fd, SZ) != 0) {
        printf("    ftruncate: errno=%d\n", errno);
        close(fd);
        return NULL;
    }
    void *p = mmap(NULL, SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (p == MAP_FAILED) {
        printf("    mmap: errno=%d\n", errno);
        close(fd);
        return NULL;
    }
    *fd_out = fd;
    return (volatile unsigned char *)p;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("== shmfs /dev/shm mmap test ==\n");

    /* Clean any stale object from a previous run. */
    shm_unlink(SHM_NAME);

    /* ---- 1. user VA + read/write through the mapping ---- */
    int fd1 = -1;
    volatile unsigned char *p1 = make_mapping(SHM_NAME, &fd1);
    ok("mmap succeeded", p1 != NULL);
    if (!p1) { printf("RESULT: FAIL (%d/%d)\n", g_pass, g_pass + g_fail); return 1; }

    uintptr_t a1 = (uintptr_t)p1;
    printf("  mapping address = %p\n", (void *)p1);
    ok("address is a USER VA (< 0xC0000000)", a1 < KVA_FLOOR && a1 != 0);

    /* Write a recognizable pattern through the mapping, read it back. */
    for (int i = 0; i < SZ; i++) p1[i] = (unsigned char)((i * 7 + 3) & 0xFF);
    int rb_ok = 1;
    for (int i = 0; i < SZ; i++)
        if (p1[i] != (unsigned char)((i * 7 + 3) & 0xFF)) { rb_ok = 0; break; }
    ok("write-through pattern reads back", rb_ok);

    /* ---- 2. second independent mapping of the SAME object sees the
     *         same data (intra-process sharing, same physical pages) ---- */
    int fd2 = -1;
    volatile unsigned char *p2 = make_mapping(SHM_NAME, &fd2);
    ok("second mmap succeeded", p2 != NULL);
    if (p2) {
        ok("second mapping is distinct VA", (void *)p2 != (void *)p1);
        int share_ok = 1;
        for (int i = 0; i < SZ; i++)
            if (p2[i] != (unsigned char)((i * 7 + 3) & 0xFF)) { share_ok = 0; break; }
        ok("second mapping sees first mapping's writes", share_ok);

        /* Write through p2, observe through p1. */
        p2[0]      = 0xAA;
        p2[SZ - 1] = 0x55;
        ok("write via mapping #2 visible via mapping #1",
           p1[0] == 0xAA && p1[SZ - 1] == 0x55);
    }

    /* ---- 3. cross-process sharing via fork ---- */
    /* Reset a sentinel the child will overwrite. */
    p1[16] = 0x00;
    p1[32] = 0x00;
    pid_t pid = fork();
    if (pid == 0) {
        /* Child: open the SAME name fresh, map, write, exit. */
        int cfd = -1;
        volatile unsigned char *cp = make_mapping(SHM_NAME, &cfd);
        if (!cp) _exit(2);
        cp[16] = 0xC1;
        cp[32] = 0xD2;
        munmap((void *)cp, SZ);
        close(cfd);
        _exit(0);
    } else if (pid > 0) {
        int st = 0;
        waitpid(pid, &st, 0);
        ok("child exited cleanly", WIFEXITED(st) && WEXITSTATUS(st) == 0);
        /* Parent observes the child's writes through its own mapping. */
        ok("parent sees child's cross-process write",
           p1[16] == 0xC1 && p1[32] == 0xD2);
    } else {
        ok("fork", 0);
    }

    /* Done with the live mappings. */
    if (p2) { munmap((void *)p2, SZ); close(fd2); }
    munmap((void *)p1, SZ);
    close(fd1);
    shm_unlink(SHM_NAME);

    /* ---- 4. leak check: many create/ftruncate/mmap/munmap/unlink ---- */
    const int N = 200;
    long before = memfree_kb();
    for (int i = 0; i < N; i++) {
        char nm[32];
        snprintf(nm, sizeof(nm), "/leak%d", i & 7);  /* reuse 8 names */
        shm_unlink(nm);
        int fd = shm_open(nm, O_CREAT | O_RDWR, 0600);
        if (fd < 0) continue;
        if (ftruncate(fd, SZ) == 0) {
            void *p = mmap(NULL, SZ, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (p != MAP_FAILED) {
                ((volatile unsigned char *)p)[0] = 1;
                ((volatile unsigned char *)p)[SZ - 1] = 1;
                munmap(p, SZ);
            }
        }
        close(fd);
        shm_unlink(nm);
    }
    long after = memfree_kb();
    if (before >= 0 && after >= 0) {
        long lost = before - after;
        printf("  leak: MemFree %ld -> %ld kB over %d cycles (lost=%ld kB)\n",
               before, after, N, lost);
        /* Each cycle touches 16 pages (64 KiB); a per-cycle leak would be
         * 200*64 = 12.8 MiB.  Allow a small slack for cache/log churn. */
        ok("no significant leak over churn (< 256 kB)", lost < 256);
    } else {
        printf("  leak: /proc/meminfo unavailable, skipping quantified check\n");
    }

    printf("RESULT: %s (%d/%d passed)\n",
           g_fail == 0 ? "PASS" : "FAIL", g_pass, g_pass + g_fail);
    return g_fail == 0 ? 0 : 1;
}
