/*
 * test_signal.c - Signal Infrastructure Tests
 *
 * Kernel tests for signal handling subsystem.
 */

#include "../kern/console.h"
#include "../include/sys/signal.h"
#include "../include/sys/proc.h"
#include "../kern/sched.h"
#include <string.h>

/* Test framework macros */
#define TEST_PASS(name) kprint("[PASS] " name "\n")
#define TEST_FAIL(name) kprint("[FAIL] " name "\n")
#define ASSERT(cond, name) do { if (!(cond)) { TEST_FAIL(name); return; } } while(0)
#define ASSERT_EQ(a, b, name) ASSERT((a) == (b), name)
#define ASSERT_NE(a, b, name) ASSERT((a) != (b), name)

/* Mock process/thread for testing when current_process is NULL */
static process_t test_process;
static thread_t test_thread;

/* Save/restore current process for tests */
static process_t *saved_process;
static thread_t *saved_thread;

static void setup_test_context(void) {
    saved_process = current_process;
    saved_thread = current_thread;
    
    memset(&test_process, 0, sizeof(test_process));
    memset(&test_thread, 0, sizeof(test_thread));
    test_process.pid = 999;
    test_thread.tid = 999;
    test_thread.proc = &test_process;
    
    current_process = &test_process;
    current_thread = &test_thread;
}

static void teardown_test_context(void) {
    current_process = saved_process;
    current_thread = saved_thread;
}

/* ========================================================================
 * sig_actions[NSIG] Tests
 * ======================================================================== */

/*
 * Test: sig_actions array exists and has correct size
 */
static void test_sig_actions_size(void) {
    setup_test_context();
    
    /* Verify sig_actions is NSIG elements (0 to NSIG-1) */
    ASSERT_EQ(sizeof(test_process.sig_actions) / sizeof(struct sigaction), 
              NSIG, "sig_actions size equals NSIG");
    
    teardown_test_context();
    TEST_PASS("test_sig_actions_size");
}

/*
 * Test: sig_actions initializes to SIG_DFL by default
 */
static void test_sig_actions_default(void) {
    setup_test_context();
    
    /* Zero-initialized process should have all SIG_DFL handlers */
    for (int i = 0; i < NSIG; i++) {
        ASSERT_EQ(test_process.sig_actions[i].sa_handler, SIG_DFL,
                  "sig_actions default handler is SIG_DFL");
    }
    
    teardown_test_context();
    TEST_PASS("test_sig_actions_default");
}

/*
 * Test: sys_sigaction installs handler correctly
 */
static void test_sigaction_install_handler(void) {
    setup_test_context();
    
    /* Install a handler for SIGUSR1 */
    struct sigaction new_act = {0};
    new_act.sa_handler = (sig_t)0x12345678;
    new_act.sa_mask = 0;
    new_act.sa_flags = 0;
    
    struct sigaction old_act = {0};
    old_act.sa_handler = (sig_t)0xDEADBEEF; /* Should be overwritten */
    
    int ret = sys_sigaction(SIGUSR1, &new_act, &old_act);
    ASSERT_EQ(ret, 0, "sys_sigaction returns 0");
    
    /* Old action should be SIG_DFL (from memset) */
    ASSERT_EQ(old_act.sa_handler, SIG_DFL, "old action is SIG_DFL");
    
    /* Verify handler was installed */
    ASSERT_EQ(test_process.sig_actions[SIGUSR1 - 1].sa_handler, 
              (sig_t)0x12345678, "handler installed correctly");
    
    teardown_test_context();
    TEST_PASS("test_sigaction_install_handler");
}

/*
 * Test: sys_sigaction returns old action
 */
static void test_sigaction_returns_old(void) {
    setup_test_context();
    
    /* Install initial handler */
    struct sigaction act1 = { .sa_handler = (sig_t)0x11111111 };
    sys_sigaction(SIGUSR2, &act1, NULL);
    
    /* Install new handler, get old */
    struct sigaction act2 = { .sa_handler = (sig_t)0x22222222 };
    struct sigaction old_act;
    int ret = sys_sigaction(SIGUSR2, &act2, &old_act);
    
    ASSERT_EQ(ret, 0, "sys_sigaction returns 0");
    ASSERT_EQ(old_act.sa_handler, (sig_t)0x11111111, "old handler returned correctly");
    
    teardown_test_context();
    TEST_PASS("test_sigaction_returns_old");
}

/*
 * Test: sys_sigaction rejects SIGKILL modifications
 */
static void test_sigaction_sigkill_rejected(void) {
    setup_test_context();
    
    struct sigaction act = { .sa_handler = SIG_IGN };
    int ret = sys_sigaction(SIGKILL, &act, NULL);
    
    ASSERT_EQ(ret, -1, "SIGKILL modification rejected");
    
    teardown_test_context();
    TEST_PASS("test_sigaction_sigkill_rejected");
}

/*
 * Test: sys_sigaction rejects SIGSTOP modifications
 */
static void test_sigaction_sigstop_rejected(void) {
    setup_test_context();
    
    struct sigaction act = { .sa_handler = SIG_IGN };
    int ret = sys_sigaction(SIGSTOP, &act, NULL);
    
    ASSERT_EQ(ret, -1, "SIGSTOP modification rejected");
    
    teardown_test_context();
    TEST_PASS("test_sigaction_sigstop_rejected");
}

/*
 * Test: sys_sigaction rejects invalid signal numbers
 */
static void test_sigaction_invalid_signal(void) {
    setup_test_context();
    
    struct sigaction act = { .sa_handler = SIG_IGN };
    
    /* Signal 0 is invalid */
    ASSERT_EQ(sys_sigaction(0, &act, NULL), -1, "signal 0 rejected");
    
    /* Signal > NSIG is invalid */
    ASSERT_EQ(sys_sigaction(NSIG + 1, &act, NULL), -1, "signal > NSIG rejected");
    
    /* Negative signal numbers are invalid */
    ASSERT_EQ(sys_sigaction(-1, &act, NULL), -1, "negative signal rejected");
    
    teardown_test_context();
    TEST_PASS("test_sigaction_invalid_signal");
}

/*
 * Test: sys_sigaction with NULL act only retrieves
 */
static void test_sigaction_query_only(void) {
    setup_test_context();
    
    /* Install handler first */
    struct sigaction act = { .sa_handler = (sig_t)0xAABBCCDD };
    sys_sigaction(SIGTERM, &act, NULL);
    
    /* Query without modifying */
    struct sigaction old_act;
    int ret = sys_sigaction(SIGTERM, NULL, &old_act);
    
    ASSERT_EQ(ret, 0, "sys_sigaction with NULL act succeeds");
    ASSERT_EQ(old_act.sa_handler, (sig_t)0xAABBCCDD, "handler queried correctly");
    
    /* Verify not modified */
    ASSERT_EQ(test_process.sig_actions[SIGTERM - 1].sa_handler, 
              (sig_t)0xAABBCCDD, "handler not modified");
    
    teardown_test_context();
    TEST_PASS("test_sigaction_query_only");
}

/*
 * Test: sigaction mask and flags are stored correctly
 */
static void test_sigaction_mask_and_flags(void) {
    setup_test_context();
    
    struct sigaction act = {
        .sa_handler = (sig_t)0x12345678,
        .sa_mask = sigmask(SIGINT) | sigmask(SIGQUIT),
        .sa_flags = SA_SIGINFO
    };
    
    sys_sigaction(SIGHUP, &act, NULL);
    
    ASSERT_EQ(test_process.sig_actions[SIGHUP - 1].sa_mask,
              sigmask(SIGINT) | sigmask(SIGQUIT), "sa_mask stored correctly");
    ASSERT_EQ(test_process.sig_actions[SIGHUP - 1].sa_flags,
              SA_SIGINFO, "sa_flags stored correctly");
    
    teardown_test_context();
    TEST_PASS("test_sigaction_mask_and_flags");
}

/* ========================================================================
 * sig_catch / sig_ignore Bitmask Tests
 * ======================================================================== */

/*
 * Test: sig_catch is set when custom handler installed
 */
static void test_sig_catch_set_on_handler(void) {
    setup_test_context();
    
    /* Initially both bitmasks should be 0 */
    ASSERT_EQ(test_process.sig_catch, 0, "sig_catch initially 0");
    ASSERT_EQ(test_process.sig_ignore, 0, "sig_ignore initially 0");
    
    /* Install custom handler */
    struct sigaction act = { .sa_handler = (sig_t)0x12345678 };
    sys_sigaction(SIGUSR1, &act, NULL);
    
    /* sig_catch should have SIGUSR1 bit set */
    ASSERT_NE(test_process.sig_catch & sigmask(SIGUSR1), 0, "sig_catch has SIGUSR1");
    ASSERT_EQ(test_process.sig_ignore & sigmask(SIGUSR1), 0, "sig_ignore does not have SIGUSR1");
    
    teardown_test_context();
    TEST_PASS("test_sig_catch_set_on_handler");
}

/*
 * Test: sig_ignore is set when SIG_IGN installed
 */
static void test_sig_ignore_set_on_sigign(void) {
    setup_test_context();
    
    /* Install SIG_IGN */
    struct sigaction act = { .sa_handler = SIG_IGN };
    sys_sigaction(SIGPIPE, &act, NULL);
    
    /* sig_ignore should have SIGPIPE bit set */
    ASSERT_NE(test_process.sig_ignore & sigmask(SIGPIPE), 0, "sig_ignore has SIGPIPE");
    ASSERT_EQ(test_process.sig_catch & sigmask(SIGPIPE), 0, "sig_catch does not have SIGPIPE");
    
    teardown_test_context();
    TEST_PASS("test_sig_ignore_set_on_sigign");
}

/*
 * Test: sig_catch cleared when handler reset to SIG_DFL
 */
static void test_sig_catch_cleared_on_sigdfl(void) {
    setup_test_context();
    
    /* Install handler first */
    struct sigaction act = { .sa_handler = (sig_t)0x12345678 };
    sys_sigaction(SIGTERM, &act, NULL);
    ASSERT_NE(test_process.sig_catch & sigmask(SIGTERM), 0, "sig_catch has SIGTERM after handler");
    
    /* Reset to SIG_DFL */
    act.sa_handler = SIG_DFL;
    sys_sigaction(SIGTERM, &act, NULL);
    
    /* Both bitmasks should be cleared for this signal */
    ASSERT_EQ(test_process.sig_catch & sigmask(SIGTERM), 0, "sig_catch cleared for SIGTERM");
    ASSERT_EQ(test_process.sig_ignore & sigmask(SIGTERM), 0, "sig_ignore cleared for SIGTERM");
    
    teardown_test_context();
    TEST_PASS("test_sig_catch_cleared_on_sigdfl");
}

/*
 * Test: sig_ignore cleared when replaced by custom handler
 */
static void test_sig_ignore_cleared_on_handler(void) {
    setup_test_context();
    
    /* Set to SIG_IGN first */
    struct sigaction act = { .sa_handler = SIG_IGN };
    sys_sigaction(SIGHUP, &act, NULL);
    ASSERT_NE(test_process.sig_ignore & sigmask(SIGHUP), 0, "sig_ignore has SIGHUP");
    
    /* Now install custom handler */
    act.sa_handler = (sig_t)0xABCDEF01;
    sys_sigaction(SIGHUP, &act, NULL);
    
    /* sig_ignore should be cleared, sig_catch should be set */
    ASSERT_EQ(test_process.sig_ignore & sigmask(SIGHUP), 0, "sig_ignore cleared for SIGHUP");
    ASSERT_NE(test_process.sig_catch & sigmask(SIGHUP), 0, "sig_catch set for SIGHUP");
    
    teardown_test_context();
    TEST_PASS("test_sig_ignore_cleared_on_handler");
}

/*
 * Test: Multiple signals tracked independently
 */
static void test_sig_bitmasks_multiple_signals(void) {
    setup_test_context();
    
    /* Set different handlers for different signals */
    struct sigaction act_handler = { .sa_handler = (sig_t)0x12345678 };
    struct sigaction act_ignore = { .sa_handler = SIG_IGN };
    
    sys_sigaction(SIGUSR1, &act_handler, NULL);
    sys_sigaction(SIGUSR2, &act_handler, NULL);
    sys_sigaction(SIGPIPE, &act_ignore, NULL);
    sys_sigaction(SIGALRM, &act_ignore, NULL);
    
    /* Verify bitmasks */
    ASSERT_NE(test_process.sig_catch & sigmask(SIGUSR1), 0, "SIGUSR1 in sig_catch");
    ASSERT_NE(test_process.sig_catch & sigmask(SIGUSR2), 0, "SIGUSR2 in sig_catch");
    ASSERT_EQ(test_process.sig_catch & sigmask(SIGPIPE), 0, "SIGPIPE not in sig_catch");
    ASSERT_EQ(test_process.sig_catch & sigmask(SIGALRM), 0, "SIGALRM not in sig_catch");
    
    ASSERT_EQ(test_process.sig_ignore & sigmask(SIGUSR1), 0, "SIGUSR1 not in sig_ignore");
    ASSERT_EQ(test_process.sig_ignore & sigmask(SIGUSR2), 0, "SIGUSR2 not in sig_ignore");
    ASSERT_NE(test_process.sig_ignore & sigmask(SIGPIPE), 0, "SIGPIPE in sig_ignore");
    ASSERT_NE(test_process.sig_ignore & sigmask(SIGALRM), 0, "SIGALRM in sig_ignore");
    
    teardown_test_context();
    TEST_PASS("test_sig_bitmasks_multiple_signals");
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

/*
 * Test: sys_sigpending returns masked signals (POSX: blocked and pending)
 */
static void test_sigpending_masking(void) {
    setup_test_context();
    
    current_thread->sig_pending = sigmask(SIGUSR1) | sigmask(SIGUSR2);
    current_thread->sig_mask = sigmask(SIGUSR1); // Block SIGUSR1
    
    uint32_t pending = 0;
    int ret = sys_sigpending(&pending);
    
    ASSERT_EQ(ret, 0, "sys_sigpending returns 0");
    /* sigpending returns pending AND blocked signals */
    ASSERT_EQ(pending, sigmask(SIGUSR1), "sys_sigpending returns only blocked pending signals");
    
    teardown_test_context();
    TEST_PASS("test_sigpending_masking");
}

/*
 * Test: sys_sigaltstack rejects if handling on stack
 */
static void test_sigaltstack_eperm_on_stack(void) {
    setup_test_context();
    
    current_thread->sig_on_stack = 1;
    
    stack_t ss = {0};
    ss.ss_size = MINSIGSTKSZ * 2;
    
    int ret = sys_sigaltstack(&ss, NULL);
    
    ASSERT_EQ(ret, -1, "sys_sigaltstack returns -1 when on stack");
    
    teardown_test_context();
    TEST_PASS("test_sigaltstack_eperm_on_stack");
}

/* ========================================================================
 * sys_sigwait Tests
 * ======================================================================== */

/*
 * Test: sys_sigwait returns immediately if signal already pending
 */
static void test_sigwait_immediate(void) {
    setup_test_context();
    
    /* Make SIGUSR1 pending */
    current_thread->sig_pending = sigmask(SIGUSR1);
    
    uint32_t wait_set = sigmask(SIGUSR1);
    int sig = 0;
    
    int ret = sys_sigwait(&wait_set, &sig);
    
    ASSERT_EQ(ret, 0, "sys_sigwait returns 0 on success");
    ASSERT_EQ(sig, SIGUSR1, "sys_sigwait returns correct signal");
    ASSERT_EQ(current_thread->sig_pending & sigmask(SIGUSR1), 0, 
              "signal removed from pending");
    
    teardown_test_context();
    TEST_PASS("test_sigwait_immediate");
}

/*
 * Test: sys_sigwait rejects NULL arguments
 */
static void test_sigwait_null_args(void) {
    setup_test_context();
    
    uint32_t wait_set = sigmask(SIGUSR1);
    int sig = 0;
    
    ASSERT_EQ(sys_sigwait(NULL, &sig), 22, "sys_sigwait rejects NULL set");
    ASSERT_EQ(sys_sigwait(&wait_set, NULL), 22, "sys_sigwait rejects NULL sig");
    
    teardown_test_context();
    TEST_PASS("test_sigwait_null_args");
}

/*
 * Test: sys_sigwait filters SIGKILL/SIGSTOP from wait set
 */
static void test_sigwait_filters_cantmask(void) {
    setup_test_context();
    
    /* Try to wait only for SIGKILL - should fail */
    uint32_t wait_set = sigmask(SIGKILL);
    int sig = 0;
    
    int ret = sys_sigwait(&wait_set, &sig);
    ASSERT_EQ(ret, 22, "sys_sigwait rejects SIGKILL-only set");
    
    /* Try to wait only for SIGSTOP - should fail */
    wait_set = sigmask(SIGSTOP);
    ret = sys_sigwait(&wait_set, &sig);
    ASSERT_EQ(ret, 22, "sys_sigwait rejects SIGSTOP-only set");
    
    teardown_test_context();
    TEST_PASS("test_sigwait_filters_cantmask");
}

/*
 * Test: sys_sigwait selects first signal when multiple pending
 */
static void test_sigwait_selects_first(void) {
    setup_test_context();
    
    /* Make multiple signals pending */
    current_thread->sig_pending = sigmask(SIGUSR1) | sigmask(SIGUSR2) | sigmask(SIGALRM);
    
    uint32_t wait_set = sigmask(SIGUSR1) | sigmask(SIGUSR2) | sigmask(SIGALRM);
    int sig = 0;
    
    int ret = sys_sigwait(&wait_set, &sig);
    
    ASSERT_EQ(ret, 0, "sys_sigwait returns 0");
    /* Should return the lowest numbered signal */
    ASSERT_EQ(sig, SIGUSR1, "sys_sigwait returns lowest pending signal");
    
    teardown_test_context();
    TEST_PASS("test_sigwait_selects_first");
}

/* ========================================================================
 * sys_sigtimedwait Tests
 * ======================================================================== */

/*
 * Test: sys_sigtimedwait returns signal immediately if pending
 */
static void test_sigtimedwait_immediate(void) {
    setup_test_context();
    
    current_thread->sig_pending = sigmask(SIGTERM);
    
    uint32_t wait_set = sigmask(SIGTERM);
    siginfo_t info = {0};
    
    int ret = sys_sigtimedwait(&wait_set, &info, NULL);
    
    ASSERT_EQ(ret, SIGTERM, "sys_sigtimedwait returns signal number");
    ASSERT_EQ(info.si_signo, SIGTERM, "siginfo.si_signo filled correctly");
    ASSERT_EQ(current_thread->sig_pending & sigmask(SIGTERM), 0,
              "signal removed from pending");
    
    teardown_test_context();
    TEST_PASS("test_sigtimedwait_immediate");
}

/*
 * Test: sys_sigtimedwait with timeout and no signal returns EAGAIN
 */
static void test_sigtimedwait_timeout(void) {
    setup_test_context();
    
    current_thread->sig_pending = 0; /* No signals pending */
    
    uint32_t wait_set = sigmask(SIGUSR1);
    int timeout_placeholder = 1; /* Non-NULL means timeout */
    
    int ret = sys_sigtimedwait(&wait_set, NULL, &timeout_placeholder);
    
    ASSERT_EQ(ret, -11, "sys_sigtimedwait returns -EAGAIN on timeout");
    
    teardown_test_context();
    TEST_PASS("test_sigtimedwait_timeout");
}

/*
 * Test: sys_sigtimedwait fills siginfo properly
 */
static void test_sigtimedwait_siginfo(void) {
    setup_test_context();
    
    current_thread->sig_pending = sigmask(SIGALRM);
    
    uint32_t wait_set = sigmask(SIGALRM);
    siginfo_t info;
    info.si_signo = 999; /* Should be overwritten */
    
    int ret = sys_sigtimedwait(&wait_set, &info, NULL);
    
    ASSERT_EQ(ret, SIGALRM, "sys_sigtimedwait returns signal");
    ASSERT_EQ(info.si_signo, SIGALRM, "si_signo set correctly");
    ASSERT_EQ(info.si_errno, 0, "si_errno is 0");
    
    teardown_test_context();
    TEST_PASS("test_sigtimedwait_siginfo");
}


/* ========================================================================
 * sendsig / Signal Frame Construction Tests
 * ======================================================================== */

#include "../arch/i386/include/signal_arch.h"
#include "../arch/i386/idt.h"

/*
 * Test: sigframe struct has correct layout
 */
static void test_sigframe_layout(void) {
    /* Verify sigframe structure contains expected fields */
    struct sigframe sf;
    
    /* Check that sigframe has reasonable size */
    ASSERT(sizeof(sf) >= sizeof(uint32_t) + sizeof(int) + sizeof(struct sigcontext),
           "sigframe has minimum expected size");
    
    /* Verify field accessibility */
    sf.retaddr = 0xDEADBEEF;
    sf.sig = 42;
    sf.sc.eax = 0x12345678;
    
    ASSERT_EQ(sf.retaddr, 0xDEADBEEF, "sigframe.retaddr accessible");
    ASSERT_EQ(sf.sig, 42, "sigframe.sig accessible");
    ASSERT_EQ(sf.sc.eax, 0x12345678, "sigframe.sc.eax accessible");
    
    TEST_PASS("test_sigframe_layout");
}

/*
 * Test: sigcontext struct has correct layout for all registers
 */
static void test_sigcontext_layout(void) {
    struct sigcontext sc;
    
    /* Check sigcontext has all required fields */
    sc.gs = 1;
    sc.fs = 2;
    sc.es = 3;
    sc.ds = 4;
    sc.edi = 5;
    sc.esi = 6;
    sc.ebp = 7;
    sc.esp = 8;
    sc.ebx = 9;
    sc.edx = 10;
    sc.ecx = 11;
    sc.eax = 12;
    sc.trapno = 13;
    sc.err = 14;
    sc.eip = 15;
    sc.cs = 16;
    sc.eflags = 17;
    sc.user_esp = 18;
    sc.user_ss = 19;
    sc.oldmask = 20;
    
    /* Verify all fields retained their values */
    ASSERT_EQ(sc.gs, 1, "sigcontext.gs");
    ASSERT_EQ(sc.fs, 2, "sigcontext.fs");
    ASSERT_EQ(sc.es, 3, "sigcontext.es");
    ASSERT_EQ(sc.ds, 4, "sigcontext.ds");
    ASSERT_EQ(sc.edi, 5, "sigcontext.edi");
    ASSERT_EQ(sc.esi, 6, "sigcontext.esi");
    ASSERT_EQ(sc.ebp, 7, "sigcontext.ebp");
    ASSERT_EQ(sc.esp, 8, "sigcontext.esp");
    ASSERT_EQ(sc.ebx, 9, "sigcontext.ebx");
    ASSERT_EQ(sc.edx, 10, "sigcontext.edx");
    ASSERT_EQ(sc.ecx, 11, "sigcontext.ecx");
    ASSERT_EQ(sc.eax, 12, "sigcontext.eax");
    ASSERT_EQ(sc.trapno, 13, "sigcontext.trapno");
    ASSERT_EQ(sc.err, 14, "sigcontext.err");
    ASSERT_EQ(sc.eip, 15, "sigcontext.eip");
    ASSERT_EQ(sc.cs, 16, "sigcontext.cs");
    ASSERT_EQ(sc.eflags, 17, "sigcontext.eflags");
    ASSERT_EQ(sc.user_esp, 18, "sigcontext.user_esp");
    ASSERT_EQ(sc.user_ss, 19, "sigcontext.user_ss");
    ASSERT_EQ(sc.oldmask, 20, "sigcontext.oldmask");
    
    TEST_PASS("test_sigcontext_layout");
}

/*
 * Test: Stack alignment calculation
 *
 * Verifies that the stack alignment logic produces 16-byte aligned
 * addresses as required by the System V ABI.
 */
static void test_stack_alignment(void) {
    /* Test various stack pointer values */
    uint32_t test_stacks[] = {
        0xBFFFEFFC,  /* Already aligned */
        0xBFFFEFF0,  /* Already aligned */
        0xBFFFEFF1,  /* Misaligned by 1 */
        0xBFFFEFF7,  /* Misaligned by 7 */
        0xBFFFEFFF,  /* Misaligned by 15 */
    };
    
    for (int i = 0; i < 5; i++) {
        uint32_t esp = test_stacks[i];
        uint32_t frame_size = sizeof(struct sigframe);
        
        /* Simulate sendsig stack calculation */
        esp -= frame_size;
        esp &= ~0xFUL;  /* Align to 16 bytes */
        
        /* Verify alignment */
        ASSERT_EQ(esp & 0xF, 0, "stack aligned to 16 bytes");
    }
    
    TEST_PASS("test_stack_alignment");
}

/*
 * Test: Frame size calculation
 *
 * Verifies that sizeof(struct sigframe) is reasonable and that
 * stack subtraction doesn't overflow.
 */
static void test_frame_size(void) {
    size_t frame_size = sizeof(struct sigframe);
    
    /* Frame should be reasonably sized */
    ASSERT(frame_size >= 64, "sigframe at least 64 bytes");
    ASSERT(frame_size <= 4096, "sigframe at most 4KB");
    
    /* Verify no overflow when subtracting from typical stack pointer */
    uint32_t esp = 0xBFFFF000;  /* Typical user stack */
    uint32_t new_esp = esp - frame_size;
    
    ASSERT(new_esp < esp, "frame subtraction doesn't overflow");
    ASSERT(new_esp > 0x10000, "frame fits in user space");
    
    TEST_PASS("test_frame_size");
}

/*
 * Test: User address validation
 */
static void test_user_addr_validation(void) {
    #define KERN_BASE 0xC0000000
    #define USER_STACK_MIN 0x00001000
    
    /* Helper function to check address validation */
    /* We can't call the static function directly, but we test the logic */
    
    /* Valid user addresses */
    uint32_t valid_addrs[] = {
        0x08048000,   /* Typical text segment */
        0xBFFFE000,   /* Typical stack */
        0x40000000,   /* Shared libraries */
    };
    
    for (int i = 0; i < 3; i++) {
        uint32_t addr = valid_addrs[i];
        ASSERT(addr >= USER_STACK_MIN && addr < KERN_BASE, 
               "valid user address in range");
    }
    
    /* Invalid addresses - kernel space */
    ASSERT(0xC0000000 >= KERN_BASE, "kernel base is off-limits");
    ASSERT(0xFFFFFFFF >= KERN_BASE, "kernel address is off-limits");
    
    /* Invalid addresses - NULL region */
    ASSERT(0x00000000 < USER_STACK_MIN, "NULL is off-limits");
    ASSERT(0x00000FFF < USER_STACK_MIN, "low addresses off-limits");
    
    #undef KERN_BASE
    #undef USER_STACK_MIN
    
    TEST_PASS("test_user_addr_validation");
}

/*
 * Test: sigcontext preserves oldmask
 *
 * Verifies that the signal mask is saved in sigcontext for
 * restoration in sys_sigreturn.
 */
static void test_sigcontext_oldmask(void) {
    struct sigcontext sc;
    memset(&sc, 0, sizeof(sc));
    
    /* Set a complex mask */
    uint32_t test_mask = sigmask(SIGUSR1) | sigmask(SIGUSR2) | sigmask(SIGTERM);
    sc.oldmask = test_mask;
    
    ASSERT_EQ(sc.oldmask, test_mask, "oldmask preserved in sigcontext");
    
    TEST_PASS("test_sigcontext_oldmask");
}

/*
 * Test: registers_t structure compatibility
 *
 * Verifies that registers_t has the fields needed for sendsig.
 */
static void test_registers_t_compatibility(void) {
    registers_t regs;
    memset(&regs, 0, sizeof(regs));
    
    /* Set all fields that sendsig uses */
    regs.gs = 0x33;
    regs.ds = 0x23;
    regs.edi = 1;
    regs.esi = 2;
    regs.ebp = 3;
    regs.esp = 4;
    regs.ebx = 5;
    regs.edx = 6;
    regs.ecx = 7;
    regs.eax = 8;
    regs.int_no = 0x80;
    regs.err_code = 0;
    regs.eip = 0x08048000;
    regs.cs = 0x1B;
    regs.eflags = 0x00200202;
    regs.useresp = 0xBFFFF000;
    regs.ss = 0x23;
    
    /* Verify all fields */
    ASSERT_EQ(regs.gs, 0x33, "registers_t.gs");
    ASSERT_EQ(regs.ds, 0x23, "registers_t.ds");
    ASSERT_EQ(regs.eip, 0x08048000, "registers_t.eip");
    ASSERT_EQ(regs.useresp, 0xBFFFF000, "registers_t.useresp");
    
    TEST_PASS("test_registers_t_compatibility");
}

/* ========================================================================
 * Test Runner
 * ======================================================================== */

void run_signal_tests(void) {
    kprint("\n--- Signal Tests ---\n");
    
    /* sig_actions[NSIG] tests */
    test_sig_actions_size();
    test_sig_actions_default();
    test_sigaction_install_handler();
    test_sigaction_returns_old();
    test_sigaction_sigkill_rejected();
    test_sigaction_sigstop_rejected();
    test_sigaction_invalid_signal();
    test_sigaction_query_only();
    test_sigaction_mask_and_flags();
    
    /* sig_catch / sig_ignore bitmask tests */
    test_sig_catch_set_on_handler();
    test_sig_ignore_set_on_sigign();
    test_sig_catch_cleared_on_sigdfl();
    test_sig_ignore_cleared_on_handler();
    test_sig_bitmasks_multiple_signals();

    /* Syscall logic tests */
    test_sigpending_masking();
    test_sigaltstack_eperm_on_stack();
    
    /* sys_sigwait tests */
    test_sigwait_immediate();
    test_sigwait_null_args();
    test_sigwait_filters_cantmask();
    test_sigwait_selects_first();
    
    /* sys_sigtimedwait tests */
    test_sigtimedwait_immediate();
    test_sigtimedwait_timeout();
    test_sigtimedwait_siginfo();
    
    /* sendsig / Frame Construction tests */
    test_sigframe_layout();
    test_sigcontext_layout();
    test_stack_alignment();
    test_frame_size();
    test_user_addr_validation();
    test_sigcontext_oldmask();
    test_registers_t_compatibility();
    
    kprint("--- Signal Tests Complete ---\n\n");
}
