/*
 * target.c: the data layout of each machine cc writes code for.
 *
 * The numbers are the System V psABI's, checked against gcc: the
 * x86-64 psABI aligns `long double` and `_Decimal128` to 16, while
 * the i386 psABI aligns every 8-byte scalar except `_Decimal64` to 4.
 */

#include "cc_target.h"

#include <stddef.h>

static const char *const g_x86_64_macros[] = {"__x86_64__", "__LP64__", "_LP64", NULL};
static const char *const g_i386_macros[] = {"__i386__", "__ILP32__", "_ILP32", NULL};

/* The typedefs' types are gcc's: LP64 on x86-64, ILP32 (with a long wchar_t) on i386. */
static const cc_target_desc_t g_x86_64 = {
    CC_TARGET_X86_64,
    "x86_64",
    8,
    {8, 8},   /* long long */
    {8, 8},   /* double */
    {16, 16}, /* long double */
    {8, 8},   /* _Decimal64 */
    {16, 16}, /* _Decimal128 */
    CC_TGT_ULONG, /* size_t */
    CC_TGT_LONG,  /* ptrdiff_t */
    CC_TGT_INT,   /* wchar_t */
    CC_TGT_LONG,  /* int64_t */
    CC_TGT_LONG,  /* int_fast16_t */
    1,            /* __int128 */
    1,            /* SSE floating point */
    g_x86_64_macros,
};

static const cc_target_desc_t g_i386 = {
    CC_TARGET_I386,
    "i386",
    4,
    {8, 4},   /* long long */
    {8, 4},   /* double */
    {12, 4},  /* long double */
    {8, 8},   /* _Decimal64 */
    {16, 16}, /* _Decimal128 */
    CC_TGT_UINT,  /* size_t */
    CC_TGT_INT,   /* ptrdiff_t */
    CC_TGT_LONG,  /* wchar_t */
    CC_TGT_LLONG, /* int64_t */
    CC_TGT_INT,   /* int_fast16_t */
    0,            /* no __int128 */
    0,            /* x87 unless -mfpmath=sse */
    g_i386_macros,
};

int cc_target_int_size(const cc_target_desc_t *tgt, cc_target_int_t t) {
    switch (t) {
    case CC_TGT_LONG:
    case CC_TGT_ULONG:
        return tgt->pointer_size;
    case CC_TGT_LLONG:
    case CC_TGT_ULLONG:
        return tgt->int64.size;
    default:
        return 4;
    }
}

static const cc_target_desc_t *g_current = &g_x86_64;

/**
 * Return the description of a target.
 *
 * @param id  the target
 * @return    its description; never NULL (an unknown id gives x86-64)
 */
const cc_target_desc_t *cc_target_get(cc_target_t id) {
    return id == CC_TARGET_I386 ? &g_i386 : &g_x86_64;
}

/**
 * Make @p id the target the compilation in progress is for.
 *
 * @param id  the target to compile for
 */
void cc_target_select(cc_target_t id) {
    g_current = cc_target_get(id);
}

/**
 * Return the target the compilation in progress is for.
 *
 * @return the selected target's description; never NULL
 */
const cc_target_desc_t *cc_target_current(void) {
    return g_current;
}

/**
 * The size of a pointer on the target being compiled for.
 *
 * @return 4 or 8
 */
int cc_target_pointer_size(void) {
    return g_current->pointer_size;
}
