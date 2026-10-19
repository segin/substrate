/*
 * brkfork — shrinking the heap leaves a page another process still maps
 * alone.
 *
 * brk gave back a shrunk page with pmap_remove (dropping this mapping's
 * hold) and then pmm_free_block (dropping another reference).  For a page
 * still mapped by a process forked from this one that took two references
 * off a page with one left to lose: the frame went back to the allocator
 * while the other process kept a PTE to it, and the next allocation
 * reused it under that process.
 *
 * The parent fills a heap page and forks C, then shrinks its heap past
 * the page.  C forks G, shrinks its own heap past the page, and grows it
 * again with a different pattern, which takes a free frame.  G's page
 * must still hold the parent's pattern.
 *
 * Builds for the host as well, where every line must also read ok:
 *
 *      cc -o brkfork brkfork.c && ./brkfork
 *
 * Native substrate programs have no brk, so on substrate this runs as a
 * static i386 Linux program (make brkfork.linux), through the Linux
 * personality's brk.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define PAGE 4096
#define NPAGES 8

static int page_is(const unsigned char *p, int c) {
    for (int i = 0; i < PAGE; i++)
        if (p[i] != (unsigned char)c) return 0;
    return 1;
}

/* Heap pages [base, base + NPAGES * PAGE), page aligned; NULL if not. */
static unsigned char *grow(void) {
    uintptr_t cur = (uintptr_t)sbrk(0);
    uintptr_t pad = (PAGE - (cur & (PAGE - 1))) & (PAGE - 1);
    if (sbrk((intptr_t)(pad + NPAGES * PAGE)) == (void *)-1) return NULL;
    return (unsigned char *)(cur + pad);
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    unsigned char *heap = grow();
    if (!heap) {
        printf("FAIL  grow the heap\nbrkfork: FAIL\n");
        return 1;
    }
    memset(heap, 'P', NPAGES * PAGE);

    pid_t c = fork();
    if (c == 0) {
        int fds[2];
        if (pipe(fds) != 0) _exit(10);
        pid_t g = fork();
        if (g == 0) {
            char go;
            close(fds[1]);
            if (read(fds[0], &go, 1) != 1) _exit(11);  /* C has shrunk */
            for (int i = 0; i < NPAGES; i++)
                if (!page_is(heap + i * PAGE, 'P')) _exit(1);
            _exit(0);
        }
        close(fds[0]);
        sbrk(-(intptr_t)(NPAGES * PAGE));          /* C gives it back */
        unsigned char *again = grow();             /* and takes frames */
        if (again) memset(again, 'C', NPAGES * PAGE);
        if (write(fds[1], "g", 1) != 1) _exit(12);
        int st = 0;
        waitpid(g, &st, 0);
        _exit(WIFEXITED(st) ? WEXITSTATUS(st) : 13);
    }

    sbrk(-(intptr_t)(NPAGES * PAGE));              /* the parent too */
    int st = 0;
    waitpid(c, &st, 0);
    int ok = WIFEXITED(st) && WEXITSTATUS(st) == 0;
    printf("%s grandchild's heap unchanged after its parent shrank (%d)\n",
           ok ? "ok   " : "FAIL ", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    printf("%s\n", ok ? "brkfork: PASS" : "brkfork: FAIL");
    return !ok;
}
