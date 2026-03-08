#include <vfs/vfs.h>
#include <kern/sched.h>
#include <sys/proc.h>
#include <vm/vm_kmem.h>
#include <kern/console.h>
#include <string.h>
#include <stdarg.h>

// Forward declaration
extern void pipe_create(fs_node_t **read_node, fs_node_t **write_node);
extern int sys_thr_exit(void *retval);

static volatile int test_error = 0;
static fs_node_t *r_node, *w_node;

#define TEST_COUNT 100

// Sequential test to verify basic pipe functionality and no crashes
int test_pipe_race(void) {
    kprint("Starting pipe basic test...\n");

    for (int iter = 0; iter < 64; iter++) {
        pipe_create(&r_node, &w_node);
        if (!r_node || !w_node) {
            kprint("Failed to create pipe nodes\n");
            return -1;
        }

        // Write some data
        for (int i = 0; i < TEST_COUNT; i++) {
            char c = (char)(i % 256);
            int written = write_fs(w_node, 0, 1, (uint8_t*)&c);
            if (written != 1) {
                kprintf("Writer error iter=%d at %d: wrote %d bytes\n", iter, i, written);
                return -1;
            }
        }

        // Read data back
        for (int i = 0; i < TEST_COUNT; i++) {
            char c;
            int read = read_fs(r_node, 0, 1, (uint8_t*)&c);
            if (read != 1) {
                 kprintf("Reader error iter=%d at %d: read %d bytes\n", iter, i, read);
                 return -1;
            }
            if (c != (char)(i % 256)) {
                kprintf("Data mismatch iter=%d at %d: expected %d, got %d\n", iter, i, (int)(char)(i % 256), (int)c);
                return -1;
            }
        }

        close_fs(w_node);
        w_node = NULL;

        {
            char c = 0;
            int read = read_fs(r_node, 0, 1, (uint8_t *)&c);
            if (read != 0) {
                kprintf("Pipe EOF test failed iter=%d: expected 0, got %d\n", iter, read);
                return -1;
            }
        }

        close_fs(r_node);
        r_node = NULL;
    }

    kprint("Pipe basic test passed successfully.\n");
    return 0;
}
