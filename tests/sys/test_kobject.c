#include <sys/kobject.h>
#include <kern/console.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

extern int snprintf(char *str, size_t size, const char *format, ...);

static int failed_tests = 0;
static int release_called = 0;
static struct kobject *released_obj = NULL;

static void fail(const char *msg) {
    kprint("FAIL: ");
    kprint(msg);
    kprint("\n");
    failed_tests++;
}

static void my_release(struct kobject *kobj) {
    release_called++;
    released_obj = kobj;
}

static void test_kobject_init(void) {
    struct kobject kobj;
    // Fill with garbage to ensure init clears it properly
    memset(&kobj, 0xAA, sizeof(kobj));

    kobject_init(&kobj, "test_obj");

    if (strcmp(kobj.name, "test_obj") != 0) fail("kobject_init: name incorrect");
    if (kobj.refcount != 1) fail("kobject_init: refcount should be 1");
    if (kobj.parent != NULL) fail("kobject_init: parent should be NULL");
    if (kobj.kset != NULL) fail("kobject_init: kset should be NULL");
}

static void test_kobject_init_name_truncation(void) {
    struct kobject kobj;
    const char *long_name = "this_is_a_very_long_name_that_exceeds_31_characters_limit";
    char expected_name[32];

    memset(&kobj, 0, sizeof(kobj));
    kobject_init(&kobj, long_name);

    // Expected behavior: first 31 chars copied, last char is null terminator
    strncpy(expected_name, long_name, 31);
    expected_name[31] = '\0';

    if (strcmp(kobj.name, expected_name) != 0) fail("kobject_init: name truncation failed");
    if (kobj.name[31] != '\0') fail("kobject_init: name not null terminated");
}

static void test_kobject_get(void) {
    struct kobject kobj;
    kobject_init(&kobj, "test_get");

    struct kobject *ret = kobject_get(&kobj);
    if (ret != &kobj) fail("kobject_get: return value incorrect");
    if (kobj.refcount != 2) fail("kobject_get: refcount incorrect");

    ret = kobject_get(NULL);
    if (ret != NULL) fail("kobject_get: NULL input should return NULL");
}

static void test_kobject_put_basic(void) {
    struct kobject kobj;
    kobject_init(&kobj, "test_put");

    // Increase refcount to 2
    kobject_get(&kobj);
    if (kobj.refcount != 2) fail("kobject_put_basic: setup failed");

    kobject_put(&kobj);
    if (kobj.refcount != 1) fail("kobject_put_basic: decrement failed");

    kobject_put(&kobj);
    if (kobj.refcount != 0) fail("kobject_put_basic: final decrement failed");

    // Ensure no crash on NULL
    kobject_put(NULL);
}

static void test_kobject_release_callback(void) {
    struct kobject kobj;
    release_called = 0;
    released_obj = NULL;

    kobject_init(&kobj, "test_release");
    kobj.release = my_release;

    kobject_get(&kobj);
    kobject_put(&kobj);
    if (release_called != 0) fail("test_kobject_release_callback: called prematurely");

    kobject_put(&kobj);
    if (release_called != 1) fail("test_kobject_release_callback: not called");
    if (released_obj != &kobj) fail("test_kobject_release_callback: wrong object");
}

static void test_kset_init(void) {
    struct kset kset;
    memset(&kset, 0xBB, sizeof(kset));

    kset_init(&kset, "test_kset");

    if (strcmp(kset.kobj.name, "test_kset") != 0) fail("kset_init: kobj name incorrect");
    if (kset.kobj.refcount != 1) fail("kset_init: kobj refcount incorrect");
    if (kset.list != NULL) fail("kset_init: list should be NULL");
    if (kset.count != 0) fail("kset_init: count should be 0");
    if (kset.kobj.parent != NULL) fail("kset_init: kobj parent should be NULL");
    if (kset.kobj.kset != NULL) fail("kset_init: kobj kset should be NULL");
    if (kset.kobj.release != NULL) fail("kset_init: kobj release should be NULL");
}

static void test_kset_init_name_truncation(void) {
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

void run_kobject_tests(void) {
    kprint("\n=== KOBJECT TESTS ===\n");
    failed_tests = 0;

    test_kobject_init();
    test_kobject_init_name_truncation();
    test_kobject_get();
    test_kobject_put_basic();
    test_kobject_release_callback();
    test_kset_init();
    test_kset_init_name_truncation();

    if (failed_tests == 0) {
        kprint("kobject: PASS\n");
    } else {
        char msg[64];
        snprintf(msg, sizeof(msg), "kobject: FAIL (%d tests failed)\n", failed_tests);
        kprint(msg);
    }
}
