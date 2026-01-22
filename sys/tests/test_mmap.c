/*
 * Unit tests for mmap/munmap/mprotect
 */

#include <vm/vm_area.h>
#include <vm/vm_mmap.c>
#include <kern/console.h>
#include <sys/mman.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        kprint("FAIL: "); kprint(msg); kprint("\n"); \
        tests_failed++; \
        return; \
    } \
    tests_passed++; \
} while(0)

// Test 1: Basic anonymous mmap
void test_mmap_anonymous(void) {
    kprint("Test: anonymous mmap\n");
    
    void *addr = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE, 
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap succeeded");
    TEST_ASSERT(addr != NULL, "got valid address");
    
    // Try to write to it
    char *ptr = (char *)addr;
    ptr[0] = 'A';
    TEST_ASSERT(ptr[0] == 'A', "can write to mapped memory");
    
    // Unmap
    int ret = sys_munmap(addr, 4096);
    TEST_ASSERT(ret == 0, "munmap succeeded");
    
    kprint("  PASS\n");
}

// Test 2: Multiple mappings
void test_multiple_mappings(void) {
    kprint("Test: multiple mappings\n");
    
    void *addr1 = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void *addr2 = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    void *addr3 = sys_mmap(NULL, 8192, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    
    TEST_ASSERT(addr1 != MAP_FAILED, "first mmap succeeded");
    TEST_ASSERT(addr2 != MAP_FAILED, "second mmap succeeded");
    TEST_ASSERT(addr3 != MAP_FAILED, "third mmap succeeded");
    
    TEST_ASSERT(addr1 != addr2, "mappings don't overlap");
    TEST_ASSERT(addr2 != addr3, "mappings don't overlap");
    
    sys_munmap(addr1, 4096);
    sys_munmap(addr2, 4096);
    sys_munmap(addr3, 8192);
    
    kprint("  PASS\n");
}

// Test 3: MAP_FIXED
void test_mmap_fixed(void) {
    kprint("Test: MAP_FIXED\n");
    
    void *target = (void *)0x50000000;
    void *addr = sys_mmap(target, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    
    TEST_ASSERT(addr == target, "MAP_FIXED mapped at requested address");
    
    sys_munmap(addr, 4096);
    
    kprint("  PASS\n");
}

// Test 4: mprotect
void test_mprotect(void) {
    kprint("Test: mprotect\n");
    
    void *addr = sys_mmap(NULL, 4096, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "mmap succeeded");
    
    // Change to read-only
    int ret = sys_mprotect(addr, 4096, PROT_READ);
    TEST_ASSERT(ret == 0, "mprotect succeeded");
    
    sys_munmap(addr, 4096);
    
    kprint("  PASS\n");
}

// Test 5: Large mapping
void test_large_mapping(void) {
    kprint("Test: large mapping\n");
    
    // Map 1MB
    void *addr = sys_mmap(NULL, 1024 * 1024, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    TEST_ASSERT(addr != MAP_FAILED, "large mmap succeeded");
    
    // Write to various pages
    char *ptr = (char *)addr;
    ptr[0] = 'A';
    ptr[4096] = 'B';
    ptr[1024 * 1024 - 1] = 'C';
    
    TEST_ASSERT(ptr[0] == 'A', "first page accessible");
    TEST_ASSERT(ptr[4096] == 'B', "second page accessible");
    TEST_ASSERT(ptr[1024 * 1024 - 1] == 'C', "last page accessible");
    
    sys_munmap(addr, 1024 * 1024);
    
    kprint("  PASS\n");
}

void run_mmap_tests(void) {
    kprint("\n=== MMAP Unit Tests ===\n");
    
    test_mmap_anonymous();
    test_multiple_mappings();
    test_mmap_fixed();
    test_mprotect();
    test_large_mapping();
    
    kprint("\nResults: ");
    kprint("Passed: ");
    kprint(" Failed: ");
    kprint("\n");
}
