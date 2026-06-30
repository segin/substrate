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
        snprintf(buf, sizeof(buf), "Native personality tests: %d passed, %d failed\n",
                tests_passed, tests_failed);
        kprint(buf);
        return;
    }

    test_assert(native->syscall_table[SYS_DUP] == (void *)&sys_dup,
                "Native syscall table wires dup to sys_dup");
    test_assert(native->syscall_table[SYS_CHDIR] == (void *)&sys_chdir,
                "Native syscall table wires chdir to sys_chdir");
    test_assert(native->syscall_table[SYS_CHMOD] == (void *)&sys_chmod,
                "Native syscall table wires chmod to sys_chmod");
    test_assert(native->syscall_table[SYS_MMAP] != NULL,
                "Native syscall table wires mmap");
    test_assert(native->syscall_table[SYS_MUNMAP] == (void *)&sys_munmap,
                "Native syscall table wires munmap to sys_munmap");
    test_assert(native->syscall_table[SYS_GETRANDOM] == (void *)&sys_getrandom,
                "Native syscall table wires getrandom to sys_getrandom");
    test_assert(native->syscall_names[SYS_DUP] != NULL &&
                    strcmp(native->syscall_names[SYS_DUP], "dup") == 0,
                "Native syscall name table exposes dup");
    test_assert(native->syscall_names[SYS_CHDIR] != NULL &&
                    strcmp(native->syscall_names[SYS_CHDIR], "chdir") == 0,
                "Native syscall name table exposes chdir");
    test_assert(native->syscall_names[SYS_CHMOD] != NULL &&
                    strcmp(native->syscall_names[SYS_CHMOD], "chmod") == 0,
                "Native syscall name table exposes chmod");
    test_assert(native->syscall_names[SYS_MMAP] != NULL &&
                    strcmp(native->syscall_names[SYS_MMAP], "mmap") == 0,
                "Native syscall name table exposes mmap");
    test_assert(native->syscall_names[SYS_GETRANDOM] != NULL &&
                    strcmp(native->syscall_names[SYS_GETRANDOM], "getrandom") == 0,
                "Native syscall name table exposes getrandom");
    test_assert(native->syscall_fmts[SYS_DUP].nargs == 1 &&
                    native->syscall_fmts[SYS_DUP].arg_types[0] == ARG_INT,
                "Native dup trace format matches ABI");
    test_assert(native->syscall_fmts[SYS_CHDIR].nargs == 1 &&
                    native->syscall_fmts[SYS_CHDIR].arg_types[0] == ARG_STR,
                "Native chdir trace format matches ABI");
    test_assert(native->syscall_fmts[SYS_CHMOD].nargs == 2 &&
                    native->syscall_fmts[SYS_CHMOD].arg_types[0] == ARG_STR &&
                    native->syscall_fmts[SYS_CHMOD].arg_types[1] == ARG_HEX,
                "Native chmod trace format matches ABI");
    test_assert(native->syscall_fmts[SYS_MMAP].nargs == 6 &&
                    native->syscall_fmts[SYS_MMAP].arg_types[0] == ARG_PTR &&
                    native->syscall_fmts[SYS_MMAP].arg_types[1] == ARG_INT &&
                    native->syscall_fmts[SYS_MMAP].arg_types[2] == ARG_INT &&
                    native->syscall_fmts[SYS_MMAP].arg_types[3] == ARG_INT &&
                    native->syscall_fmts[SYS_MMAP].arg_types[4] == ARG_INT &&
                    native->syscall_fmts[SYS_MMAP].arg_types[5] == ARG_HEX,
                "Native mmap trace format matches ABI");
    test_assert(native->syscall_fmts[SYS_GETRANDOM].nargs == 3 &&
                    native->syscall_fmts[SYS_GETRANDOM].arg_types[0] == ARG_PTR &&
                    native->syscall_fmts[SYS_GETRANDOM].arg_types[1] == ARG_INT &&
                    native->syscall_fmts[SYS_GETRANDOM].arg_types[2] == ARG_HEX,
                "Native getrandom trace format matches ABI");

    snprintf(buf, sizeof(buf), "Native personality tests: %d passed, %d failed\n",
            tests_passed, tests_failed);
    kprint(buf);
}
