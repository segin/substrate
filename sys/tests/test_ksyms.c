/*
 * test_ksyms.c - Unit tests for kernel symbol resolution
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "../kern/console.h"
#include "../kern/ksyms.h"

/* Test result tracking */
static int tests_passed = 0;
static int tests_failed = 0;

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

/*
 * test_ksym_lookup_known - Test lookup of known symbol
 */
static void test_ksym_lookup_known(void) {
    const struct ksym *sym = ksym_lookup(0xC0100000);
    test_assert(sym != NULL, "ksym_lookup finds _start at 0xC0100000");
    if (sym) {
        test_assert(strcmp(sym->name, "_start") == 0, "Symbol name is _start");
    }
}

/*
 * test_ksym_lookup_offset - Test lookup with offset into function
 */
static void test_ksym_lookup_offset(void) {
    /* Address within kmain (0xC0100020 + some offset) */
    const struct ksym *sym = ksym_lookup(0xC0100025);
    test_assert(sym != NULL, "ksym_lookup finds symbol for offset address");
    if (sym) {
        test_assert(strcmp(sym->name, "kmain") == 0, "Offset address maps to kmain");
    }
}

/*
 * test_ksym_resolve - Test full resolution with offset
 */
static void test_ksym_resolve_basic(void) {
    char buf[64];
    
    /* Exact address */
    ksym_resolve(0xC0100020, buf, sizeof(buf));
    test_assert(strcmp(buf, "kmain") == 0, "Exact address resolves to kmain");
    
    /* Address with offset */
    ksym_resolve(0xC0100025, buf, sizeof(buf));
    test_assert(strncmp(buf, "kmain+0x5", 9) == 0, "Offset address resolves to kmain+0x5");
}

/*
 * test_ksym_unknown - Test lookup of unknown address
 */
static void test_ksym_unknown(void) {
    char buf[64];
    
    /* Address far from any known symbol */
    ksym_resolve(0xDEADBEEF, buf, sizeof(buf));
    test_assert(strncmp(buf, "0x", 2) == 0, "Unknown address returns hex string");
}

/*
 * test_ksyms - Main test entry point
 */
void test_ksyms(void) {
    kprint("=== Kernel Symbol Tests ===\n");
    
    tests_passed = 0;
    tests_failed = 0;
    
    ksym_init();
    
    test_ksym_lookup_known();
    test_ksym_lookup_offset();
    test_ksym_resolve_basic();
    test_ksym_unknown();
    
    char buf[64];
    sprintf(buf, "\nKernel symbol tests: %d passed, %d failed\n", tests_passed, tests_failed);
    kprint(buf);
}
