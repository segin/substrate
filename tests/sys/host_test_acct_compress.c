#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <assert.h>
#include <limits.h>

// Host system headers for types and constants
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <errno.h>
#include <dirent.h>

// Guard kernel headers to prevent conflict with host headers
// We use the guards from the actual kernel headers (Substrate)
#ifndef _SUBSTRATE_SYS_TYPES_H
#define _SUBSTRATE_SYS_TYPES_H
#endif
#ifndef _SUBSTRATE_SYS_TIME_H
#define _SUBSTRATE_SYS_TIME_H
#endif
#ifndef _SUBSTRATE_SYS_RESOURCE_H
#define _SUBSTRATE_SYS_RESOURCE_H
#endif
#ifndef _SUBSTRATE_SYS_SIGNAL_H
#define _SUBSTRATE_SYS_SIGNAL_H
#endif
#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H
#endif

// Mock kprint
void kprint(const char *str) {
    printf("[KERNEL] %s", str);
}

// Forward declarations for kernel structures
struct fs_node;
typedef struct fs_node fs_node_t;

// Global state for mocks
static fs_node_t *mock_acct_file = NULL;
static fs_node_t *mock_root = NULL;
static int close_fs_called = 0;
static int open_fs_called = 0;
static size_t last_write_size = 0;
static uint8_t last_write_buffer[1024];

// Mock VFS functions
void close_fs(fs_node_t *node) {
    (void)node;
    close_fs_called++;
}

void open_fs(fs_node_t *node, uint8_t read, uint8_t write) {
    (void)node; (void)read; (void)write;
    open_fs_called++;
}

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset;
    if (size > sizeof(last_write_buffer)) {
        printf("Error: Write too large for mock buffer (%zu > %zu)\n", size, sizeof(last_write_buffer));
        return 0;
    }
    memcpy(last_write_buffer, buffer, size);
    last_write_size = size;
    return size;
}

fs_node_t *finddir_fs(fs_node_t *node, char *name) {
    (void)node;
    if (strcmp(name, "acctfile") == 0) {
        return mock_acct_file;
    }
    return NULL;
}

// Mock get_time
static uint32_t mock_time = 1000;
uint32_t get_time(void) {
    return mock_time;
}

// Mock copyinstr (needed by sys_acct)
int copyinstr(const void *uaddr, void *kaddr, size_t len, size_t *done) {
    // In host test, uaddr is a valid host pointer (string literal or buffer)
    // kaddr is a kernel buffer (stack or heap)
    if (!uaddr || !kaddr) return -1; // EFAULT
    strncpy((char*)kaddr, (const char*)uaddr, len);
    ((char*)kaddr)[len-1] = '\0'; // Ensure termination
    if (done) *done = strlen((char*)kaddr) + 1;
    return 0;
}

// Include source file directly to test internal functions
// Note: This pulls in sys/acct.h, sys/proc.h, vfs/vfs.h, etc. which define types like comp_t, process_t, fs_node_t.
// We rely on -Imock_include to provide necessary mocks or redirect to real kernel headers where appropriate.
#include "../../sys/kern/acct.c"

// Re-declare globals if needed
// process_t is defined in sys/proc.h (included by acct.c)
process_t mock_proc_struct;
process_t *current_process = &mock_proc_struct;

// fs_root is extern in acct.c
fs_node_t *fs_root = NULL;

// Helper to decompress for verification
// Value = mantissa * 8^exp
// mantissa is low 13 bits. exp is high 3 bits.
uint64_t decompress(comp_t c) {
    int exp = (c >> 13) & 0x7;
    int mantissa = c & 0x1FFF;
    if (exp == 0) return mantissa;
    // Note: Implicit rounding/precision loss means this won't be exact
    return (uint64_t)mantissa << (3 * exp);
}

void test_compress_zero() {
    printf("Test: compress(0)\n");
    comp_t res = compress(0);
    assert(res == 0);
    printf("PASS\n");
}

void test_compress_max_uncompressed() {
    printf("Test: compress(8191)\n");
    comp_t res = compress(8191);
    // Should be 0 exp, 8191 mantissa
    assert(res == 8191);
    printf("PASS\n");
}

void test_compress_boundary() {
    printf("Test: compress(8192)\n");
    comp_t res = compress(8192);
    // 8192 = 1024 * 8^1.
    // exp=1, mantissa=1024.
    // 1 << 13 | 1024 = 8192 + 1024 = 9216
    assert((res >> 13) == 1);
    assert((res & 0x1FFF) == 1024);
    assert(res == 9216);
    printf("PASS\n");
}

void test_compress_rounding() {
    printf("Test: compress rounding\n");
    // Case 1: No rounding. 8192 + 0 = 8192.
    // 8192 >> 3 = 1024. Remainder 0.
    // Result: exp=1, mant=1024.
    assert(compress(8192) == 9216);

    // Case 2: Rounding up.
    // We want `round = t & 4` to be true.
    // t needs to have bit 2 set.
    // 8192 + 4 = 8196.
    // 8196 >> 3 = 1024. Remainder 4.
    // Round up: mant = 1024 + 1 = 1025.
    // Result: exp=1, mant=1025.
    // Value = 1025 * 8 = 8200.
    comp_t res = compress(8196);
    assert((res >> 13) == 1);
    assert((res & 0x1FFF) == 1025);
    // Error check: input 8196, output 8200. Error 4.
    printf("PASS (8196 -> %d)\n", res);

    // Case 3: Rounding causing further shift?
    // Max mantissa is 8191.
    // If we have t such that t >> 3 == 8191, and round bit set.
    // t = (8191 << 3) + 4 = 65528 + 4 = 65532.
    // 65532 >> 3 = 8191. Round bit 4 is set.
    // mant becomes 8191 + 1 = 8192.
    // 8192 >= 8192. So loop continues!
    // t becomes 8192 >> 3 = 1024.
    // exp increments.
    // Original exp was 1 (from 65532 -> 8191).
    // Now exp becomes 2.
    // Final result: exp=2, mant=1024.
    // Value = 1024 * 8^2 = 1024 * 64 = 65536.
    // Input 65532 -> Output 65536.
    res = compress(65532);
    assert((res >> 13) == 2);
    assert((res & 0x1FFF) == 1024);
    printf("PASS (65532 -> %d)\n", res);
}

void test_compress_max_uint32() {
    printf("Test: compress(UINT32_MAX)\n");
    comp_t res = compress(UINT32_MAX);
    // Analysis:
    // Loop runs 7 times. round is set on last iteration (bit 2 was 1).
    // Final t is 2047.
    // Rounding increments t to 2048.
    // Result: exp=7, mant=2048.
    // 7 << 13 | 2048 = 57344 + 2048 = 59392.
    // 0xE800.
    if ((res & 0x1FFF) != 2048) {
        printf("FAIL: Expected mantissa 2048, got %d\n", res & 0x1FFF);
    }
    assert((res >> 13) == 7);
    assert((res & 0x1FFF) == 2048);
    printf("PASS\n");
}

void test_monotonicity() {
    printf("Test: Monotonicity\n");
    // Check random increasing values
    uint32_t last_v = 0;
    comp_t last_c = compress(0);

    for (int i = 0; i < 100000; i++) {
        uint32_t v = (rand() % 10000) + last_v + 1;
        if (v < last_v) break; // Overflow
        comp_t c = compress(v);
        // Decompressed value should be roughly increasing
        // But strictly, compressed representation as integer should be increasing?
        // Not necessarily, because (exp, mant) pairs map to values.
        // But (exp << 13) + mant is monotonic with value?
        // Yes, because higher exp means higher value range.
        // And within same exp, higher mant means higher value.
        // So yes, comp_t as uint16_t should be monotonic.
        if (c < last_c) {
            printf("FAIL: Monotonicity violation: %u -> %u, %u -> %u\n", last_v, last_c, v, c);
            assert(0);
        }
        last_v = v;
        last_c = c;
    }
    printf("PASS\n");
}

void test_integration_large_values() {
    printf("Test: Integration with large values\n");
    // Setup file
    fs_node_t file_node;
    memset(&file_node, 0, sizeof(file_node));
    file_node.flags = FS_FILE;
    acct_node = &file_node;

    // Setup process with large times
    memset(&mock_proc_struct, 0, sizeof(mock_proc_struct));
    strncpy(mock_proc_struct.comm, "big_job", AC_COMM_LEN);
    mock_proc_struct.start_time = 1000;
    mock_proc_struct.utime = 65532; // Expect 65536 decompressed
    mock_proc_struct.stime = 8192;  // Expect 9216 (compressed raw) -> 8192 decompressed
    mock_proc_struct.uid = 0;
    mock_proc_struct.gid = 0;

    mock_time = 2000; // Elapsed = 1000

    last_write_size = 0;
    acct_process(0);

    assert(last_write_size == sizeof(struct acct));
    struct acct *ac = (struct acct *)last_write_buffer;

    printf("Written: utime=%u (raw), stime=%u (raw)\n", ac->ac_utime, ac->ac_stime);

    // Check utime
    // 65532 -> exp=2, mant=1024 -> 0x4400 (17408 dec? No. 2<<13 + 1024 = 16384 + 1024 = 17408)
    // Wait. 2 << 13 = 16384. + 1024 = 17408.
    // Let's check against compress(65532).
    assert(ac->ac_utime == compress(65532));
    assert(ac->ac_utime == 17408);

    // Check stime
    // 8192 -> exp=1, mant=1024 -> 1<<13 + 1024 = 8192 + 1024 = 9216.
    assert(ac->ac_stime == compress(8192));
    assert(ac->ac_stime == 9216);

    printf("PASS\n");
}

int main() {
    printf("Running host_test_acct_compress...\n");

    // Initialize mocks
    fs_node_t file_node;
    memset(&file_node, 0, sizeof(file_node));
    file_node.flags = FS_FILE;
    mock_acct_file = &file_node;
    mock_root = &file_node;
    fs_root = mock_root;

    test_compress_zero();
    test_compress_max_uncompressed();
    test_compress_boundary();
    test_compress_rounding();
    test_compress_max_uint32();
    test_monotonicity();
    test_integration_large_values();

    printf("All tests passed!\n");
    return 0;
}
