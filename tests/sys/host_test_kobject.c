#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stddef.h>

// Include the source under test directly
// This avoids needing a symlink or complex build script
#include "../../sys/kern/kobject.c"

// Global state for tracking callbacks
int release_called = 0;
struct kobject *released_obj = NULL;
int failed_tests = 0;

void fail(const char *msg) {
    printf("FAIL: %s\n", msg);
    failed_tests++;
}

void my_release(struct kobject *kobj) {
    release_called++;
    released_obj = kobj;
    // printf("Release called for %s\n", kobj->name);
}

void test_kobject_init(void) {
    printf("Running test_kobject_init...\n");
    struct kobject kobj;
    // Fill with garbage to ensure init clears it properly
    memset(&kobj, 0xAA, sizeof(kobj));

    kobject_init(&kobj, "test_obj");

    if (strcmp(kobj.name, "test_obj") != 0) fail("kobject_init: name incorrect");
    if (kobj.refcount != 1) fail("kobject_init: refcount should be 1");
    if (kobj.parent != NULL) fail("kobject_init: parent should be NULL");
    if (kobj.kset != NULL) fail("kobject_init: kset should be NULL");
    if (kobj.release != NULL) fail("kobject_init: release callback should be NULL");
}

void test_kobject_init_name_truncation(void) {
    printf("Running test_kobject_init_name_truncation...\n");
    struct kobject kobj;
    const char *long_name = "this_is_a_very_long_name_that_exceeds_31_characters_limit";
    char expected_name[32];

    memset(&kobj, 0, sizeof(kobj));
    kobject_init(&kobj, long_name);

    // Expected behavior: first 31 chars copied, last char is null terminator
    strncpy(expected_name, long_name, 31);
    expected_name[31] = '\0';

    if (strcmp(kobj.name, expected_name) != 0) fail("kobject_init: name truncation failed");
    // Ensure null termination at index 31
    if (kobj.name[31] != '\0') fail("kobject_init: name not null terminated at index 31");
}

void test_kobject_get(void) {
    printf("Running test_kobject_get...\n");
    struct kobject kobj;
    kobject_init(&kobj, "test_get");

    struct kobject *ret = kobject_get(&kobj);
    if (ret != &kobj) fail("kobject_get: return value incorrect");
    if (kobj.refcount != 2) fail("kobject_get: refcount incorrect");

    ret = kobject_get(NULL);
    if (ret != NULL) fail("kobject_get: NULL input should return NULL");
}

void test_kobject_put(void) {
    printf("Running test_kobject_put...\n");
    struct kobject kobj;
    kobject_init(&kobj, "test_put");
    kobj.release = my_release;
    release_called = 0;
    released_obj = NULL;

    // Increase refcount to 2
    kobject_get(&kobj);
    if (kobj.refcount != 2) fail("kobject_put: setup failed");

    // Refcount 2 -> 1
    kobject_put(&kobj);
    if (kobj.refcount != 1) fail("kobject_put: decrement failed");
    if (release_called != 0) fail("kobject_put: release called prematurely");

    // Refcount 1 -> 0
    kobject_put(&kobj);
    if (kobj.refcount != 0) fail("kobject_put: final decrement failed");
    if (release_called != 1) fail("kobject_put: release not called");
    if (released_obj != &kobj) fail("kobject_put: wrong object released");

    // Ensure no crash on NULL
    kobject_put(NULL);
}

void test_kobject_put_no_release(void) {
    struct kobject kobj;
    kobject_init(&kobj, "test_no_rel");
    // No release callback set

    kobject_put(&kobj);
    if (kobj.refcount != 0) fail("kobject_put_no_release: refcount not 0");
    // Should not crash
}

void test_kset_init(void) {
    printf("Running test_kset_init...\n");
    struct kset kset;
    memset(&kset, 0xBB, sizeof(kset));

    kset_init(&kset, "test_kset");

    if (strcmp(kset.kobj.name, "test_kset") != 0) fail("kset_init: kobj name incorrect");
    if (kset.kobj.refcount != 1) fail("kset_init: kobj refcount incorrect");
    if (kset.list != NULL) fail("kset_init: list should be NULL");
    if (kset.count != 0) fail("kset_init: count should be 0");

    // Verify internal kobject structure is correct
    if (kset.kobj.parent != NULL) fail("kset_init: kobj parent not NULL");
    if (kset.kobj.kset != NULL) fail("kset_init: kobj kset not NULL");
    if (kset.kobj.release != NULL) fail("kset_init: kobj release should be NULL");
}

void test_kset_init_name_truncation(void) {
    printf("Running test_kset_init_name_truncation...\n");
    struct kset kset;
    const char *long_name = "this_is_a_very_long_name_that_exceeds_31_characters_limit_kset";
    char expected_name[32];

    memset(&kset, 0xCC, sizeof(kset));
    kset_init(&kset, long_name);

    // Expected behavior: first 31 chars copied, last char is null terminator
    strncpy(expected_name, long_name, 31);
    expected_name[31] = '\0';

    if (strcmp(kset.kobj.name, expected_name) != 0) fail("kset_init: name truncation failed");
    if (kset.kobj.name[31] != '\0') fail("kset_init: name not null terminated");
}

int main(void) {
    printf("Running kobject tests...\n");

    test_kobject_init();
    test_kobject_init_name_truncation();
    test_kobject_get();
    test_kobject_put();
    test_kobject_put_no_release();
    test_kset_init();
    test_kset_init_name_truncation();

    if (failed_tests == 0) {
        printf("All kobject tests passed!\n");
        return 0;
    } else {
        printf("kobject tests FAILED: %d failures\n", failed_tests);
        return 1;
    }
}
