#include <stdint.h>
#include <stddef.h>
#include <sys/time.h>
#include <kern/console.h>
#include <sys/file.h>

// Constants for sys_mmap
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define MAP_PRIVATE 0x002
#define MAP_ANONYMOUS 0x020

// Declaration of sys_mmap
extern void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset);

// Declaration of kprintf
extern int kprintf(const char *fmt, ...);

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}

void run_mmap_perf_test(void) {
    kprint("Starting sys_mmap performance test...\n");

    // Size: 200MB. Assuming system has ~128MB.
    // 200 * 1024 * 1024
    size_t size = 200 * 1024 * 1024;

    uint64_t start = rdtsc();
    void *ret = sys_mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    uint64_t end = rdtsc();

    if (ret != (void*)-1) {
        kprint("ERROR: sys_mmap succeeded? Expected failure due to OOM.\n");
        // If it succeeded, we consumed all memory.
    } else {
        kprint("sys_mmap failed as expected.\n");
    }

    uint64_t diff = end - start;

    // Print in Millions of Cycles
    kprintf("sys_mmap(200MB) failure path took %d million cycles.\n", (uint32_t)(diff / 1000000));
}
