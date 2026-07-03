#include <exec/perso/personality.h>
#include <exec/perso/freebsd/freebsd_syscalls.h>
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

void test_freebsd_personality(void) {
    struct personality *freebsd;
    char buf[96];

    tests_passed = 0;
    tests_failed = 0;

    kprint("=== FreeBSD Personality Tests ===\n");

    freebsd = perso_lookup(PERS_FREEBSD);
    test_assert(freebsd != NULL, "FreeBSD personality lookup succeeds");
    if (!freebsd) {
        snprintf(buf, sizeof(buf), "FreeBSD personality tests: %d passed, %d failed\n",
                tests_passed, tests_failed);
        kprint(buf);
        return;
    }

    test_assert(freebsd->syscall_table[FREEBSD_SYS_getrandom] == (void *)&sys_getrandom,
                "FreeBSD syscall table wires getrandom to sys_getrandom");
    test_assert(freebsd->syscall_names[FREEBSD_SYS_getrandom] != NULL &&
                    strcmp(freebsd->syscall_names[FREEBSD_SYS_getrandom], "getrandom") == 0,
                "FreeBSD syscall name table exposes getrandom");
    test_assert(freebsd->syscall_fmts[FREEBSD_SYS_getrandom].nargs == 3 &&
                    freebsd->syscall_fmts[FREEBSD_SYS_getrandom].arg_types[0] == ARG_PTR &&
                    freebsd->syscall_fmts[FREEBSD_SYS_getrandom].arg_types[1] == ARG_INT &&
                    freebsd->syscall_fmts[FREEBSD_SYS_getrandom].arg_types[2] == ARG_HEX,
                "FreeBSD getrandom trace format matches ABI");

    snprintf(buf, sizeof(buf), "FreeBSD personality tests: %d passed, %d failed\n",
            tests_passed, tests_failed);
    kprint(buf);
}