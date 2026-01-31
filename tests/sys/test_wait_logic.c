#include <sys/proc.h>
#include <sys/wait.h>
#include <errno.h>
#include <stddef.h>
#include "tests.h"

// Externs from sys/pm/wait.c
extern int sys_wait4(pid_t pid, int *status, int options, struct rusage *rusage);
extern process_t *current_process;
extern void panic(const char *msg);
extern void kprint(const char *msg);

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        kprint("FAILED: " #cond "\n"); \
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
    (void)chan; // Unused parameter
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


// Helper to reset mocks
void setup_mocks(void) {
    mock_parent.pid = 100; mock_parent.pgrp = 100;
    
    // Child 1: Zombie, Group 100, Exit 10
    mock_child1.pid = 101; mock_child1.pgrp = 100; mock_child1.state = SZOMB; 
    mock_child1.p_sibling = &mock_child2; mock_child1.p_parent = &mock_parent; mock_child1.exit_code = 10;
    
    // Child 2: Running, Group 100, Exit 0 (will change)
    mock_child2.pid = 102; mock_child2.pgrp = 100; mock_child2.state = SRUN; 
    mock_child2.p_sibling = &mock_child3; mock_child2.p_parent = &mock_parent; mock_child2.exit_code = 0;

    // Child 3: Zombie, Group 200, Exit 20
    mock_child3.pid = 103; mock_child3.pgrp = 200; mock_child3.state = SZOMB; 
    mock_child3.p_sibling = NULL; mock_child3.p_parent = &mock_parent; mock_child3.exit_code = 20;

    mock_parent.p_children = &mock_child1;
    current_process = &mock_parent;
    
    // Reset test checks
    sched_sleep_calls = 0;
    sched_sleep_mode = 0;
    current_thread->sig_pending = 0;
}

void test_wait_logic(void) {
    int status;
    int ret;

    // Test 1: Wait for specific PID (Zombie)
    setup_mocks();
    ret = sys_wait4(101, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 101);
    TEST_ASSERT(WEXITSTATUS(status) == 10);
    // Verify Reaping (101 removed)
    TEST_ASSERT(mock_parent.p_children == &mock_child2); // child1 removed from head
    
    // Test 2: Wait for any child (-1)
    setup_mocks();
    ret = sys_wait4(-1, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 101); // First zombie found
    
    // Test 3: Wait for process group (0) -> Same as parent (100)
    setup_mocks();
    ret = sys_wait4(0, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 101); // 101 is in group 100
    
    // Test 4: Wait for specific process group (-200)
    setup_mocks();
    ret = sys_wait4(-200, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 103); // 103 is in group 200
    TEST_ASSERT(WEXITSTATUS(status) == 20);
    
    // Test 5: Wait for non-existent PID
    setup_mocks();
    ret = sys_wait4(999, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == -ECHILD);
    
    // Test 6: Wait for running process (102) -> WNOHANG should return 0
    setup_mocks();
    ret = sys_wait4(102, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 0);
    
    // Test 7: WNOHANG with no zombies (make all running)
    setup_mocks();
    mock_child1.state = SRUN;
    mock_child3.state = SRUN;
    ret = sys_wait4(-1, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == 0);
    
    // Test 8: No children at all (ECHILD)
    setup_mocks();
    mock_parent.p_children = NULL; // Clear list
    ret = sys_wait4(-1, &status, WNOHANG, NULL);
    TEST_ASSERT(ret == -ECHILD);
    
    // Test 9: Blocking Wait (Simulate Sleep -> Wakeup -> Zombie)
    setup_mocks();
    mock_child2.exit_code = 30; // Set for this test
    
    ret = sys_wait4(102, &status, 0, NULL);
    TEST_ASSERT(ret == 102);
    TEST_ASSERT(WEXITSTATUS(status) == 30);
    TEST_ASSERT(sched_sleep_calls == 1);
    
    // Verify Reaping of 102 (Middle of list removal)
    // list was 101 -> 102 -> 103.
    // 102 removed. 101->p_sibling should be 103.
    TEST_ASSERT(mock_child1.p_sibling == &mock_child3);
    TEST_ASSERT(mock_child2.pid == -1);
    TEST_ASSERT(mock_child2.state == 0);
    
    // Test 10: Blocking Wait Interrupted (EINTR)
    setup_mocks();
    sched_sleep_mode = 1; // Signal mode
    
    ret = sys_wait4(102, &status, 0, NULL);
    TEST_ASSERT(ret == -EINTR);
    TEST_ASSERT(sched_sleep_calls == 1);
}
