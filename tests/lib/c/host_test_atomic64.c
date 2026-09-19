/*
 * host_test_atomic64.c
 *
 * Verifies substrate libc's 64-bit __atomic_* helpers from lib/c/src/atomic.c
 * by compiling that source directly into the test.  The i486 target has no
 * cmpxchg8b, so these are real function calls rather than inline instructions,
 * and they are implemented over a spinlock pool -- this checks the values they
 * produce, including the fetch_* return-the-old-value contract and
 * compare_exchange's write-back of the observed value on failure.
 *
 *     make -C tests host_test_atomic64 && tests/host_test_atomic64
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../../../lib/c/src/atomic.c"

static int fails;

#define CHECK(cond, what) do { \
    if (cond) printf("  ok   %s\n", what); \
    else { printf("  FAIL %s\n", what); fails++; } \
} while (0)

int main(void)
{
    uint64_t v = 0;

    __atomic_store_8(&v, 0x1122334455667788ULL, __ATOMIC_SEQ_CST);
    CHECK(v == 0x1122334455667788ULL, "store_8 writes all 64 bits");
    CHECK(__atomic_load_8(&v, __ATOMIC_SEQ_CST) == 0x1122334455667788ULL, "load_8 reads them back");

    CHECK(__atomic_exchange_8(&v, 1, __ATOMIC_SEQ_CST) == 0x1122334455667788ULL, "exchange_8 returns the old value");
    CHECK(v == 1, "exchange_8 stores the new value");

    v = 10;
    CHECK(__atomic_fetch_add_8(&v, 5, __ATOMIC_SEQ_CST) == 10 && v == 15, "fetch_add_8 returns old, adds");
    CHECK(__atomic_fetch_sub_8(&v, 3, __ATOMIC_SEQ_CST) == 15 && v == 12, "fetch_sub_8 returns old, subtracts");
    v = 0xF0F0F0F0F0F0F0F0ULL;
    CHECK(__atomic_fetch_and_8(&v, 0xFF00FF00FF00FF00ULL, __ATOMIC_SEQ_CST) == 0xF0F0F0F0F0F0F0F0ULL
          && v == 0xF000F000F000F000ULL, "fetch_and_8");
    CHECK(__atomic_fetch_or_8(&v, 0x0000000000000FFFULL, __ATOMIC_SEQ_CST) == 0xF000F000F000F000ULL
          && v == 0xF000F000F000FFFFULL, "fetch_or_8");
    CHECK(__atomic_fetch_xor_8(&v, 0xFFFFFFFFFFFFFFFFULL, __ATOMIC_SEQ_CST) == 0xF000F000F000FFFFULL
          && v == 0x0FFF0FFF0FFF0000ULL, "fetch_xor_8");

    v = 42;
    uint64_t expected = 42;
    CHECK(__atomic_compare_exchange_8(&v, &expected, 99, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
          && v == 99, "compare_exchange_8 succeeds when the value matches");
    expected = 7;
    CHECK(!__atomic_compare_exchange_8(&v, &expected, 123, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)
          && v == 99 && expected == 99, "compare_exchange_8 fails and reports the observed value");

    /* Called through a pointer: with a constant size gcc folds
     * __atomic_is_lock_free at compile time -- and the host test builds for
     * i686, which has cmpxchg8b -- so a direct call would test the compiler,
     * not libc's function. */
    bool (*is_lock_free)(size_t, const volatile void *) = __atomic_is_lock_free;
    uint32_t w = 0; uint8_t b = 0;
    CHECK(is_lock_free(1, &b), "is_lock_free: 1-byte is lock-free");
    CHECK(is_lock_free(4, &w), "is_lock_free: aligned 4-byte is lock-free");
    CHECK(!is_lock_free(8, &v), "is_lock_free: 8-byte is not (lock pool)");

    printf("host_test_atomic64: %s\n", fails ? "FAIL" : "PASS");
    return fails != 0;
}
