#include <exec/perso/personality.h>
#include <exec/perso/linux/linux_syscalls.h>
#include <sys/syscall_impl.h>
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

void test_linux_personality(void) {
    struct personality *linux;
    char buf[96];

    tests_passed = 0;
    tests_failed = 0;

    kprint("=== Linux Personality Tests ===\n");

    linux = perso_lookup(PERS_LINUX);
    test_assert(linux != NULL, "Linux personality lookup succeeds");
    if (!linux) {
        sprintf(buf, "Linux personality tests: %d passed, %d failed\n",
                tests_passed, tests_failed);
        kprint(buf);
        return;
    }

    test_assert(linux->syscall_table[LINUX_SYS_modify_ldt] == (void *)&sys_modify_ldt,
                "Linux syscall table wires modify_ldt to sys_modify_ldt");
    test_assert(linux->syscall_names[LINUX_SYS_modify_ldt] != NULL &&
                    strcmp(linux->syscall_names[LINUX_SYS_modify_ldt], "modify_ldt") == 0,
                "Linux syscall name table exposes modify_ldt");
    test_assert(linux->syscall_fmts[LINUX_SYS_modify_ldt].nargs == 3,
                "Linux modify_ldt trace format has three arguments");
    test_assert(linux->syscall_fmts[LINUX_SYS_modify_ldt].arg_types[0] == ARG_INT &&
                    linux->syscall_fmts[LINUX_SYS_modify_ldt].arg_types[1] == ARG_PTR &&
                    linux->syscall_fmts[LINUX_SYS_modify_ldt].arg_types[2] == ARG_LONG,
                "Linux modify_ldt trace format matches ABI");

    sprintf(buf, "Linux personality tests: %d passed, %d failed\n",
            tests_passed, tests_failed);
    kprint(buf);
}
