/*
 * /dev/fuse character device behaviour.
 *
 * This file used to drive a request queue: inject a fuse_in_header, read it
 * back out, check the head/tail wrap.  None of that exists any more.
 * fuse_dev_read() used to sleep until the queue was non-empty, but nothing
 * ever enqueued anything -- fuse_vfs_read never did -- so the wait condition
 * was permanently unsatisfiable, and the sleep was not interruptible.  Any
 * user could open /dev/fuse, read, and park a thread that not even SIGKILL
 * could recover; vfs_init() registers the device, so it was reachable, not
 * dormant.  587faecf1 made it fail honestly instead.
 *
 * So what is worth testing now is exactly that: the device refuses, promptly,
 * rather than hanging, and the write path still validates its length before
 * refusing -- that check was deliberately kept so a future implementation
 * cannot inherit the unchecked cast to a user-supplied fuse_out_header.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
#include <stdbool.h>

#include <sys/errno.h>

// Include vfs/vfs.h to get fs_node_t definition
#include <vfs/vfs.h>

// Mock dependencies
void devfs_register_device(fs_node_t *node) { (void)node; }

// Rename conflicting functions
#define fuse_init real_fuse_init
#define fuse_fs_init real_fuse_fs_init

/*
 * sched_sleep must never be reached.  If a future change reintroduces the
 * unsatisfiable wait, this turns a hung test run into a clean failure.
 */
static int sched_sleep_count = 0;
void mock_sched_sleep(void *chan);
#define sched_sleep mock_sched_sleep

// Include source
#include "../../../sys/fs/fuse.c"

void mock_sched_sleep(void *chan)
{
    (void)chan;
    sched_sleep_count++;
}

static bool test_fuse_read_refuses_without_hanging(void)
{
    struct fuse_in_header buffer;
    size_t rc;

    printf("Testing /dev/fuse read refuses instead of parking...\n");

    sched_sleep_count = 0;

    rc = fuse_dev_read(NULL, 0, sizeof(buffer), (uint8_t *)&buffer);
    if (rc != (size_t)-ENOSYS) {
        printf("FAIL: expected -ENOSYS (%zu), got %zu\n",
               (size_t)-ENOSYS, rc);
        return false;
    }

    /* A short buffer is refused the same way, and still must not sleep. */
    rc = fuse_dev_read(NULL, 0, 1, NULL);
    if (rc != (size_t)-ENOSYS) {
        printf("FAIL: short read expected -ENOSYS (%zu), got %zu\n",
               (size_t)-ENOSYS, rc);
        return false;
    }

    if (sched_sleep_count != 0) {
        printf("FAIL: read slept %d time(s); it must never block on a queue "
               "nothing fills\n", sched_sleep_count);
        return false;
    }

    return true;
}

static bool test_fuse_write_checks_length_before_refusing(void)
{
    struct fuse_out_header reply;
    uint8_t stub[1];
    size_t rc;

    printf("Testing /dev/fuse write validates length...\n");

    memset(&reply, 0, sizeof(reply));

    /* Too short for the header: rejected on length, not on being unimplemented. */
    rc = fuse_dev_write(NULL, 0, sizeof(stub), stub);
    if (rc != (size_t)-EINVAL) {
        printf("FAIL: short write expected -EINVAL (%zu), got %zu\n",
               (size_t)-EINVAL, rc);
        return false;
    }

    /* NULL buffer likewise. */
    rc = fuse_dev_write(NULL, 0, sizeof(reply), NULL);
    if (rc != (size_t)-EINVAL) {
        printf("FAIL: NULL write expected -EINVAL (%zu), got %zu\n",
               (size_t)-EINVAL, rc);
        return false;
    }

    /* Long enough, but there are no outstanding requests to reply to. */
    rc = fuse_dev_write(NULL, 0, sizeof(reply), (const uint8_t *)&reply);
    if (rc != (size_t)-ENOSYS) {
        printf("FAIL: full write expected -ENOSYS (%zu), got %zu\n",
               (size_t)-ENOSYS, rc);
        return false;
    }

    return true;
}

bool test_fuse_read(void)
{
    bool pass = true;

    pass &= test_fuse_read_refuses_without_hanging();
    pass &= test_fuse_write_checks_length_before_refusing();
    return pass;
}
