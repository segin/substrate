#include <sys/kobject.h>
#include <kern/console.h>
#include <string.h>

static int release_called = 0;

static void test_release(struct kobject *kobj) {
    (void)kobj;
    release_called = 1;
}

void run_kobject_tests(void) {
    struct kobject kobj;

    kprint("Running kobject tests...\n");

    // Test 1: Release callback not called when refcount > 0
    release_called = 0;
    kobject_init(&kobj, "test_obj");
    kobj.release = test_release;

    kobject_get(&kobj); // Refcount -> 2
    kobject_put(&kobj); // Refcount -> 1

    if (release_called) {
        kprint("kobject: FAIL - Release called prematurely\n");
    } else {
        kprint("kobject: PASS - Release not called prematurely\n");
    }

    // Test 2: Release callback called when refcount == 0
    kobject_put(&kobj); // Refcount -> 0

    if (release_called) {
        kprint("kobject: PASS - Release called correctly\n");
    } else {
        kprint("kobject: FAIL - Release not called\n");
    }
}
