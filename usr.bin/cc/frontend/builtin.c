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
    {"__builtin_va_arg_pack"},
    {"__builtin_va_arg_pack_len"},
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
    {"__builtin_counted_by_ref"},
    {"__builtin_return_address"},
    {"__builtin_frame_address"},
    {"__builtin_memcmp"},
    {"__builtin_prefetch"},
    {"__builtin_cpu_supports"},
    {"__builtin_strlen"},
    {"__builtin_strcmp"},
    {"__builtin_extract_return_addr"},
    {"__builtin_memcpy"},
    {"__builtin_memmove"},
    {"__builtin_memset"},
    {"__builtin___memcpy_chk"},
    {"__builtin___memmove_chk"},
    {"__builtin___memset_chk"},
    {"__builtin_huge_val"},
    {"__builtin_huge_valf"},
    {"__builtin_huge_vall"},
    {"__builtin_inf"},
    {"__builtin_inff"},
    {"__builtin_infl"},
    {"__builtin_ldbl_min"},
    {"__builtin_ldbl_denorm_min"},
    {"__builtin_ldbl_max"},
    {"__builtin_nanf"},
    {"__builtin_nan"},
    {"__builtin_nanl"},
    {"__builtin_nansf"},
    {"__builtin_nans"},
    {"__builtin_nansl"},
    {"__builtin_types_compatible_p"},
    {"__builtin_choose_expr"},
    {"__builtin_offsetof"},
};

/**
 * The C library function a builtin named after one calls, as gcc calls it where it does not expand the builtin
 * itself: "strncpy" for `__builtin_strncpy`.
 *
 * @param name  the called name
 * @return      the function's name (a suffix of @p name), or NULL if @p name is no such builtin
 */
const char *cc_builtin_library_function(const char *name) {
    static const char *const libc[] = {
        "memcmp", "memchr", "bcmp", "bzero", "strcmp", "strncmp", "strchr", "strrchr", "strstr", "strcpy",
        "strncpy", "strcat", "strncat", "strlen", "strnlen", "strspn", "strcspn", "strpbrk", "abort", "exit",
        "abs", "labs", "llabs", "malloc", "calloc", "realloc", "free", "printf", "sprintf", "snprintf",
        "vsnprintf", "puts", "putchar", "memcpy", "memmove", "memset", NULL};
    size_t i;

    if (name == NULL || strncmp(name, "__builtin_", 10) != 0) {
        return(NULL);
    }
    for (i = 0; libc[i] != NULL; ++i) {
        if (strcmp(name + 10, libc[i]) == 0) {
            return(name + 10);
        }
    }
    return(NULL);
}

int cc_builtin_is_recognized(const char *name) {
    size_t i;

    if (name == NULL || name[0] == '\0') {
        return(0);
    }
    if (cc_builtin_library_function(name) != NULL || cc_builtin_atomic_generic(name) != CC_AGEN_NONE) {
        return(1);
    }
    for (i = 0; i < sizeof(g_known_builtins) / sizeof(g_known_builtins[0]); ++i) {
        if (strcmp(g_known_builtins[i].name, name) == 0) {
            return(1);
        }
    }
    return(0);
}

/**
 * Whether @p name is an atomic read-modify-write builtin, and which.
 *
 * @param name  the called name
 * @param out   receives what the builtin does; may be NULL
 * @return      1 if it is one, else 0
 */
int cc_builtin_atomic_rmw(const char *name, cc_rmw_builtin_t *out) {
    static const struct {
        const char *name;
        cc_rmw_builtin_t rmw;
    } rmw[] = {
        {"__sync_fetch_and_add", {CC_RMW_ADD, 0, 0}},
        {"__sync_fetch_and_sub", {CC_RMW_SUB, 0, 0}},
        {"__sync_fetch_and_and", {CC_RMW_AND, 0, 0}},
        {"__sync_fetch_and_or", {CC_RMW_OR, 0, 0}},
        {"__sync_fetch_and_xor", {CC_RMW_XOR, 0, 0}},
        {"__sync_fetch_and_nand", {CC_RMW_NAND, 0, 0}},
        {"__sync_add_and_fetch", {CC_RMW_ADD, 1, 0}},
        {"__sync_sub_and_fetch", {CC_RMW_SUB, 1, 0}},
        {"__sync_and_and_fetch", {CC_RMW_AND, 1, 0}},
        {"__sync_or_and_fetch", {CC_RMW_OR, 1, 0}},
        {"__sync_xor_and_fetch", {CC_RMW_XOR, 1, 0}},
        {"__sync_nand_and_fetch", {CC_RMW_NAND, 1, 0}},
        {"__sync_lock_test_and_set", {CC_RMW_XCHG, 0, 0}},
        {"__atomic_fetch_add", {CC_RMW_ADD, 0, 1}},
        {"__atomic_fetch_sub", {CC_RMW_SUB, 0, 1}},
        {"__atomic_fetch_and", {CC_RMW_AND, 0, 1}},
        {"__atomic_fetch_or", {CC_RMW_OR, 0, 1}},
        {"__atomic_fetch_xor", {CC_RMW_XOR, 0, 1}},
        {"__atomic_fetch_nand", {CC_RMW_NAND, 0, 1}},
        {"__atomic_add_fetch", {CC_RMW_ADD, 1, 1}},
        {"__atomic_sub_fetch", {CC_RMW_SUB, 1, 1}},
        {"__atomic_and_fetch", {CC_RMW_AND, 1, 1}},
        {"__atomic_or_fetch", {CC_RMW_OR, 1, 1}},
        {"__atomic_xor_fetch", {CC_RMW_XOR, 1, 1}},
        {"__atomic_nand_fetch", {CC_RMW_NAND, 1, 1}},
        {"__atomic_exchange_n", {CC_RMW_XCHG, 0, 1}},
    };
    size_t i;

    if (name == NULL) {
        return(0);
    }
    for (i = 0; i < sizeof(rmw) / sizeof(rmw[0]); ++i) {
        if (strcmp(rmw[i].name, name) == 0) {
            if (out != NULL) {
                *out = rmw[i].rmw;
            }
            return(1);
        }
    }
    return(0);
}

/**
 * Which of gcc's generic atomic builtins @p name is, if any.
 *
 * @param name  the called name
 * @return      the builtin, or CC_AGEN_NONE
 */
cc_atomic_generic_t cc_builtin_atomic_generic(const char *name) {
    static const struct {
        const char *name;
        cc_atomic_generic_t which;
    } gen[] = {
        {"__atomic_load", CC_AGEN_LOAD},
        {"__atomic_store", CC_AGEN_STORE},
        {"__atomic_exchange", CC_AGEN_EXCHANGE},
        {"__atomic_compare_exchange", CC_AGEN_CAS},
        {"__atomic_compare_exchange_n", CC_AGEN_CAS_N},
        {"__atomic_test_and_set", CC_AGEN_TEST_AND_SET},
        {"__atomic_clear", CC_AGEN_CLEAR},
        {"__atomic_is_lock_free", CC_AGEN_IS_LOCK_FREE},
        {"__atomic_always_lock_free", CC_AGEN_ALWAYS_LOCK_FREE},
    };
    size_t i;

    for (i = 0; name != NULL && i < sizeof(gen) / sizeof(gen[0]); ++i) {
        if (strcmp(gen[i].name, name) == 0) {
            return(gen[i].which);
        }
    }
    return(CC_AGEN_NONE);
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
