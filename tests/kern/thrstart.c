/*
 * thrstart — thr_new with a stack the caller cannot write fails with
 * EFAULT; it does not take the kernel down.
 *
 * The trampoline that takes a new thread to user mode used to store its
 * first call frame (the argument, a return address of 0) on the user
 * stack itself, in ring 0 and with no fault recovery, so an unmapped or
 * read-only stack panicked the kernel when the thread first ran.  The
 * frame is now written by thr_new with copyout.
 *
 * A thread created with no TLS block must also start: its %gs selects
 * the TLS descriptor, which has to be valid even though no base was set.
 *
 * Substrate only (thr_new is the native thread call).
 */
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/thr.h>

#define STACK 16384

static int failed;
static volatile int ran;
static volatile int seen_arg;

static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok) failed = 1;
}

/* thr_new and thr_exit have no libc wrappers; libpthread calls them
 * through syscall(), which returns the kernel's 0 or -errno. */
static void start(void *arg) {
    seen_arg = (int)(long)arg;
    ran = 1;
    syscall(SYS_THR_EXIT, 0);
}

/* 0, or the -errno thr_new failed with. */
static int new_thread(void *stack_base, size_t size, void *arg) {
    struct thr_param p;
    long tid = 0;
    memset(&p, 0, sizeof(p));
    p.start_func = start;
    p.arg = arg;
    p.stack_base = stack_base;
    p.stack_size = size;
    p.child_tid = &tid;
    return (int)syscall(SYS_THR_NEW, (intptr_t)&p, sizeof(p));
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);

    /* No memory at all behind the stack. */
    int rc = new_thread((void *)0x1000, 0x1000, NULL);
    check(rc == -EFAULT, "unmapped stack: EFAULT");

    /* A stack the caller may only read. */
    void *ro = mmap(NULL, STACK, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    check(ro != MAP_FAILED, "map a read-only stack");
    if (ro != MAP_FAILED) {
        rc = new_thread(ro, STACK, NULL);
        check(rc == -EFAULT, "read-only stack: EFAULT");
    }

    /* A good stack and no TLS block: the thread runs and sees its arg. */
    void *stack = malloc(STACK);
    check(stack != NULL, "allocate a stack");
    if (stack) {
        rc = new_thread(stack, STACK, (void *)(long)42);
        check(rc == 0, "good stack: thread created");
        for (int i = 0; i < 100000 && !ran; i++)
            sched_yield();
        check(ran && seen_arg == 42, "thread with no TLS ran, saw its arg");
    }

    printf("%s\n", failed ? "thrstart: FAIL" : "thrstart: PASS");
    return failed;
}
