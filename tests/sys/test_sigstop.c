#include <sys/types.h>
#include <sys/signal.h>
#include <sys/proc.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <string.h>

/*
 * SIGSTOP Verification Tests
 *
 * Checks:
 * 1. Immutability: sigaction(SIGSTOP) should fail.
 * 2. Immutability: sigprocmask(SIGSTOP) should not block.
 * 3. Delivery: psignal(SIGSTOP) sets pending on threads.
 * 4. Cleanup: SIGCONT clears pending SIGSTOP.
 */

// Externs for syscalls/functions we test
extern int sys_sigaction(int sig, const void *act, void *oact);
extern int sys_sigprocmask(int how, const void *set, void *oset);
extern void psignal(process_t *p, int sig);
extern process_t *proc_create(struct personality *pers);
extern thread_t *sched_alloc_thread(process_t *proc);
extern struct personality personality_native;

static int test_sigstop_immutability(void) {
    kprint("Test: SIGSTOP immutability... ");

    // 1. Try to catch SIGSTOP
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = (sig_t)0xDEADBEEF; // Some handler
    
    // sys_sigaction returns -1 on failure (or generic error)
    if (sys_sigaction(SIGSTOP, &sa, NULL) == 0) {
        kprint("FAIL: sys_sigaction allowed catching SIGSTOP\n");
        return -1;
    }

    // 2. Try to block SIGSTOP
    // We need a dummy thread as sys_sigprocmask operates on current_thread
    // But we are in kernel test runner, current_thread might be valid or NULL?
    // Test runner runs in a thread probably?
    extern thread_t *current_thread;
    if (!current_thread) {
        kprint("SKIP (No current_thread)\n");
        return 0; 
    }
    
    uint32_t set = sigmask(SIGSTOP);
    if (sys_sigprocmask(1 /* SIG_BLOCK */, &set, NULL) != 0) {
        kprint("FAIL: sys_sigprocmask call failed\n");
        return -1;
    }
    
    // Check if it was actually blocked
    if (current_thread->sig_mask & sigmask(SIGSTOP)) {
        kprint("FAIL: SIGSTOP found in sig_mask after blocking\n");
        return -1;
    }

    kprint("OK\n");
    return 0;
}

static int test_sigstop_delivery(void) {
    kprint("Test: SIGSTOP pending delivery... ");

    // Create dummy process/thread
    process_t *p = proc_create(&personality_native);
    if (!p) return -1;
    
    thread_t *t1 = sched_alloc_thread(p);
    thread_t *t2 = sched_alloc_thread(p); // Test multi-thread
    if (!t1 || !t2) return -1;

    t1->tid = 1001; t1->state = THREAD_READY; t1->sig_pending = 0;
    t2->tid = 1002; t2->state = THREAD_READY; t2->sig_pending = 0;

    // Send SIGSTOP
    psignal(p, SIGSTOP);

    // Verify pending on BOTH threads
    if (!(t1->sig_pending & sigmask(SIGSTOP))) {
        kprint("FAIL: SIGSTOP not pending on T1\n");
        // Clean up?
        return -1;
    }
    if (!(t2->sig_pending & sigmask(SIGSTOP))) {
        kprint("FAIL: SIGSTOP not pending on T2\n");
        return -1;
    }

    // Verify SIGCONT clears it
    psignal(p, SIGCONT);
    
    if (t1->sig_pending & sigmask(SIGSTOP)) {
        kprint("FAIL: SIGSTOP still pending on T1 after SIGCONT\n");
        return -1;
    }
    if (t2->sig_pending & sigmask(SIGSTOP)) {
        kprint("FAIL: SIGSTOP still pending on T2 after SIGCONT\n");
        return -1;
    }

    kprint("OK\n");
    return 0;
}

void run_sigstop_tests(void) {
    kprint("--- SIGSTOP Verification Tests ---\n");
    test_sigstop_immutability();
    test_sigstop_delivery();
    kprint("--- SIGSTOP Tests Complete ---\n");
}
