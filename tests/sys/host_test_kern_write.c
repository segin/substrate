#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <sys/types.h>

// To test kern_write from sys/kern/syscall.c, we need to mock current_process, MAX_FD, etc.
#define MAX_FD 256
#define IO_CHUNK_SIZE 4096

typedef unsigned char uint8_t;

// Mock fs_node
typedef struct fs_node {
    int flags;
    size_t (*write)(struct fs_node *node, off_t offset, size_t size, const uint8_t *buffer);
} fs_node_t;

// Mock file_t
typedef struct file {
    fs_node_t *f_data;
    off_t f_offset;
    short f_flag;
    int f_count;
} file_t;

// Mock process_t
typedef struct process {
    file_t *fds[MAX_FD];
} process_t;

// Globals
process_t mock_proc;
process_t *current_process = &mock_proc;

// Mock VFS functions
static size_t last_write_size = 0;
static size_t total_written = 0;

size_t write_fs(fs_node_t *node, off_t offset, size_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    last_write_size = size;
    total_written += size;
    return size;
}

// Function to test
int kern_write(int fd, const char *buf, int len) {
    if (fd < 0 || fd >= MAX_FD) return -1;
    if (len < 0) return -22; // EINVAL
    if (len == 0) return 0;

    file_t *f = current_process->fds[fd];
    if (!f) return -1;

    // Check for node write support
    if (f->f_data && ((fs_node_t*)f->f_data)->write) {
        int bytes = (int)write_fs((fs_node_t*)f->f_data, f->f_offset, len, (const uint8_t*)buf);

        if (bytes > 0) {
            f->f_offset += bytes;
        }

        return bytes;
    } else
        return 0;
}

int main() {
    printf("Running kern_write tests...\n");

    // Setup mock
    fs_node_t mock_node;
    mock_node.write = write_fs;

    file_t mock_file;
    mock_file.f_data = &mock_node;
    mock_file.f_offset = 0;

    current_process->fds[1] = &mock_file;

    // Test 1: Invalid FD
    assert(kern_write(-1, "test", 4) == -1);
    assert(kern_write(MAX_FD, "test", 4) == -1);

    // Test 2: Invalid len
    assert(kern_write(1, "test", -1) == -22);

    // Test 3: Zero len
    assert(kern_write(1, "test", 0) == 0);

    // Test 4: Valid write, small buffer
    total_written = 0;
    int ret = kern_write(1, "test", 4);
    assert(ret == 4);
    assert(last_write_size == 4);
    assert(total_written == 4);
    assert(mock_file.f_offset == 4);

    // Test 5: Valid write, exact chunk size
    total_written = 0;
    mock_file.f_offset = 0;
    char large_buf[IO_CHUNK_SIZE];
    ret = kern_write(1, large_buf, IO_CHUNK_SIZE);
    assert(ret == IO_CHUNK_SIZE);
    assert(last_write_size == IO_CHUNK_SIZE);
    assert(total_written == IO_CHUNK_SIZE);
    assert(mock_file.f_offset == IO_CHUNK_SIZE);

    // Test 6: Valid write, larger than chunk size
    total_written = 0;
    mock_file.f_offset = 0;
    char huge_buf[IO_CHUNK_SIZE * 2 + 10];
    ret = kern_write(1, huge_buf, sizeof(huge_buf));
    assert(ret == sizeof(huge_buf));
    assert(last_write_size == sizeof(huge_buf));
    assert(total_written == sizeof(huge_buf));
    assert(mock_file.f_offset == sizeof(huge_buf));

    // Test 7: Node without write support
    fs_node_t mock_node_no_write;
    mock_node_no_write.write = NULL;
    file_t mock_file_no_write;
    mock_file_no_write.f_data = &mock_node_no_write;
    current_process->fds[2] = &mock_file_no_write;

    ret = kern_write(2, "test", 4);
    assert(ret == 0);

    printf("All kern_write tests passed!\n");
    return 0;
}
