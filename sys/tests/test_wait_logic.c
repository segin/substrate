#include <sys/proc.h>
#include <sys/wait.h>
#include <sys/errno.h>
#include "tests.h"

// Externs from sys/pm/wait.c (we need to compile it or link against pm.o)
// But sys_wait4 uses current_process global. We need to mock it.
extern int sys_wait4(pid_t pid, int *status, int options, struct rusage *rusage);
extern process_t *current_process;
extern int printf(const char *format, ...);

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAILED: %s (in Test %d?)\n", #cond, 9); \
        panic("Assertion failed"); \
    } else { \
        kprint("PASS: " #cond "\n"); \
    } \
} while(0)

// Mocks
static process_t mock_parent;
static process_t mock_child1;
static process_t mock_child2;
static process_t mock_child3;

// Test globals
static int sched_sleep_calls = 0;
static int sched_sleep_mode = 0; // 0=Wait->Zombie, 1=Signal
extern thread_t *current_thread;

// Mock sched_sleep
void sched_sleep(void *chan) {
    sched_sleep_calls++;
    if (sched_sleep_mode == 0) {
        // Change target child to Zombie to simulate wakeup by exit
        if (mock_child2.pid == 102 && mock_child2.state == SRUN) {
             mock_child2.state = SZOMB;
        }
    } else if (sched_sleep_mode == 1) {
        // Simulate Signal
        current_thread->sig_pending = 1; // Set arbitrary bit
    }
}

void test_wait_logic(void) {
    // Setup Mock Process Tree
    mock_parent.pid = 100;
    mock_parent.pgrp = 100;
    
    mock_child1.pid = 101;
    mock_child1.pgrp = 100;
    mock_child1.state = SZOMB;
    mock_child1.p_sibling = &mock_child2;
    mock_child1.p_parent = &mock_parent;
    mock_child1.exit_code = 10;

    mock_child2.pid = 102;
    mock_child2.pgrp = 100;
    mock_child2.state = SRUN;
    mock_child2.p_sibling = &mock_child3;
    mock_child2.p_parent = &mock_parent;

    mock_child3.pid = 103;
    mock_child3.pgrp = 200;
    mock_child3.state = SZOMB;
    mock_child3.p_sibling = NULL;
    mock_child3.p_parent = &mock_parent;
    mock_child3.exit_code = 20;

    mock_parent.p_children = &mock_child1;
    current_process = &mock_parent; // Mock current!

    int status;
    int ret;

    // Test 1: Wait for specific PID (Zombie)
    ret = sys_wait4(101, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 101);
    TEST_ASSERT(WEXITSTATUS(status) == 10);

    // Test 2: Wait for any child (-1)
    // Should find 101 or 103. First one in list is 101.
    ret = sys_wait4(-1, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 101); // 101 is first in list and zombie

    // Test 3: Wait for process group (0) -> Same as parent (100)
    // 101 is in 100.
    ret = sys_wait4(0, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 101); 

    // Test 4: Wait for specific process group (-200)
    // 103 is in 200.
    ret = sys_wait4(-200, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 103);
    TEST_ASSERT(WEXITSTATUS(status) == 20);

    // Test 5: Wait for non-existent PID
    ret = sys_wait4(999, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == -ECHILD); // find_zombie returns NULL, any_exists=0 since no match

    // Test 6: Wait for running process (102) -> WNOHANG should return 0
    ret = sys_wait4(102, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 0);

    // Test 7: WNOHANG with no zombies (make all running)
    mock_child1.state = SRUN;
    mock_child3.state = SRUN;
    ret = sys_wait4(-1, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 0); // Children exist, but none are zombies

    // Test 8: No children at all (ECHILD)
    mock_parent.p_children = NULL; // Clear list
    ret = sys_wait4(-1, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == -ECHILD);

    // Restore children for blocking tests
    mock_parent.p_children = &mock_child1;

    // Test 9: Blocking Wait (Simulate Sleep -> Wakeup -> Zombie)
    // Setup: Child 102 is SRUN. Wait for 102.
    // Logic: test_sched_sleep callback will change 102 to SZOMB.
    mock_child2.state = SRUN;
    mock_child2.pid = 102;
    mock_child2.exit_code = 30;
    
    // Reset call count
    sched_sleep_calls = 0;
    
    ret = sys_wait4(102, &status, 0, NULL);
    if (ret != 102) {
        printf("DEBUG: Test 9 failed. ret=%d, calls=%d, child2.state=%d\n", ret, sched_sleep_calls, mock_child2.state);
    }
    TEST_ASSERT(ret == 102);
    TEST_ASSERT(WEXITSTATUS(status) == 30);
    TEST_ASSERT(sched_sleep_calls == 1);

    // Test 10: Blocking Wait Interrupted (EINTR)
    // Setup: Wait for 102 (SRUN). Simulation will set sig_pending.
    mock_child2.state = SRUN;
    sched_sleep_calls = 0;
    
    // Set callback mode to SIGNAL
    sched_sleep_mode = 1; 

    ret = sys_wait4(102, &status, 0, NULL);
    TEST_ASSERT(ret == -EINTR);
    TEST_ASSERT(sched_sleep_calls == 1);
    
    // Clear signal for next tests
    current_thread->sig_pending = 0;
}
