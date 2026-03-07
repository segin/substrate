#include "cc_frontend.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    const char *name;
} builtin_name_t;

static const builtin_name_t g_known_builtins[] = {
    {"__builtin_va_start"},
    {"__builtin_c23_va_start"},
    {"__builtin_va_end"},
    {"__builtin_c23_va_end"},
    {"__builtin_va_copy"},
    {"__builtin_c23_va_copy"},
    {"__builtin_va_arg"},
    {"__builtin_c23_va_arg"},
    {"__builtin_alloca"},
    {"__builtin_expect"},
    {"__builtin_constant_p"},
    {"__builtin_trap"},
    {"__builtin_unreachable"},
    {"__builtin_assume"},
    {"__builtin_assume_aligned"},
    {"__builtin_unpredictable"},
    {"__builtin_clz"},
    {"__builtin_clzl"},
    {"__builtin_clzll"},
    {"__builtin_ctz"},
    {"__builtin_ctzl"},
    {"__builtin_ctzll"},
    {"__builtin_ffs"},
    {"__builtin_ffsl"},
    {"__builtin_ffsll"},
    {"__builtin_popcount"},
    {"__builtin_popcountl"},
    {"__builtin_popcountll"},
    {"__builtin_bswap16"},
    {"__builtin_bswap32"},
    {"__builtin_bswap64"},
    {"__builtin_add_overflow"},
    {"__builtin_sub_overflow"},
    {"__builtin_mul_overflow"},
    {"__builtin_add_overflow_p"},
    {"__builtin_sub_overflow_p"},
    {"__builtin_mul_overflow_p"},
    {"__builtin_object_size"},
    {"__builtin_return_address"},
    {"__builtin_frame_address"},
    {"__builtin_memcmp"},
    {"__builtin_prefetch"},
    {"__builtin_strlen"},
    {"__builtin_memcpy"},
    {"__builtin_memmove"},
    {"__builtin_memset"},
    {"__builtin___memcpy_chk"},
    {"__builtin___memmove_chk"},
    {"__builtin___memset_chk"},
    {"__builtin_huge_val"},
    {"__builtin_huge_valf"},
    {"__builtin_huge_vall"},
    {"__builtin_nanf"},
    {"__builtin_nan"},
    {"__builtin_nanl"},
    {"__builtin_types_compatible_p"},
    {"__builtin_choose_expr"},
    {"__builtin_offsetof"},
};

int cc_builtin_is_recognized(const char *name) {
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return(0);
    }
    for (i = 0; i < sizeof(g_known_builtins) / sizeof(g_known_builtins[0]); ++i) {
        if (strcmp(g_known_builtins[i].name, name) == 0) {
            return(1);
        }
    }
    return(0);
}

int cc_builtin_bswap_bits(const char *name) {
    if (name == NULL) {
        return(0);
    }
    if (strcmp(name, "__builtin_bswap16") == 0) {
        return(16);
    }
    if (strcmp(name, "__builtin_bswap32") == 0) {
        return(32);
    }
    if (strcmp(name, "__builtin_bswap64") == 0) {
        return(64);
    }
    return(0);
}
