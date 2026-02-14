#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <assert.h>

// Include host system headers for types and constants
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <signal.h>
#include <errno.h>

// Guard kernel headers that conflict with host
// We define these to prevent kernel headers from being processed,
// relying on host headers for types.
#ifndef _SYS_TYPES_H
#define _SYS_TYPES_H
#endif
#ifndef _SYS_TIME_H
#define _SYS_TIME_H
#endif
#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H
#endif
#ifndef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H
#endif

// Mock kernel environment
// HOST_TEST is defined via command line

// Mock fs_node_t before vfs.h
// Wait, vfs.h defines it. We can let vfs.h define it if we include it.
// But vfs.h needs <sys/types.h> (guarded, host version used).
// And <stdint.h> (host version used).
// So it should work.

// Mock kprint
void kprint(const char *str) {
    printf("[KERNEL] %s", str);
}

// Forward declarations for mocks
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
        printf("Error: Write too large for mock buffer\n");
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

// Include source file
// We need to define structures required by acct.c
// acct.c includes: <sys/acct.h>, <sys/proc.h>, <sys/session.h>, <vfs/vfs.h>, <drivers/video/vga.h>, <kern/sched.h>

// We need to mock sys/proc.h dependencies
// sys/proc.h includes sys/acct.h (ok), sys/signal.h (guarded), sys/resource.h (guarded)
// It needs struct process definition.

// Mock drivers/video/vga.h and kern/sched.h if needed
// acct.c doesn't seem to use them explicitly, but includes them.
// Let's create dummy headers or just let them fail if they are missing?
// No, -Isys/include will find them if they exist.
// drivers/video/vga.h likely includes low-level stuff.
// kern/sched.h likely includes thread stuff.

// Let's try to include acct.c directly.
// But first, we need to handle headers.

// Define dummy macros/types if needed.

// Helper to include headers from sys/include
// We rely on -Isys/include

// Mock current_process
// We need struct process to be defined.
// sys/proc.h defines it.
// We'll define a global current_process pointer.
// But we need the type.
// So we must include sys/proc.h.
// sys/proc.h needs struct pgrp, struct session, struct sigaction (host), struct rusage (host).

// We need to prevent sys/proc.h from including other kernel headers that might conflict?
// It includes <sys/acct.h> which is fine.

// Let's include the source!
// But wait, acct.c includes <drivers/video/vga.h>.
// vga.h might include <sys/io.h> or inline assembly which fails on host.
// We can create a dummy vga.h or mock the include.
// Since we can't easily mock the file, we can define a macro to skip it if it was guarded?
// No, C doesn't work that way.
// We can use the fact that we include acct.c directly.
// We can define the header path to something else? No.

// We can create a temporary directory `mocks/drivers/video/vga.h` and add it to include path.
// OR we can just hope it's empty enough or guarded.

// Let's try to stub the headers by creating local files.
// But first, let's try to compile without mocks for headers and see errors.

#include "../../sys/kern/acct.c"

// Re-declare globals if needed (acct_node is static in acct.c, so it's available here)
// current_process is extern in acct.c. We need to define it.
process_t mock_proc_struct;
process_t *current_process = &mock_proc_struct;

// fs_root is extern in acct.c (implied via finddir_fs usage?)
// acct.c: fs_node_t *node = finddir_fs(fs_root, (char*)path);
fs_node_t *fs_root = NULL;

// Test Runner
int main() {
    printf("Running acct tests...\n");

    // Setup mock file
    fs_node_t file_node;
    memset(&file_node, 0, sizeof(file_node));
    file_node.flags = FS_FILE;
    file_node.length = 0;
    mock_acct_file = &file_node;
    mock_root = &file_node; // Just a placeholder
    fs_root = mock_root;

    // Test 1: sys_acct(NULL) -> disable
    printf("Test 1: sys_acct(NULL)...\n");
    acct_node = &file_node; // Simulate enabled
    sys_acct(NULL);
    assert(acct_node == NULL);
    assert(close_fs_called == 1);
    printf("PASS\n");

    // Test 2: sys_acct("nonexistent") -> fail
    printf("Test 2: sys_acct(\"nonexistent\")...\n");
    int ret = sys_acct("nonexistent");
    assert(ret == -1);
    assert(acct_node == NULL);
    printf("PASS\n");

    // Test 3: sys_acct("acctfile") -> success
    printf("Test 3: sys_acct(\"acctfile\")...\n");
    close_fs_called = 0;
    open_fs_called = 0;
    ret = sys_acct("acctfile");
    assert(ret == 0);
    assert(acct_node == &file_node);
    assert(open_fs_called == 1);
    printf("PASS\n");

    // Test 4: acct_process (No accounting enabled)
    printf("Test 4: acct_process (disabled)...\n");
    acct_node = NULL;
    last_write_size = 0;
    acct_process(0);
    assert(last_write_size == 0);
    printf("PASS\n");

    // Test 5: acct_process (Enabled)
    printf("Test 5: acct_process (enabled)...\n");
    acct_node = &file_node;
    // Setup process
    memset(&mock_proc_struct, 0, sizeof(mock_proc_struct));
    strncpy(mock_proc_struct.comm, "test_cmd", AC_COMM_LEN);
    mock_proc_struct.start_time = 100;
    mock_proc_struct.utime = 10; // ticks
    mock_proc_struct.stime = 20; // ticks
    mock_proc_struct.uid = 1001;
    mock_proc_struct.gid = 1001;
    mock_proc_struct.ac_flag = 0;

    // Mock time = 1000. Elapsed = 1000 - 100 = 900.

    last_write_size = 0;
    acct_process(0);

    assert(last_write_size == sizeof(struct acct));

    struct acct *ac = (struct acct *)last_write_buffer;
    printf("Written comm: %s\n", ac->ac_comm);
    assert(strcmp(ac->ac_comm, "test_cmd") == 0);
    assert(ac->ac_btime == 100);
    assert(ac->ac_uid == 1001);
    assert(ac->ac_gid == 1001);
    assert(ac->ac_flag == AFORK); // AFORK is added by acct_process

    // Verify compression
    // compress(10) -> exp=0, val=10 -> 10
    // compress(20) -> exp=0, val=20 -> 20
    // compress(900) -> exp=0, val=900 -> 900
    // Note: compress logic: while (t >= 8192) ...
    assert(ac->ac_utime == 10);
    assert(ac->ac_stime == 20);
    assert(ac->ac_etime == 900);

    printf("PASS\n");

    printf("All tests passed!\n");
    return 0;
}
