#include <exec/perso/personality.h>
#include <sys/syscall_impl.h>
#include <arch/i386/syscall.h>
#include <kern/console.h>
#include <stdio.h>
#include <string.h>

static int tests_passed;
static int tests_failed;

static void test_assert(int condition, const char *name) {
    if (condition) {
        tests_passed++;
    } else {
        tests_failed++;
        kprint("FAIL: ");
        kprint(name);
        kprint("\n");
    }
}

void test_native_personality(void) {
    struct personality *native;
    char buf[96];

    tests_passed = 0;
    tests_failed = 0;

    kprint("=== Native Personality Tests ===\n");

    native = perso_lookup(PERS_NATIVE);
    test_assert(native != NULL, "Native personality lookup succeeds");
    if (!native) {
        sprintf(buf, "Native personality tests: %d passed, %d failed\n",
                tests_passed, tests_failed);
        kprint(buf);
        return;
    }

    test_assert(native->syscall_table[SYS_DUP] == (void *)&sys_dup,
                "Native syscall table wires dup to sys_dup");
    test_assert(native->syscall_names[SYS_DUP] != NULL &&
                    strcmp(native->syscall_names[SYS_DUP], "dup") == 0,
                "Native syscall name table exposes dup");
    test_assert(native->syscall_fmts[SYS_DUP].nargs == 1 &&
                    native->syscall_fmts[SYS_DUP].arg_types[0] == ARG_INT,
                "Native dup trace format matches ABI");

    sprintf(buf, "Native personality tests: %d passed, %d failed\n",
            tests_passed, tests_failed);
    kprint(buf);
}