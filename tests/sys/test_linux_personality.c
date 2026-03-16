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
    test_assert(linux->syscall_table[LINUX_SYS_mount] == (void *)&sys_mount,
                "Linux syscall table wires mount to sys_mount");
    test_assert(linux->syscall_table[LINUX_SYS_umount] == (void *)&sys_umount,
                "Linux syscall table wires umount to sys_umount");
    test_assert(linux->syscall_table[LINUX_SYS_dup] == (void *)&sys_dup,
                "Linux syscall table wires dup to sys_dup");
    test_assert(linux->syscall_names[LINUX_SYS_modify_ldt] != NULL &&
                    strcmp(linux->syscall_names[LINUX_SYS_modify_ldt], "modify_ldt") == 0,
                "Linux syscall name table exposes modify_ldt");
    test_assert(linux->syscall_names[LINUX_SYS_mount] != NULL &&
                    strcmp(linux->syscall_names[LINUX_SYS_mount], "mount") == 0,
                "Linux syscall name table exposes mount");
    test_assert(linux->syscall_names[LINUX_SYS_umount] != NULL &&
                    strcmp(linux->syscall_names[LINUX_SYS_umount], "umount") == 0,
                "Linux syscall name table exposes umount");
    test_assert(linux->syscall_fmts[LINUX_SYS_modify_ldt].nargs == 3,
                "Linux modify_ldt trace format has three arguments");
    test_assert(linux->syscall_fmts[LINUX_SYS_mount].nargs == 5,
                "Linux mount trace format has five arguments");
    test_assert(linux->syscall_fmts[LINUX_SYS_mount].arg_types[0] == ARG_STR &&
                    linux->syscall_fmts[LINUX_SYS_mount].arg_types[1] == ARG_STR &&
                    linux->syscall_fmts[LINUX_SYS_mount].arg_types[2] == ARG_STR &&
                    linux->syscall_fmts[LINUX_SYS_mount].arg_types[3] == ARG_HEX &&
                    linux->syscall_fmts[LINUX_SYS_mount].arg_types[4] == ARG_PTR,
                "Linux mount trace format matches ABI");
    test_assert(linux->syscall_fmts[LINUX_SYS_umount].nargs == 1 &&
                    linux->syscall_fmts[LINUX_SYS_umount].arg_types[0] == ARG_STR,
                "Linux umount trace format matches ABI");
    test_assert(linux->syscall_fmts[LINUX_SYS_modify_ldt].arg_types[0] == ARG_INT &&
                    linux->syscall_fmts[LINUX_SYS_modify_ldt].arg_types[1] == ARG_PTR &&
                    linux->syscall_fmts[LINUX_SYS_modify_ldt].arg_types[2] == ARG_LONG,
                "Linux modify_ldt trace format matches ABI");

    sprintf(buf, "Linux personality tests: %d passed, %d failed\n",
            tests_passed, tests_failed);
    kprint(buf);
}
