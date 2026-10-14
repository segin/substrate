#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef __WCHAR_MIN__
#error "__WCHAR_MIN__ missing"
#endif
#ifndef __INTPTR_WIDTH__
#error "__INTPTR_WIDTH__ missing"
#endif
#ifndef __INTMAX_WIDTH__
#error "__INTMAX_WIDTH__ missing"
#endif
#ifndef __SIG_ATOMIC_WIDTH__
#error "__SIG_ATOMIC_WIDTH__ missing"
#endif
#ifndef __WINT_WIDTH__
#error "__WINT_WIDTH__ missing"
#endif
#if __INTPTR_WIDTH__ != (__SIZEOF_POINTER__ * __CHAR_BIT__)
#error "__INTPTR_WIDTH__ mismatch"
#endif
#if __INTMAX_WIDTH__ != 64
#error "__INTMAX_WIDTH__ mismatch"
#endif
#if __SIG_ATOMIC_WIDTH__ != 32
#error "__SIG_ATOMIC_WIDTH__ mismatch"
#endif
#if __WINT_WIDTH__ != 32
#error "__WINT_WIDTH__ mismatch"
#endif
#if __WCHAR_WIDTH__ != 32
#error "__WCHAR_WIDTH__ mismatch"
#endif
#if __WCHAR_MIN__ >= 0
#error "__WCHAR_MIN__ should be negative"
#endif

int main(void) {
    intmax_t s = -42;
    uintmax_t u = 42;
    uint64_t wide = UINT64_C(1) << 32;
    intptr_t ip = (intptr_t)(void *)&s;
    uintptr_t up = (uintptr_t)(void *)&u;
    int_fast16_t f16 = 7;
    uint_fast32_t f32 = 9;
    char buf[64];

    if (snprintf(buf, sizeof(buf), "%" PRIdMAX " %" PRIuMAX, s, u) < 0) {
        return 1;
    }
    if (strcmp(buf, "-42 42") != 0) {
        return 2;
    }
    if (sizeof(intmax_t) != 8 || sizeof(uintmax_t) != 8) {
        return 3;
    }
    if (sizeof(intptr_t) != sizeof(void *) || sizeof(uintptr_t) != sizeof(void *)) {
        return 4;
    }
    if (wide != 0x100000000ULL) {
        return 5;
    }
    if (f16 != 7 || f32 != 9 || ip == 0 || up == 0) {
        return 6;
    }
    return 0;
}
