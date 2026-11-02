/*
 * target.c: the data layout of each machine cc writes code for.
 *
 * The numbers are the System V psABI's, checked against gcc: the
 * x86-64 psABI aligns `long double` and `_Decimal128` to 16, while
 * the i386 psABI aligns every 8-byte scalar except `_Decimal64` to 4.
 */

#include "cc_target.h"
#include "cc_abi.h"
#include "cc_middle.h"

#include <stddef.h>
#include <string.h>

static const char *const g_x86_64_macros[] = {"__x86_64__", "__x86_64", "__amd64__", "__amd64",
                                              "__LP64__",   "_LP64",    NULL};
static const char *const g_i386_macros[] = {"__i386__", "__i386", "__ILP32__", "_ILP32", NULL};

/* movabs $env, %r10; movabs $fn, %r11; jmp *%r11 */
static const cc_target_tramp_store_t g_x86_64_tramp[] = {
    {CC_TRAMP_CONST, 0, 2, 0xba49}, {CC_TRAMP_ENV, 2, 8, 0},     {CC_TRAMP_CONST, 10, 2, 0xbb49},
    {CC_TRAMP_FN, 12, 8, 0},        {CC_TRAMP_CONST, 20, 2, 0xff41}, {CC_TRAMP_CONST, 22, 1, 0xe3},
    {CC_TRAMP_END, 0, 0, 0},
};

/* movl $env, %ecx; movl $fn, %eax; jmp *%eax */
static const cc_target_tramp_store_t g_i386_tramp[] = {
    {CC_TRAMP_CONST, 0, 1, 0xb9}, {CC_TRAMP_ENV, 1, 4, 0},          {CC_TRAMP_CONST, 5, 1, 0xb8},
    {CC_TRAMP_FN, 6, 4, 0},       {CC_TRAMP_CONST, 10, 2, 0xe0ff}, {CC_TRAMP_END, 0, 0, 0},
};

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
    "-m64",
    "--64",
    "elf_x86_64",
    "x86_64-pc-linux-gnu",
    "/lib64/ld-linux-x86-64.so.2",
    1, /* PIE */
    "lock; orl $0, (%%rsp)",
    24, /* {gp_offset, fp_offset, overflow_arg_area, reg_save_area} */
    g_x86_64_tramp,
    CC_ABI_RET_X87 | CC_ABI_RET_X87_COMPLEX | CC_ABI_RET_MEMORY,
    cc_abi_x86_64_aggregate_arg,
    cc_abi_x86_64_aggregate_return,
    NULL, /* every integer is a register's width */
    cc_abi_x86_64_va_arg,
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
    "-m32",
    "--32",
    "elf_i386",
    "i386-pc-linux-gnu",
    "/lib/ld-linux.so.2",
    0, /* absolute addressing: not a PIE */
    "lock; orl $0, (%%esp)",
    0, /* a va_list is the next argument's address */
    g_i386_tramp,
    /* a vector returned in %xmm0 or %mm0 too */
    CC_ABI_RET_X87 | CC_ABI_RET_X87_COMPLEX | CC_ABI_RET_MEMORY | CC_ABI_RET_VEC128 | CC_ABI_RET_VEC64,
    cc_abi_i386_aggregate_arg,
    cc_abi_i386_aggregate_return,
    cc_legalize_i386_function, /* 64-bit integers as two words */
    cc_abi_i386_va_arg,
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

/* ---- -march, -mtune and the instruction-set extensions, as gcc has them ---- */

#define ISA_X86_64 (CC_ISA_MMX | CC_ISA_SSE | CC_ISA_SSE2 | CC_ISA_FXSR)
#define ISA_V2 (ISA_X86_64 | CC_ISA_SSE3 | CC_ISA_SSSE3 | CC_ISA_SSE4_1 | CC_ISA_SSE4_2 | CC_ISA_POPCNT | \
                CC_ISA_CRC32 | CC_ISA_SAHF | CC_ISA_CX16)
#define ISA_V3 (ISA_V2 | CC_ISA_AVX | CC_ISA_AVX2 | CC_ISA_BMI | CC_ISA_BMI2 | CC_ISA_F16C | CC_ISA_FMA | \
                CC_ISA_LZCNT | CC_ISA_MOVBE | CC_ISA_XSAVE)
#define ISA_V4 (ISA_V3 | CC_ISA_AVX512F | CC_ISA_AVX512BW | CC_ISA_AVX512CD | CC_ISA_AVX512DQ | CC_ISA_AVX512VL)
#define ISA_P4 (CC_ISA_MMX | CC_ISA_SSE | CC_ISA_SSE2 | CC_ISA_FXSR)
#define ISA_NOCONA (ISA_P4 | CC_ISA_SSE3 | CC_ISA_CX16)
#define ISA_CORE2 (ISA_NOCONA | CC_ISA_SSSE3 | CC_ISA_SAHF)
#define ISA_NEHALEM (ISA_CORE2 | CC_ISA_SSE4_1 | CC_ISA_SSE4_2 | CC_ISA_POPCNT | CC_ISA_CRC32)

static const char *const m_none[] = {NULL};
static const char *const m_i486[] = {"__i486", "__i486__", NULL};
/* (an i586's __pentium_mmx__ only with MMX, -march=pentium -mmmx too, as gcc's; its tuning's by name) */
static const char *const m_i586[] = {"__i586", "__i586__", "__pentium", "__pentium__", "__pentium_mmx__", NULL};
static const char *const m_i686[] = {"__i686", "__i686__", "__pentiumpro", "__pentiumpro__", NULL};
static const char *const m_p4[] = {"__pentium4", "__pentium4__", NULL};
static const char *const m_nocona[] = {"__nocona", "__nocona__", NULL};
static const char *const m_core2[] = {"__core2", "__core2__", NULL};
static const char *const m_nehalem[] = {"__corei7", "__corei7__", "__nehalem", "__nehalem__", NULL};
static const char *const m_k8[] = {"__k8", "__k8__", NULL};

static const char *const t_i386[] = {"__tune_i386__", NULL};
static const char *const t_i486[] = {"__tune_i486__", NULL};
static const char *const t_i586[] = {"__tune_i586__", "__tune_pentium__", NULL};
static const char *const t_pmmx[] = {"__tune_i586__", "__tune_pentium__", "__tune_pentium_mmx__", NULL};
static const char *const t_i686[] = {"__tune_i686__", "__tune_pentiumpro__", NULL};
static const char *const t_p2[] = {"__tune_i686__", "__tune_pentiumpro__", "__tune_pentium2__", NULL};
static const char *const t_p3[] = {"__tune_i686__", "__tune_pentiumpro__", "__tune_pentium2__", "__tune_pentium3__",
                                   NULL};
static const char *const t_p4[] = {"__tune_pentium4__", NULL};
static const char *const t_nocona[] = {"__tune_nocona__", NULL};
static const char *const t_core2[] = {"__tune_core2__", NULL};
static const char *const t_nehalem[] = {"__tune_corei7__", "__tune_nehalem__", NULL};

/* gcc's -march names cc knows; the x86-64 levels and "generic" tune for no machine in particular */
static const cc_arch_t g_arches[] = {
    {"i386", 3, 0, m_none, t_i386},
    {"i486", 4, 0, m_i486, t_i486},
    {"i586", 5, 0, m_i586, t_i586},
    {"pentium", 5, 0, m_i586, t_i586},
    {"pentium-mmx", 5, CC_ISA_MMX, m_i586, t_pmmx},
    {"pentiumpro", 6, 0, m_i686, t_i686},
    {"i686", 6, 0, m_i686, t_i686},
    {"pentium2", 6, CC_ISA_MMX | CC_ISA_FXSR, m_i686, t_p2},
    {"pentium3", 6, CC_ISA_MMX | CC_ISA_FXSR | CC_ISA_SSE, m_i686, t_p3},
    {"pentium3m", 6, CC_ISA_MMX | CC_ISA_FXSR | CC_ISA_SSE, m_i686, t_p3},
    {"pentium-m", 6, ISA_P4, m_i686, t_i686},
    {"pentium4", 7, ISA_P4, m_p4, t_p4},
    {"pentium4m", 7, ISA_P4, m_p4, t_p4},
    {"prescott", 7, ISA_P4 | CC_ISA_SSE3, m_nocona, t_nocona},
    {"nocona", 7, ISA_NOCONA, m_nocona, t_nocona},
    {"core2", 7, ISA_CORE2, m_core2, t_core2},
    {"nehalem", 7, ISA_NEHALEM, m_nehalem, t_nehalem},
    {"corei7", 7, ISA_NEHALEM, m_nehalem, t_nehalem},
    {"x86-64", 7, ISA_X86_64, m_k8, m_none},
    {"x86-64-v2", 7, ISA_V2, m_k8, m_none},
    {"x86-64-v3", 7, ISA_V3, m_k8, m_none},
    {"x86-64-v4", 7, ISA_V4, m_k8, m_none},
    {"generic", 7, ISA_X86_64, m_k8, m_none},
};

const cc_arch_t *cc_arch_find(const char *name) {
    size_t k;

    for (k = 0; name != NULL && k < sizeof(g_arches) / sizeof(g_arches[0]); ++k) {
        if (strcmp(name, g_arches[k].name) == 0) {
            return &g_arches[k];
        }
    }
    return NULL;
}

/* what turning each on turns on (gcc's OPTION_MASK_ISA_*_SET; MMX, POPCNT and CRC32 come after, cc_isa_from_flags()) */
#define SET_SSE CC_ISA_SSE
#define SET_SSE2 (CC_ISA_SSE2 | SET_SSE)
#define SET_SSE3 (CC_ISA_SSE3 | SET_SSE2)
#define SET_SSSE3 (CC_ISA_SSSE3 | SET_SSE3)
#define SET_SSE4_1 (CC_ISA_SSE4_1 | SET_SSSE3)
#define SET_SSE4_2 (CC_ISA_SSE4_2 | SET_SSE4_1)
#define SET_AVX (CC_ISA_AVX | SET_SSE4_2 | CC_ISA_XSAVE)
#define SET_AVX2 (CC_ISA_AVX2 | SET_AVX)
#define SET_AVX512F (CC_ISA_AVX512F | SET_AVX2)
/* and what turning each off turns off (OPTION_MASK_ISA_*_UNSET) */
#define UNSET_AVX512F (CC_ISA_AVX512F | CC_ISA_AVX512BW | CC_ISA_AVX512CD | CC_ISA_AVX512DQ | CC_ISA_AVX512VL)
#define UNSET_AVX2 (CC_ISA_AVX2 | UNSET_AVX512F)
#define UNSET_AVX (CC_ISA_AVX | UNSET_AVX2 | CC_ISA_FMA | CC_ISA_F16C)
#define UNSET_SSE4_2 (CC_ISA_SSE4_2 | UNSET_AVX | CC_ISA_CRC32)
#define UNSET_SSE4_1 (CC_ISA_SSE4_1 | UNSET_SSE4_2)
#define UNSET_SSSE3 (CC_ISA_SSSE3 | UNSET_SSE4_1)
#define UNSET_SSE3 (CC_ISA_SSE3 | UNSET_SSSE3 | CC_ISA_SSE4A)
#define UNSET_SSE2 (CC_ISA_SSE2 | UNSET_SSE3)
#define UNSET_SSE (CC_ISA_SSE | UNSET_SSE2)

/** One -m<name>: what it turns on, and what -mno-<name> turns off. */
typedef struct {
    const char *name;
    unsigned set;
    unsigned unset;
} isa_flag_t;

static const isa_flag_t g_isa_flags[] = {
    {"mmx", CC_ISA_MMX, CC_ISA_MMX},
    {"sse", SET_SSE, UNSET_SSE},
    {"sse2", SET_SSE2, UNSET_SSE2},
    {"sse3", SET_SSE3, UNSET_SSE3},
    {"ssse3", SET_SSSE3, UNSET_SSSE3},
    {"sse4.1", SET_SSE4_1, UNSET_SSE4_1},
    {"sse4.2", SET_SSE4_2, UNSET_SSE4_2},
    {"sse4", SET_SSE4_2, UNSET_SSE4_1},
    {"sse4a", CC_ISA_SSE4A | SET_SSE3, CC_ISA_SSE4A},
    {"popcnt", CC_ISA_POPCNT, CC_ISA_POPCNT},
    {"crc32", CC_ISA_CRC32, CC_ISA_CRC32},
    {"avx", SET_AVX, UNSET_AVX},
    {"avx2", SET_AVX2, UNSET_AVX2},
    {"fma", CC_ISA_FMA | SET_AVX, CC_ISA_FMA},
    {"f16c", CC_ISA_F16C | SET_AVX, CC_ISA_F16C},
    {"bmi", CC_ISA_BMI, CC_ISA_BMI},
    {"bmi2", CC_ISA_BMI2, CC_ISA_BMI2},
    {"lzcnt", CC_ISA_LZCNT, CC_ISA_LZCNT},
    {"abm", CC_ISA_ABM, CC_ISA_ABM},
    {"3dnow", CC_ISA_MMX, 0},
    {"3dnowa", CC_ISA_MMX, 0},
    {"movbe", CC_ISA_MOVBE, CC_ISA_MOVBE},
    {"xsave", CC_ISA_XSAVE, CC_ISA_XSAVE | UNSET_AVX},
    {"fxsr", CC_ISA_FXSR, CC_ISA_FXSR},
    {"sahf", CC_ISA_SAHF, CC_ISA_SAHF},
    {"cx16", CC_ISA_CX16, CC_ISA_CX16},
    {"avx512f", SET_AVX512F, UNSET_AVX512F},
    {"avx512bw", CC_ISA_AVX512BW | SET_AVX512F, CC_ISA_AVX512BW},
    {"avx512cd", CC_ISA_AVX512CD | SET_AVX512F, CC_ISA_AVX512CD},
    {"avx512dq", CC_ISA_AVX512DQ | SET_AVX512F, CC_ISA_AVX512DQ},
    {"avx512vl", CC_ISA_AVX512VL | SET_AVX512F, CC_ISA_AVX512VL},
};

/**
 * The extension option @p flag names.
 *
 * @param flag  an option
 * @param on    set to 1 for -m<ext>, 0 for -mno-<ext>
 * @return      its entry, or NULL for an option that is not an extension's
 */
static const isa_flag_t *isa_flag_find(const char *flag, int *on) {
    const char *name;
    size_t k;

    if (flag == NULL || strncmp(flag, "-m", 2) != 0) {
        return NULL;
    }
    *on = strncmp(flag, "-mno-", 5) != 0;
    name = flag + (*on ? 2 : 5);
    for (k = 0; k < sizeof(g_isa_flags) / sizeof(g_isa_flags[0]); ++k) {
        if (strcmp(name, g_isa_flags[k].name) == 0) {
            return &g_isa_flags[k];
        }
    }
    return NULL;
}

int cc_isa_is_flag(const char *flag) {
    int on;

    return isa_flag_find(flag, &on) != NULL;
}

unsigned cc_isa_from_flags(unsigned arch_isa, const char *const *flags, size_t n) {
    unsigned isa = 0;
    unsigned explicit_bits = 0;
    size_t i;
    size_t j;

    for (i = 0; i < n; ++i) {
        int on;
        int later_on;
        const isa_flag_t *f = isa_flag_find(flags[i], &on);

        if (f == NULL) {
            continue;
        }
        /* (gcc cancels an option given again later, either way) */
        for (j = i + 1; j < n && isa_flag_find(flags[j], &later_on) != f; ++j) {
        }
        if (j < n) {
            continue;
        }
        if (on) {
            isa |= f->set;
            explicit_bits |= f->set;
        } else {
            isa &= ~f->unset;
            explicit_bits |= f->unset;
        }
    }
    isa |= arch_isa & ~explicit_bits;
    /* what an extension brings where no option said otherwise (gcc's ix86_option_override_internal) */
    if ((isa & CC_ISA_SSE) != 0) {
        isa |= CC_ISA_MMX & ~explicit_bits;
    }
    if ((isa & CC_ISA_SSE4_2) != 0) {
        isa |= (CC_ISA_POPCNT | CC_ISA_CRC32) & ~explicit_bits;
    }
    if ((isa & CC_ISA_ABM) != 0) {
        isa |= (CC_ISA_LZCNT | CC_ISA_POPCNT) & ~explicit_bits;
    }
    return isa;
}

const char *cc_isa_macro(unsigned bit) {
    static const struct {
        unsigned bit;
        const char *macro;
    } macros[] = {
        {CC_ISA_MMX, "__MMX__"},           {CC_ISA_SSE, "__SSE__"},           {CC_ISA_SSE2, "__SSE2__"},
        {CC_ISA_SSE3, "__SSE3__"},         {CC_ISA_SSSE3, "__SSSE3__"},       {CC_ISA_SSE4_1, "__SSE4_1__"},
        {CC_ISA_SSE4_2, "__SSE4_2__"},     {CC_ISA_POPCNT, "__POPCNT__"},     {CC_ISA_CRC32, "__CRC32__"},
        {CC_ISA_AVX, "__AVX__"},           {CC_ISA_AVX2, "__AVX2__"},         {CC_ISA_FMA, "__FMA__"},
        {CC_ISA_F16C, "__F16C__"},         {CC_ISA_BMI, "__BMI__"},           {CC_ISA_BMI2, "__BMI2__"},
        {CC_ISA_LZCNT, "__LZCNT__"},       {CC_ISA_MOVBE, "__MOVBE__"},       {CC_ISA_XSAVE, "__XSAVE__"},
        {CC_ISA_FXSR, "__FXSR__"},         {CC_ISA_AVX512F, "__AVX512F__"},   {CC_ISA_AVX512BW, "__AVX512BW__"},
        {CC_ISA_AVX512CD, "__AVX512CD__"}, {CC_ISA_AVX512DQ, "__AVX512DQ__"}, {CC_ISA_AVX512VL, "__AVX512VL__"},
        {CC_ISA_SSE4A, "__SSE4A__"},       {CC_ISA_ABM, "__ABM__"},
    };
    size_t k;

    for (k = 0; k < sizeof(macros) / sizeof(macros[0]); ++k) {
        if (macros[k].bit == bit) {
            return macros[k].macro;
        }
    }
    return NULL;
}
