#include <sys/types.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <kern/sched.h>
#include <kern/console.h>
#include <pm/pm.h>
#include <exec/perso/personality.h>
#include <string.h>
#include <sys/errno.h>
#include <exec/perso/personality.h>
#include <stdio.h>

/*
 * Kernel-side signal property tests
 *
 * Verifies:
 * 1. Signal mask management (sigmask, sigaddset, etc.)
 * 2. psignal thread selection logic
 * 3. Pending signal preservation
 */

static int test_sigmask(void) {
    kprint("Test: sigmask macros... ");
    if (sigmask(1) != 0x1) return -1;
    if (sigmask(32) != 0x80000000U) return -1;
    kprint("OK\n");
    return 0;
}

static int test_sys_kill_errors(void) {
    kprint("Test: sys_kill error handling... ");
    char buf[128];

    // 1. Invalid signal number
    int ret = sys_kill(1, -1);
    if (ret != -EINVAL) {
        snprintf(buf, sizeof(buf), "FAIL: sys_kill(-1) returned %d, expected %d (-EINVAL)\n", ret, -EINVAL);
        kprint(buf);
        return -1;
    }

    ret = sys_kill(1, NSIG + 1);
    if (ret != -EINVAL) {
        snprintf(buf, sizeof(buf), "FAIL: sys_kill(NSIG+1) returned %d, expected %d (-EINVAL)\n", ret, -EINVAL);
        kprint(buf);
        return -1;
    }

    // 2. Non-existent process (ESRCH)
    // Use a large PID unlikely to exist
    ret = sys_kill(9999, 0);
    if (ret != -ESRCH) {
        snprintf(buf, sizeof(buf), "FAIL: sys_kill(9999) returned %d, expected %d (-ESRCH)\n", ret, -ESRCH);
        kprint(buf);
        return -1;
    }

    kprint("OK\n");
    return 0;
}

static int test_psignal_delivery(void) {
    kprint("Test: psignal delivery logic... ");
    
    // Create a dummy process and thread
    extern process_t *proc_create(int perso_id);
    process_t *p = proc_create(PERS_NATIVE);
    if (!p) return -1;
    
    extern thread_t *sched_alloc_thread(process_t *proc);
    thread_t *t = sched_alloc_thread(p);
    if (!t) return -1;
    
    t->state = THREAD_READY;
    t->sig_mask = 0;
    t->sig_pending = 0;
    
    extern void psignal(process_t *p, int sig);
    psignal(p, SIGUSR1);
    
    if (!(t->sig_pending & sigmask(SIGUSR1))) {
        kprint("FAIL: Signal not pending on thread\n");
        return -1;
    }
    
    // Test masking
    t->sig_pending = 0;
    t->sig_mask = sigmask(SIGUSR1);
    psignal(p, SIGUSR1);
    
    if (!(t->sig_pending & sigmask(SIGUSR1))) {
        kprint("FAIL: Masked signal not pending (should still be pending, just not delivered)\n");
        return -1;
    }
    
    kprint("OK\n");
    return 0;
}

static int test_init_protection(void) {
    kprint("Test: Init (PID 1) protection... ");
    
    process_t *init = proc_find(1);
    if (!init) {
        kprint("SKIP (Init not found)\n");
        return 0;
    }
    
    // Block the test if we are actually init (shouldn't happen in test runner)
    if (current_process->pid == 1) return 0;
    
    // Find init's thread
    thread_t *it = NULL;
    /*
     * #425: the static threads[MAX_THREADS] array no longer exists -- threads
     * live on the t_allthread_next registry, walked with thread_first() /
     * thread_next().  This `extern thread_t threads[]` also declared a symbol
     * the kernel does not define, which is exactly the kind of stale
     * declaration that only surfaces when the tests are actually built.
     */
    for (thread_t *t = thread_first(); t != NULL; t = thread_next(t)) {
        if (t->proc == init) {
            it = t;
            break;
        }
    }
    
    if (!it) return 0;
    
    extern void psignal(process_t *p, int sig);
    psignal(init, SIGKILL);
    
    if (it->sig_pending & sigmask(SIGKILL)) {
        kprint("FAIL: SIGKILL pending on Init\n");
        return -1;
    }
    
    psignal(init, SIGSTOP);
    if (it->sig_pending & sigmask(SIGSTOP)) {
        kprint("FAIL: SIGSTOP pending on Init\n");
        return -1;
    }
    
    kprint("OK\n");
    return 0;
}

void run_signal_tests(void) {
    kprint("--- Kernel Signal Property Tests ---\n");
    test_sigmask();
    test_psignal_delivery();
    test_init_protection();
    test_sys_kill_errors();
    kprint("--- Signal Tests Complete ---\n");
}
