#include <kern/console.h>
#include <kern/time.h>
#include <sys/time.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <sys/types.h>

extern int sys_nanosleep(void *req, void *rem);
// Use uint32_t for offset if off_t is not matching correctly with kernel internal,
// but sys_mmap usually takes off_t or uint32_t depending on bitness.
// vm/vm_mmap.c: void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint32_t offset)
// Note: It takes uint32_t offset!
extern void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint32_t offset);
extern int sys_munmap(void *addr, size_t length);

void run_nanosleep_tests(void) {
    kprint("Running nanosleep tests...\n");

    // Allocate user memory for timespec struct to pass checks in copyin
    // We rely on current_process having a valid VM space (which kernel tests usually do)
    void *req_ptr = sys_mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (req_ptr == MAP_FAILED) {
        kprint("FAIL: sys_mmap failed, cannot allocate user memory for test\n");
        return;
    }

    struct timespec *req = (struct timespec *)req_ptr;

    // Test 1: Basic sleep 100ms
    kprint("Test 1: Basic sleep 100ms\n");
    req->tv_sec = 0;
    req->tv_nsec = 100000000; // 100ms

    uint64_t start = get_ticks();
    int ret = sys_nanosleep(req, NULL);
    uint64_t end = get_ticks();

    if (ret != 0) {
        kprintf("FAIL: nanosleep returned error: %d\n", ret);
    } else {
        uint64_t duration = end - start;
        // 100ms = 10 ticks (at 100Hz)
        if (duration < 10) {
            kprintf("FAIL: nanosleep duration too short: %llu ticks (expected >= 10)\n", duration);
        } else {
            kprintf("PASS: nanosleep 100ms took %llu ticks\n", duration);
        }
    }

    // Test 2: Zero sleep
    kprint("Test 2: Zero sleep\n");
    req->tv_sec = 0;
    req->tv_nsec = 0;
    start = get_ticks();
    ret = sys_nanosleep(req, NULL);
    end = get_ticks();
    if (ret != 0) {
        kprintf("FAIL: nanosleep(0) returned error: %d\n", ret);
    } else {
        kprintf("PASS: nanosleep(0) took %llu ticks\n", end - start);
    }

    // Test 3: Invalid input
    kprint("Test 3: Invalid input\n");
    req->tv_sec = -1;
    req->tv_nsec = 0;
    ret = sys_nanosleep(req, NULL);
    if (ret != -EINVAL) {
        kprintf("FAIL: nanosleep(-1 sec) returned %d (expected -EINVAL)\n", ret);
    } else {
        kprint("PASS: nanosleep(-1 sec) returned EINVAL\n");
    }

    req->tv_sec = 0;
    req->tv_nsec = 1000000000; // >= 1s
    ret = sys_nanosleep(req, NULL);
    if (ret != -EINVAL) {
        kprintf("FAIL: nanosleep(1s in nsec) returned %d (expected -EINVAL)\n", ret);
    } else {
        kprint("PASS: nanosleep(1s in nsec) returned EINVAL\n");
    }

    sys_munmap(req_ptr, 4096);
    kprint("nanosleep tests complete.\n");
}
