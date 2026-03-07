#ifndef CC_FRONTEND_H
#define CC_FRONTEND_H

#include <stddef.h>

typedef struct {
    char path[512];
    size_t line;
    size_t col;
    size_t error_count;
    char message[256];
} cc_diag_t;

#define CC_MAX_ARRAY_DIMS 4

#define CC_STORAGE_STATIC  (1 << 0)
#define CC_STORAGE_EXTERN  (1 << 1)
#define CC_STORAGE_INLINE  (1 << 2)
#define CC_STORAGE_AUTO    (1 << 3)
#define CC_STORAGE_REGISTER (1 << 4)
#define CC_STORAGE_THREAD_LOCAL (1 << 5)
#define CC_STORAGE_AUTO_TYPE (1 << 6)
#define CC_STORAGE_CONST (1 << 7)
#define CC_STORAGE_VOLATILE (1 << 8)
#define CC_STORAGE_RESTRICT (1 << 9)

#define CC_ATTR_PACKED     (1 << 0)
#define CC_ATTR_ALIGNED    (1 << 1)
#define CC_ATTR_NORETURN   (1 << 2)
#define CC_ATTR_UNUSED     (1 << 3)
#define CC_ATTR_USED       (1 << 4)
#define CC_ATTR_SECTION    (1 << 5)
#define CC_ATTR_DEPRECATED (1 << 6)
#define CC_ATTR_NODISCARD  (1 << 7)
#define CC_ATTR_REPRODUCIBLE (1 << 8)
#define CC_ATTR_UNSEQUENCED (1 << 9)
#define CC_ATTR_FALLTHROUGH (1 << 10)
#define CC_ATTR_ALWAYS_INLINE (1 << 11)
#define CC_ATTR_NOINLINE (1 << 12)
#define CC_ATTR_HOT (1 << 13)
#define CC_ATTR_COLD (1 << 14)
#define CC_ATTR_FORMAT (1 << 15)
#define CC_ATTR_NONNULL (1 << 16)
#define CC_ATTR_MALLOC_FN (1 << 17)
#define CC_ATTR_WEAK (1 << 18)
#define CC_ATTR_ALIAS (1 << 19)
#define CC_ATTR_FLATTEN (1 << 20)
#define CC_ATTR_TARGET (1 << 21)
#define CC_ATTR_TLS_MODEL (1 << 22)
#define CC_ATTR_CLEANUP (1 << 23)
#define CC_ATTR_VIS_DEFAULT (1 << 24)
#define CC_ATTR_VIS_HIDDEN (1 << 25)
#define CC_ATTR_VIS_PROTECTED (1 << 26)
#define CC_ATTR_VIS_INTERNAL (1 << 27)
#define CC_ATTR_TRANSPARENT_UNION (1 << 28)
#define CC_ATTR_VECTOR_SIZE (1 << 29)
#define CC_ATTR_MAY_ALIAS (1 << 30)

typedef enum {
    CC_TYPE_VOID = 0,
    CC_TYPE_BOOL,
    CC_TYPE_CHAR,
    CC_TYPE_UCHAR,
    CC_TYPE_SHORT,
    CC_TYPE_USHORT,
    CC_TYPE_INT,
    CC_TYPE_UINT,
    CC_TYPE_LONG_LONG,
    CC_TYPE_ULONG_LONG,
    CC_TYPE_FLOAT,
    CC_TYPE_DOUBLE,
    CC_TYPE_PTR_VOID,
    CC_TYPE_PTR_BOOL,
    CC_TYPE_PTR_CHAR,
    CC_TYPE_PTR_UCHAR,
    CC_TYPE_PTR_SHORT,
    CC_TYPE_PTR_USHORT,
    CC_TYPE_PTR_INT,
    CC_TYPE_PTR_UINT,
    CC_TYPE_PTR_LONG_LONG,
    CC_TYPE_PTR_ULONG_LONG,
    CC_TYPE_PTR_FLOAT,
    CC_TYPE_PTR_DOUBLE,
    CC_TYPE_PTR_PTR_VOID,
    CC_TYPE_PTR_PTR_BOOL,
    CC_TYPE_PTR_PTR_CHAR,
    CC_TYPE_PTR_PTR_UCHAR,
    CC_TYPE_PTR_PTR_SHORT,
    CC_TYPE_PTR_PTR_USHORT,
    CC_TYPE_PTR_PTR_INT,
    CC_TYPE_PTR_PTR_UINT,
    CC_TYPE_PTR_PTR_LONG_LONG,
    CC_TYPE_PTR_PTR_ULONG_LONG,
    CC_TYPE_PTR_PTR_FLOAT,
    CC_TYPE_PTR_PTR_DOUBLE,
    CC_TYPE_PTR_PTR_PTR_VOID,
    CC_TYPE_PTR_PTR_PTR_BOOL,
    CC_TYPE_PTR_PTR_PTR_CHAR,
    CC_TYPE_PTR_PTR_PTR_UCHAR,
    CC_TYPE_PTR_PTR_PTR_SHORT,
    CC_TYPE_PTR_PTR_PTR_USHORT,
    CC_TYPE_PTR_PTR_PTR_INT,
    CC_TYPE_PTR_PTR_PTR_UINT,
    CC_TYPE_PTR_PTR_PTR_LONG_LONG,
    CC_TYPE_PTR_PTR_PTR_ULONG_LONG,
    CC_TYPE_PTR_PTR_PTR_FLOAT,
    CC_TYPE_PTR_PTR_PTR_DOUBLE,
    CC_TYPE_PTR_PTR_PTR_PTR_VOID,
    CC_TYPE_PTR_PTR_PTR_PTR_BOOL,
    CC_TYPE_PTR_PTR_PTR_PTR_CHAR,
    CC_TYPE_PTR_PTR_PTR_PTR_UCHAR,
    CC_TYPE_PTR_PTR_PTR_PTR_SHORT,
    CC_TYPE_PTR_PTR_PTR_PTR_USHORT,
    CC_TYPE_PTR_PTR_PTR_PTR_INT,
    CC_TYPE_PTR_PTR_PTR_PTR_UINT,
    CC_TYPE_PTR_PTR_PTR_PTR_LONG_LONG,
    CC_TYPE_PTR_PTR_PTR_PTR_ULONG_LONG,
    CC_TYPE_PTR_PTR_PTR_PTR_FLOAT,
    CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_VOID,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_BOOL,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_CHAR,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_UCHAR,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_SHORT,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_USHORT,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_INT,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_UINT,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_LONG_LONG,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_ULONG_LONG,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_FLOAT,
    CC_TYPE_PTR_PTR_PTR_PTR_PTR_DOUBLE,
    /* New scalar/type-class ids appended without changing legacy pointer ids. */
    CC_TYPE_SCHAR,
    CC_TYPE_LONG,
    CC_TYPE_ULONG,
    CC_TYPE_LDOUBLE,
    CC_TYPE_ENUM,
    CC_TYPE_COMPLEX,
    CC_TYPE_IMAGINARY,
    CC_TYPE_BITINT,
    CC_TYPE_DECIMAL32,
    CC_TYPE_DECIMAL64,
    CC_TYPE_DECIMAL128,
    CC_TYPE_ATOMIC,
    CC_TYPE_FUNC
} cc_type_t;

#define CC_LEGACY_BASE_MIN CC_TYPE_VOID
#define CC_LEGACY_BASE_MAX CC_TYPE_DOUBLE
#define CC_LEGACY_BASE_COUNT 12u
#define CC_LEGACY_PTR_L1_MIN CC_TYPE_PTR_VOID
#define CC_LEGACY_PTR_L5_MAX CC_TYPE_PTR_PTR_PTR_PTR_PTR_DOUBLE
#define CC_LEGACY_PTR_MAX_DEPTH 5u

/*
 * Dynamic pointer encoding for arbitrary nesting:
 *   bits[31:24] = 0x7F tag
 *   bits[23:8]  = depth (1..65535)
 *   bits[7:0]   = base cc_type_t id
 */
#define CC_TYPE_PTR_DYN_TAG 0x7F000000u
#define CC_TYPE_PTR_DYN_DEPTH_SHIFT 8u
#define CC_TYPE_PTR_DYN_DEPTH_MASK 0x00FFFF00u
#define CC_TYPE_PTR_DYN_BASE_MASK 0x000000FFu

static inline int cc_type_is_legacy_base(cc_type_t t) {
    return t >= CC_LEGACY_BASE_MIN && t <= CC_LEGACY_BASE_MAX;
}

static inline int cc_type_is_dynamic_pointer(cc_type_t t) {
    return (((unsigned int)t & 0xFF000000u) == CC_TYPE_PTR_DYN_TAG);
}

static inline int cc_type_is_pointer(cc_type_t t) {
    if (t == CC_TYPE_FUNC) return(1);
    if (t >= CC_LEGACY_PTR_L1_MIN && t <= CC_LEGACY_PTR_L5_MAX) return(1);
    return(cc_type_is_dynamic_pointer(t));
}

static inline unsigned int cc_type_pointer_depth(cc_type_t t) {
    if (t == CC_TYPE_FUNC) return(1u);
    if (t >= CC_LEGACY_PTR_L1_MIN && t <= CC_LEGACY_PTR_L5_MAX) {
        return((unsigned int)(t - CC_LEGACY_PTR_L1_MIN) / CC_LEGACY_BASE_COUNT + 1u);
    }
    if (cc_type_is_dynamic_pointer(t)) {
        return(((unsigned int)t & CC_TYPE_PTR_DYN_DEPTH_MASK) >> CC_TYPE_PTR_DYN_DEPTH_SHIFT);
    }
    return(0u);
}

static inline cc_type_t cc_type_pointer_base(cc_type_t t) {
    if (t == CC_TYPE_FUNC) return(CC_TYPE_VOID);
    if (t >= CC_LEGACY_PTR_L1_MIN && t <= CC_LEGACY_PTR_L5_MAX) {
        unsigned int idx = (unsigned int)(t - CC_LEGACY_PTR_L1_MIN) % CC_LEGACY_BASE_COUNT;
        return((cc_type_t)(CC_LEGACY_BASE_MIN + (cc_type_t)idx));
    }
    if (cc_type_is_dynamic_pointer(t)) {
        return((cc_type_t)((unsigned int)t & CC_TYPE_PTR_DYN_BASE_MASK));
    }
    return(CC_TYPE_VOID);
}

static inline cc_type_t cc_type_deref_once(cc_type_t t) {
    unsigned int depth;
    cc_type_t base;
    if (!cc_type_is_pointer(t)) return(CC_TYPE_VOID);
    depth = cc_type_pointer_depth(t);
    base = cc_type_pointer_base(t);
    if (depth <= 1u) return(base);
    if (cc_type_is_legacy_base(base) && (depth - 1u) <= CC_LEGACY_PTR_MAX_DEPTH) {
        return((cc_type_t)(CC_LEGACY_PTR_L1_MIN + (base - CC_LEGACY_BASE_MIN) +
                           ((depth - 2u) * CC_LEGACY_BASE_COUNT)));
    }
    return((cc_type_t)(CC_TYPE_PTR_DYN_TAG | ((depth - 1u) << CC_TYPE_PTR_DYN_DEPTH_SHIFT) |
                       ((unsigned int)base & CC_TYPE_PTR_DYN_BASE_MASK)));
}

static inline cc_type_t cc_type_make_pointer(cc_type_t t) {
    unsigned int depth;
    cc_type_t base;
    if (!cc_type_is_pointer(t)) {
        if (cc_type_is_legacy_base(t)) {
            return((cc_type_t)(CC_LEGACY_PTR_L1_MIN + (t - CC_LEGACY_BASE_MIN)));
        }
        return((cc_type_t)(CC_TYPE_PTR_DYN_TAG | (1u << CC_TYPE_PTR_DYN_DEPTH_SHIFT) | ((unsigned int)t & CC_TYPE_PTR_DYN_BASE_MASK)));
    }
    depth = cc_type_pointer_depth(t) + 1u;
    base = cc_type_pointer_base(t);
    if (cc_type_is_legacy_base(base) && depth <= CC_LEGACY_PTR_MAX_DEPTH) {
        return((cc_type_t)(CC_LEGACY_PTR_L1_MIN + (base - CC_LEGACY_BASE_MIN) + ((depth - 1u) * CC_LEGACY_BASE_COUNT)));
    }
    if (depth > 0xFFFFu) {
        depth = 0xFFFFu;
    }
    return((cc_type_t)(CC_TYPE_PTR_DYN_TAG | (depth << CC_TYPE_PTR_DYN_DEPTH_SHIFT) |
                       ((unsigned int)base & CC_TYPE_PTR_DYN_BASE_MASK)));
}

typedef enum {
    CC_BIN_ADD = 0,
    CC_BIN_SUB,
    CC_BIN_MUL,
    CC_BIN_DIV,
    CC_BIN_MOD,
    CC_BIN_SHL,
    CC_BIN_SHR,
    CC_BIN_BAND,
    CC_BIN_BXOR,
    CC_BIN_BOR,
    CC_BIN_EQ,
    CC_BIN_NE,
    CC_BIN_LT,
    CC_BIN_LE,
    CC_BIN_GT,
    CC_BIN_GE,
    CC_BIN_LAND,
    CC_BIN_LOR,
    CC_BIN_COMMA
} cc_binop_t;

typedef enum {
    CC_EXPR_INT = 0,
    CC_EXPR_FLOAT,
    CC_EXPR_STR,
    CC_EXPR_IDENT,
    CC_EXPR_BIN,
    CC_EXPR_MEMBER,
    CC_EXPR_CALL,
    CC_EXPR_ASSIGN,
    CC_EXPR_UPDATE,
    CC_EXPR_ADDR,
    CC_EXPR_LABEL_ADDR,
    CC_EXPR_DEREF,
    CC_EXPR_CAST,
    CC_EXPR_SIZEOF,
    CC_EXPR_INIT_LIST,
    CC_EXPR_GENERIC,
    CC_EXPR_TERNARY,
    CC_EXPR_STMT
} cc_expr_kind_t;

typedef struct cc_expr cc_expr_t;
typedef struct cc_stmt cc_stmt_t;
typedef struct cc_asm_operand cc_asm_operand_t;

struct cc_expr {
    cc_expr_kind_t kind;
    size_t line;
    size_t col;
    cc_type_t value_type;
    int struct_id;
    unsigned char paren_wrapped;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
    long int_val;
    double float_val;
    char *ident;
    cc_binop_t op;
    int member_is_arrow;
    long member_offset;
    int member_is_bitfield;
    int member_bit_width;
    int member_bit_offset;
    int member_bit_storage_bits;
    int member_bit_signed;
    long member_bit_storage_size;
    cc_expr_t *lhs;
    cc_expr_t *rhs;
    cc_expr_t *third;
    int update_postfix;
    cc_type_t aux_type;
    int aux_struct_id;
    cc_expr_t **args;
    size_t arg_count;
    cc_type_t *generic_types;
    int *generic_struct_ids;
    unsigned char *generic_is_default;
    size_t generic_count;
    long generic_selected;
    cc_stmt_t *stmt_expr_stmts;
    size_t stmt_expr_count;
};

typedef enum {
    CC_STMT_DECL = 0,
    CC_STMT_EXPR,
    CC_STMT_ASM,
    CC_STMT_RETURN,
    CC_STMT_IF,
    CC_STMT_BLOCK,
    CC_STMT_WHILE,
    CC_STMT_DO,
    CC_STMT_FOR,
    CC_STMT_SWITCH,
    CC_STMT_CASE,
    CC_STMT_DEFAULT,
    CC_STMT_BREAK,
    CC_STMT_CONTINUE,
    CC_STMT_GOTO,
    CC_STMT_LABEL
} cc_stmt_kind_t;

struct cc_asm_operand {
    char *name;
    char *constraint;
    cc_expr_t *expr;
};

struct cc_stmt {
    size_t line;
    size_t col;
    cc_type_t type;
    int type_struct_id;
    long array_len;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
    int storage;
    int attr_flags;
    long attr_align;
    char *attr_section;
    char *attr_alias;
    cc_stmt_kind_t kind;
    int is_synthetic_block;
    char *decl_name;
    char *label_name;
    cc_expr_t *expr;
    cc_stmt_t *init_stmt;
    cc_expr_t *init_expr;
    cc_expr_t *post_expr;
    cc_stmt_t *then_branch;
    cc_stmt_t *else_branch;
    cc_stmt_t *block_stmts;
    size_t block_count;
    long case_hi;
    int case_has_range;
    int asm_is_volatile;
    int asm_is_goto;
    char *asm_template;
    cc_asm_operand_t *asm_outputs;
    size_t asm_output_count;
    cc_asm_operand_t *asm_inputs;
    size_t asm_input_count;
    char **asm_clobbers;
    size_t asm_clobber_count;
    char **asm_goto_labels;
    size_t asm_goto_label_count;
};

typedef struct {
    char *name;
    cc_type_t type;
    int type_struct_id;
    long array_len;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
    int storage;
} cc_param_t;

typedef struct {
    char *name;
    cc_type_t type;
    int type_struct_id;
    long array_len;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
    long offset;
    long size;
    int is_bitfield;
    int bit_width;
    int bit_offset;
    int bit_storage_bits;
    int bit_signed;
    long bit_storage_size;
} cc_struct_member_t;

typedef struct {
    char *tag;
    int depth;
    cc_struct_member_t *members;
    size_t member_count;
    long size;
    long align;
    int attr_flags;
    long attr_align;
    char *attr_alias;
    int is_union;
    int has_flexible_array;
    int complete;
} cc_struct_def_t;

typedef struct {
    char *name;
    size_t line;
    size_t col;
    cc_type_t ret_type;
    int ret_struct_id;
    int storage;
    int attr_flags;
    long attr_align;
    long attr_format_index;
    long attr_format_first_to_check;
    char *attr_section;
    char *attr_alias;
    int has_body;
    int has_prototype;
    int has_oldstyle_param_decls;
    int is_variadic;
    cc_param_t *params;
    size_t param_count;
    cc_stmt_t *stmts;
    size_t stmt_count;
} cc_function_t;

typedef struct {
    char *name;
    size_t line;
    size_t col;
    cc_type_t type;
    int type_struct_id;
    long array_len;
    int array_ndim;
    long array_dims[CC_MAX_ARRAY_DIMS];
    int storage;
    int attr_flags;
    long attr_align;
    char *attr_section;
    char *attr_alias;
    cc_expr_t *init;
} cc_global_t;

typedef struct {
    cc_function_t *funcs;
    size_t func_count;
    cc_global_t *globals;
    size_t global_count;
    cc_struct_def_t *structs;
    size_t struct_count;
} cc_translation_unit_t;

int cc_parse_file(const char *path, cc_translation_unit_t *out, cc_diag_t *diag);
int cc_sema_check(const cc_translation_unit_t *tu, cc_diag_t *diag);
void cc_tu_free(cc_translation_unit_t *tu);
int cc_builtin_is_recognized(const char *name);
int cc_builtin_bswap_bits(const char *name);
void cc_frontend_set_pointer_size(int bytes);
void cc_frontend_set_std_mode(const char *std_mode);
void cc_frontend_set_gnu89_inline_mode(int enabled, int override_set);
void cc_frontend_set_implicit_funcdecl_policy(int allow, int override_set);
void cc_frontend_set_diag_flags(int wall, int werror, int pedantic, int pedantic_errors);
int cc_preprocess_file(const char *in_path, const char *out_path, const char *std_mode,
                       const char *const *flags, size_t flag_count, cc_diag_t *diag);

#endif
