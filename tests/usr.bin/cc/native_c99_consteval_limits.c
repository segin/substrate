#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define TYPE_MINIMUM(t) ((t)((t)0 < (t)-1 ? (t)0 : ~TYPE_MAXIMUM(t)))
#define TYPE_MAXIMUM(t) ((t)((t)0 < (t)-1 ? (t)-1 : ((((t)1 << (sizeof(t) * CHAR_BIT - 2)) - 1) * 2 + 1)))

struct limit_checks {
    int check_ptrdiff:
        PTRDIFF_MIN == TYPE_MINIMUM(ptrdiff_t) &&
        PTRDIFF_MAX == TYPE_MAXIMUM(ptrdiff_t) ? 1 : -1;
    int check_sig_atomic:
        SIG_ATOMIC_MIN == TYPE_MINIMUM(sig_atomic_t) &&
        SIG_ATOMIC_MAX == TYPE_MAXIMUM(sig_atomic_t) ? 1 : -1;
    int check_size:
        SIZE_MAX == TYPE_MAXIMUM(size_t) ? 1 : -1;
    int check_wchar:
        WCHAR_MIN == TYPE_MINIMUM(wchar_t) &&
        WCHAR_MAX == TYPE_MAXIMUM(wchar_t) ? 1 : -1;
    int check_wint:
        WINT_MIN == TYPE_MINIMUM(wint_t) &&
        WINT_MAX == TYPE_MAXIMUM(wint_t) ? 1 : -1;
    int check_uint8_c:
        ((-1 < UINT8_C(0)) == (-1 < (uint_least8_t)0)) ? 1 : -1;
    int check_uint16_c:
        ((-1 < UINT16_C(0)) == (-1 < (uint_least16_t)0)) ? 1 : -1;
};

int main(void) {
    return sizeof(struct limit_checks) > 0 ? 0 : 1;
}
