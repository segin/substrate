#include <sys/kobject.h>
#include <kern/console.h>
#include <string.h>

static int release_called = 0;
static struct kobject *released_obj = NULL;

static void my_release(struct kobject *kobj) {
    release_called++;
    released_obj = kobj;
}

void run_kobject_tests(void) {
    kprint("Running kobject tests...\n");

    struct kobject kobj;
    // Initialize
    kobject_init(&kobj, "test_obj");

    // Test initialization
    if (kobj.refcount != 1) {
        kprint("FAIL: Initial refcount should be 1, got %u\n", kobj.refcount);
        return;
    }
    if (kobj.release != NULL) {
        kprint("FAIL: Initial release callback should be NULL\n");
        return;
    }

    // Set release callback
    kobj.release = my_release;

    // Test get
    kobject_get(&kobj);
    if (kobj.refcount != 2) {
        kprint("FAIL: Refcount after get should be 2, got %u\n", kobj.refcount);
        return;
    }

    // Test put (refcount 2 -> 1, no release)
    kobject_put(&kobj);
    if (kobj.refcount != 1) {
        kprint("FAIL: Refcount after first put should be 1, got %u\n", kobj.refcount);
        return;
    }
    if (release_called != 0) {
        kprint("FAIL: Release callback called prematurely\n");
        return;
    }

    // Test put (refcount 1 -> 0, release called)
    kobject_put(&kobj);
    if (kobj.refcount != 0) {
        kprint("FAIL: Refcount after final put should be 0, got %u\n", kobj.refcount);
        return;
    }
    if (release_called != 1) {
        kprint("FAIL: Release callback not called\n");
        return;
    }
    if (released_obj != &kobj) {
        kprint("FAIL: Release callback called with wrong object\n");
        return;
    }

    // Reset for next test
    release_called = 0;
    released_obj = NULL;

    // Test with NULL release
    struct kobject kobj2;
    kobject_init(&kobj2, "test_obj2");
    kobject_put(&kobj2); // Should not crash
    if (kobj2.refcount != 0) {
        kprint("FAIL: Refcount for kobj2 should be 0, got %u\n", kobj2.refcount);
        return;
    }

    kprint("kobject tests passed!\n");
}
