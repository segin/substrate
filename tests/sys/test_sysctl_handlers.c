/*
 * tests/sys/test_sysctl_handlers.c
 *
 * Unit tests for sysctl handlers.
 */

#include <sys/sysctl.h>
#include <sys/errno.h>
#include <kern/console.h>
#include <string.h>

/* Helper to setup a sysctl_req */
static void setup_req(struct sysctl_req *req, void *oldptr, size_t oldlen, void *newptr, size_t newlen) {
    memset(req, 0, sizeof(*req));
    req->oldptr = oldptr;
    req->oldlen = oldlen;
    req->newptr = newptr;
    req->newlen = newlen;
}

static int test_sysctl_handle_int(void) {
    struct sysctl_oid oid = {0};
    struct sysctl_req req;
    int target_val = 42;
    int old_val;
    int new_val = 100;
    int error;

    kprint("test_sysctl_handle_int: start\n");

    /* Case 1: Read integer variable */
    setup_req(&req, &old_val, sizeof(old_val), NULL, 0);
    error = sysctl_handle_int(&oid, &target_val, 0, &req);
    if (error != 0 || old_val != 42 || req.oldidx != sizeof(int)) {
        kprintf("FAIL: Read int variable (error=%d, val=%d)\n", error, old_val);
        return 1;
    }

    /* Case 2: Write integer variable */
    setup_req(&req, NULL, 0, &new_val, sizeof(new_val));
    error = sysctl_handle_int(&oid, &target_val, 0, &req);
    if (error != 0 || target_val != 100) {
        kprintf("FAIL: Write int variable (error=%d, val=%d)\n", error, target_val);
        return 1;
    }

    /* Case 3: Read constant integer (arg1=NULL, arg2=value) */
    setup_req(&req, &old_val, sizeof(old_val), NULL, 0);
    error = sysctl_handle_int(&oid, NULL, 12345, &req);
    if (error != 0 || old_val != 12345) {
        kprintf("FAIL: Read constant int (error=%d, val=%d)\n", error, old_val);
        return 1;
    }

    /* Case 4: Read buffer too small (ENOMEM) */
    char small_buf[1];
    setup_req(&req, small_buf, sizeof(small_buf), NULL, 0);
    error = sysctl_handle_int(&oid, &target_val, 0, &req);
    if (error != ENOMEM) {
        kprintf("FAIL: Read buffer too small (error=%d)\n", error);
        return 1;
    }

    /* Case 5: Write buffer too small (EINVAL) */
    char small_new_val[1] = {0};
    setup_req(&req, NULL, 0, small_new_val, sizeof(small_new_val));
    error = sysctl_handle_int(&oid, &target_val, 0, &req);
    if (error != EINVAL) {
        kprintf("FAIL: Write buffer too small (error=%d)\n", error);
        return 1;
    }

    kprint("test_sysctl_handle_int: PASS\n");
    return 0;
}

static int test_sysctl_handle_string(void) {
    struct sysctl_oid oid = {0};
    struct sysctl_req req;
    char target_str[32] = "hello";
    char old_str[32];
    char new_str[] = "world";
    int error;

    kprint("test_sysctl_handle_string: start\n");

    /* Case 1: Read string */
    memset(old_str, 0, sizeof(old_str));
    setup_req(&req, old_str, sizeof(old_str), NULL, 0);
    error = sysctl_handle_string(&oid, target_str, sizeof(target_str), &req);
    if (error != 0 || strcmp(old_str, "hello") != 0) {
        kprintf("FAIL: Read string (error=%d, str='%s')\n", error, old_str);
        return 1;
    }

    /* Case 2: Write string */
    setup_req(&req, NULL, 0, new_str, strlen(new_str) + 1);
    /* arg2 is max length for string */
    error = sysctl_handle_string(&oid, target_str, sizeof(target_str), &req);
    if (error != 0 || strcmp(target_str, "world") != 0) {
        kprintf("FAIL: Write string (error=%d, str='%s')\n", error, target_str);
        return 1;
    }

    /* Case 3: Read buffer too small (ENOMEM) */
    char small_buf[3]; /* "world" needs 6 bytes including NULL */
    setup_req(&req, small_buf, sizeof(small_buf), NULL, 0);
    error = sysctl_handle_string(&oid, target_str, sizeof(target_str), &req);
    if (error != ENOMEM) {
        kprintf("FAIL: Read buffer too small (error=%d)\n", error);
        return 1;
    }

    /* Case 4: Write buffer too large (ENAMETOOLONG) */
    char long_str[64];
    memset(long_str, 'A', sizeof(long_str));
    long_str[63] = '\0';
    setup_req(&req, NULL, 0, long_str, sizeof(long_str));
    /* Target buffer is 32 bytes */
    error = sysctl_handle_string(&oid, target_str, 32, &req);
    if (error != ENAMETOOLONG) {
        kprintf("FAIL: Write buffer too large (error=%d)\n", error);
        return 1;
    }

    kprint("test_sysctl_handle_string: PASS\n");
    return 0;
}

static int test_sysctl_handle_opaque(void) {
    struct sysctl_oid oid = {0};
    struct sysctl_req req;
    char target_data[16] = "opaque data";
    char old_data[16];
    char new_data[16] = "new data";
    int error;

    kprint("test_sysctl_handle_opaque: start\n");

    /* Case 1: Read opaque data */
    memset(old_data, 0, sizeof(old_data));
    setup_req(&req, old_data, sizeof(old_data), NULL, 0);
    /* arg2 is length of opaque data */
    error = sysctl_handle_opaque(&oid, target_data, 12, &req); // "opaque data" + NULL = 12
    if (error != 0 || memcmp(old_data, target_data, 12) != 0) {
        kprintf("FAIL: Read opaque (error=%d)\n", error);
        return 1;
    }

    /* Case 2: Write opaque data (expect EPERM) */
    setup_req(&req, NULL, 0, new_data, sizeof(new_data));
    error = sysctl_handle_opaque(&oid, target_data, sizeof(target_data), &req);
    if (error != EPERM) {
        kprintf("FAIL: Write opaque allowed (error=%d)\n", error);
        return 1;
    }

    /* Case 3: Read buffer too small (ENOMEM) */
    char small_buf[5];
    setup_req(&req, small_buf, sizeof(small_buf), NULL, 0);
    error = sysctl_handle_opaque(&oid, target_data, 12, &req);
    if (error != ENOMEM) {
        kprintf("FAIL: Read opaque buffer too small (error=%d)\n", error);
        return 1;
    }

    kprint("test_sysctl_handle_opaque: PASS\n");
    return 0;
}

void test_sysctl_handlers(void) {
    kprint("test_sysctl_handlers: starting...\n");
    int failures = 0;

    failures += test_sysctl_handle_int();
    failures += test_sysctl_handle_string();
    failures += test_sysctl_handle_opaque();

    if (failures == 0) {
        kprint("test_sysctl_handlers: ALL PASS\n");
    } else {
        kprintf("test_sysctl_handlers: %d FAILURES\n", failures);
    }
}
