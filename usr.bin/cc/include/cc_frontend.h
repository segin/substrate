#ifndef CC_FRONTEND_H
#define CC_FRONTEND_H

#include <stddef.h>

typedef struct cc_diag {
    char path[512];
    size_t line;
    size_t col;
    size_t error_count;
    char message[256];
} cc_diag_t;

/* cc_expr_t.unary_form: which operand of the binary spelling is the unary operator's */
#define CC_UNARY_RHS 1
#define CC_UNARY_LHS 2

typedef unsigned long long cc_attr_flags_t;

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
#define CC_STORAGE_CONSTEXPR (1 << 10) /**< C23 constexpr: a const object whose value is a constant expression */

#define CC_ATTR_PACKED     (1ULL << 0)
#define CC_ATTR_ALIGNED    (1ULL << 1)
#define CC_ATTR_NORETURN   (1ULL << 2)
#define CC_ATTR_UNUSED     (1ULL << 3)
#define CC_ATTR_USED       (1ULL << 4)
#define CC_ATTR_SECTION    (1ULL << 5)
#define CC_ATTR_DEPRECATED (1ULL << 6)
#define CC_ATTR_NODISCARD  (1ULL << 7)
#define CC_ATTR_REPRODUCIBLE (1ULL << 8)
#define CC_ATTR_UNSEQUENCED (1ULL << 9)
#define CC_ATTR_FALLTHROUGH (1ULL << 10)
#define CC_ATTR_ALWAYS_INLINE (1ULL << 11)
#define CC_ATTR_NOINLINE (1ULL << 12)
#define CC_ATTR_HOT (1ULL << 13)
#define CC_ATTR_COLD (1ULL << 14)
#define CC_ATTR_FORMAT (1ULL << 15)
#define CC_ATTR_NONNULL (1ULL << 16)
#define CC_ATTR_MALLOC_FN (1ULL << 17)
#define CC_ATTR_WEAK (1ULL << 18)
#define CC_ATTR_ALIAS (1ULL << 19)
#define CC_ATTR_FLATTEN (1ULL << 20)
#define CC_ATTR_TARGET (1ULL << 21)
#define CC_ATTR_TLS_MODEL (1ULL << 22)
#define CC_ATTR_CLEANUP (1ULL << 23)
#define CC_ATTR_VIS_DEFAULT (1ULL << 24)
#define CC_ATTR_VIS_HIDDEN (1ULL << 25)
#define CC_ATTR_VIS_PROTECTED (1ULL << 26)
#define CC_ATTR_VIS_INTERNAL (1ULL << 27)
#define CC_ATTR_TRANSPARENT_UNION (1ULL << 28)
#define CC_ATTR_VECTOR_SIZE (1ULL << 29)
#define CC_ATTR_MAY_ALIAS (1ULL << 30)
#define CC_ATTR_CONSTRUCTOR (1ULL << 31)
#define CC_ATTR_DESTRUCTOR (1ULL << 32)
#define CC_ATTR_GNU_INLINE (1ULL << 33) /* GNU89 inline meaning: `extern inline` defines nothing outside, `inline` does */

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
    CC_EXPR_STMT,
    CC_EXPR_COMPLEX, /**< a complex value from its parts: lhs real, rhs imaginary (`2.0i`, __builtin_complex) */
    /**
     * An initializer with a designation, `.a[2].b = v`, as the parser reads
     * it: args are the designators -- CC_EXPR_MEMBER with ident for `.name`,
     * CC_EXPR_INT with int_val (and int_val_hi, for GNU `[lo ... hi]`) for
     * `[index]` -- and rhs the value.  parser_canon_initializer() resolves
     * every one before anything else sees the initializer.
     */
    CC_EXPR_DESIGNATION
} cc_expr_kind_t;

typedef struct cc_expr cc_expr_t;
typedef struct cc_stmt cc_stmt_t;

/**
 * The run-time bounds of a variable-length array type: one slot per array
 * level, outermost first, NULL where that level's bound is a constant.
 * Owns its expressions; empty (all zero) when no level has one.
 */
typedef struct {
    cc_expr_t **at; /**< the bounds */
    size_t count;   /**< the number of slots */
} cc_bounds_t;
typedef struct cc_asm_operand cc_asm_operand_t;

struct cc_expr {
    cc_expr_kind_t kind;
    size_t line;
    size_t col;
    unsigned char paren_wrapped;
    unsigned char is_subscript; /**< a CC_BIN_ADD that is the `a + i` of a subscript `a[i]` */
    /**
     * A unary operator the parser spelled as a binary one: CC_UNARY_RHS for
     * `-x` and `+x` (`-1 * x`, `1 * x`), CC_UNARY_LHS for `~x` (`x ^ -1`);
     * 0 for a real binary operator.  The result has the operand's promoted
     * type, not the usual arithmetic conversion's.
     */
    unsigned char unary_form;
    /**
     * A CC_EXPR_CAST sema put over a floating operation it evaluates in
     * long double (FLT_EVAL_METHOD 2): the expression has the type the cast
     * names, its value the operand's greater range and precision (C11
     * 5.2.4.2.2p9), which a conversion takes from the operand, not the cast.
     */
    unsigned char excess_precision;
    /**
     * A CC_EXPR_CAST to a decimal floating type, _Float128 or a wide integer
     * type whose brace list is the constant's encoding (its limbs, for a wide
     * integer), as the parser makes a literal and sema a
     * folded conversion -- not a compound literal the program wrote, whose
     * list holds a value of the type.
     */
    unsigned char encoded_literal;
    long long int_val;
    long long int_val_hi; /**< an array designator `[lo ... hi]`'s hi (int_val is lo); lo for `[index]` */
    double float_val;
    long double float_val_ld;
    char *ident;
    cc_binop_t op;
    int member_is_arrow;
    int member_is_part; /**< GNU __real__ / __imag__: the member is a complex value's part */
    long member_offset;
    int member_is_bitfield;
    int member_bit_width;
    int member_bit_offset;
    int member_bit_storage_bits;
    int member_bit_signed;
    long member_bit_storage_size;
    int member_bit_split; /**< the bit-field's cc_struct_member_t.bit_split */
    cc_expr_t *lhs;
    cc_expr_t *rhs;
    cc_expr_t *third;
    int update_postfix;
    cc_expr_t **args;
    size_t arg_count;
    const struct cc_ctype **generic_ctypes; /**< _Generic: each association's type, NULL for `default` */
    unsigned char *generic_is_default;
    size_t generic_count;
    long generic_selected;
    cc_stmt_t *stmt_expr_stmts;
    size_t stmt_expr_count;
    const struct cc_ctype *ctype; /**< the expression's type: what the parser gave a constant, cast or other
                                       node it built, and what sema gives every expression it checks */
    const struct cc_ctype *aux_ctype; /**< the type a `sizeof (T)` or `_Alignof (T)` names, or a string
                                           literal's element type; else NULL */
    const struct cc_ctype *named_const_type; /**< a CC_EXPR_IDENT naming a constexpr integer object, or a
                                                  CC_EXPR_MEMBER an integer member of a constexpr structure:
                                                  its type, int_val its value's bits; else NULL */
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

struct cc_ctype;

struct cc_stmt {
    size_t line;
    size_t col;
    const struct cc_ctype *ctype; /**< a declaration's type (cc_ctype.h) */
    int storage;
    cc_attr_flags_t attr_flags;
    long attr_align;
    char *attr_section;
    char *attr_alias;
    char *asm_register; /**< a `register ... asm("reg")` declaration: the register, without % (owned) */
    char *attr_cleanup; /**< a declaration's `cleanup(fn)`: fn, called with the variable's address as the
                             variable goes out of scope (owned); NULL for none */
    cc_stmt_kind_t kind;
    int is_synthetic_block;
    char *decl_name;
    char *label_name;
    /** a declared variable-length array's run-time bounds (for a pointer to one, its pointee's) */
    cc_bounds_t array_bounds;
    cc_expr_t *expr;
    cc_stmt_t *init_stmt;
    cc_expr_t *init_expr;
    cc_expr_t *post_expr;
    cc_stmt_t *then_branch;
    cc_stmt_t *else_branch;
    cc_stmt_t *block_stmts;
    size_t block_count;
    long long case_hi; /* long long, as a case value is: a host long can be 32 bits */
    int case_has_range;
    int asm_is_volatile;
    int asm_is_goto;
    int asm_is_basic; /**< basic asm (no `:`): the template is the text, `%` and all */
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
    int storage;
    const struct cc_ctype *ctype; /**< its type as adjusted (arrays and functions as pointers) */
    /** for a pointer to a variable-length array (as `int a[n][m]` is adjusted to be): the bounds of what it
     *  points to */
    cc_bounds_t vla_bounds;
} cc_param_t;

typedef struct {
    char *name;
    long offset;
    long size;
    int is_bitfield;
    int bit_width;
    int bit_offset;
    int bit_storage_bits;
    int bit_signed;
    long bit_storage_size;
    int bit_split; /**< a bit-field whose storage unit holds an ordinary member's bytes too: no unit could
                        avoid them, so a store writes only the bytes the field occupies */
    const struct cc_ctype *ctype; /**< its type, where its declaration recorded one; else NULL */
} cc_struct_member_t;

/**
 * One member declarator of a structure or union, as its layout reads it:
 * unnamed bit-fields, which are not members but take up room, included.
 */
typedef struct {
    int member;                   /**< the member it places (an index into members), or -1 for none */
    const struct cc_ctype *ctype; /**< a bit-field's declared type */
    long size;                    /**< an ordinary member's size in bytes */
    long align;                   /**< its type's alignment, or 1 where the declarator is `packed` */
    long user_align;              /**< the alignment `aligned` or `_Alignas` asks for, or 0 */
    int bit_width;                /**< a bit-field's width, or -1 for an ordinary member */
} cc_member_layout_t;

typedef struct {
    char *tag;
    int depth;
    cc_struct_member_t *members;
    size_t member_count;
    size_t member_cap;
    cc_member_layout_t *layout;   /**< its member declarators in order, as declared */
    size_t layout_count;
    size_t layout_cap;
    long size;
    long align;
    /** what `_Alignof` gives, as gcc gives it: align, but an alignment that only vectors give capped at 16;
     *  0 for align */
    long c11_align;
    cc_attr_flags_t attr_flags;
    long attr_align;
    char *attr_alias;
    int is_union;
    int has_flexible_array;
    int complete;
    /** for the structure standing for a complex type, `{ T __re, __im; }`: T (float, double or
     *  long double); NULL for any other structure */
    const struct cc_ctype *complex_elem;
    /** for the structure standing for an integer type wider than 64 bits -- `_BitInt(N)` or
     *  `__int128`, `{ limb __limb[n]; }` with limbs of the target's word -- its width N; else 0 */
    long wide_bits;
    int wide_unsigned;  /**< such an integer type is unsigned */
    int wide_is_int128; /**< it is `__int128`, not `_BitInt(128)` (a distinct type, 16-aligned) */
    /** for the structure standing for a vector type (`vector_size`, `ext_vector_type`),
     *  `{ T __vec[n]; }`: n, its element count; else 0 */
    long vector_count;
    const struct cc_ctype *vector_elem; /**< such a vector's element type */
    int vector_cmp_sid;    /**< the vector type its comparisons give: signed integers of the element's size */
    /** for the structure standing for a decimal floating type, `{ __bid; }` holding its BID
     *  encoding (an integer, or two 64-bit halves low first for _Decimal128): 32, 64 or 128; else 0 */
    int decimal_bits;
    /** for the structure standing for _Float128 (`__float128`), `{ __b128[2]; }` holding its IEEE binary128
     *  encoding, two 64-bit halves low first: 128; else 0 */
    int binary_bits;
} cc_struct_def_t;

/**
 * A canonical initializer names the member a union is initialized through
 * with a designator (a CC_EXPR_MEMBER whose ident is the member's name).
 * An anonymous member has no name; its designator is spelled with this
 * prefix and the member's index, "#2", which no identifier can be.
 */
#define CC_MEMBER_INDEX_DESIGNATOR '#'

/**
 * The index of the member of @p sd that designator @p name names: a member
 * of that name, or the one a CC_MEMBER_INDEX_DESIGNATOR spelling gives.
 *
 * @return the index, or -1 if there is none
 */
int cc_struct_designated_member(const cc_struct_def_t *sd, const char *name);

/**
 * The width of the floating type a structure stands for when cc carries
 * that type's encoding in one and libgcc operates on it: a decimal type's
 * (32, 64, 128) or _Float128's (128).
 *
 * @param sd  the structure, or NULL
 * @return    its width in bits, or 0 for any other structure
 */
static inline int cc_struct_soft_float_bits(const cc_struct_def_t *sd) {
    return sd == NULL ? 0 : sd->decimal_bits > 0 ? sd->decimal_bits : sd->binary_bits;
}

typedef struct {
    char *name;
    size_t line;
    size_t col;
    const struct cc_ctype *ret_ctype; /**< the return type */
    int storage;
    cc_attr_flags_t attr_flags;
    long attr_align;
    long attr_format_index;
    long attr_format_first_to_check;
    int attr_regparm; /**< `regparm(n)` (i386): n + 1; 0 when this declaration does not say */
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
    char *nest_parent;      /**< a GNU nested function: the name of the function it is defined in (owned); else NULL */
    size_t nest_stmt_index; /**< where in that function's body: the number of its top-level statements before it */
} cc_function_t;

typedef struct {
    char *name;
    size_t line;
    size_t col;
    int storage;
    cc_attr_flags_t attr_flags;
    long attr_align;
    char *attr_section;
    char *attr_alias;
    char *asm_register; /**< a global register variable, `register T x asm("esp")`: its register, without %;
                             it has no storage and reading it reads the register (owned) */
    cc_expr_t *init;
    const struct cc_ctype *ctype; /**< its type (cc_ctype.h): as declared, completed by a later declaration or,
                                       by sema, from its initializer */
} cc_global_t;

typedef struct cc_translation_unit {
    cc_function_t *funcs;
    size_t func_count;
    cc_global_t *globals;
    size_t global_count;
    cc_struct_def_t *structs;
    size_t struct_count;
    char **toplevel_asm;       /**< each file-scope `asm("...")`'s text, in source order (owned) */
    size_t toplevel_asm_count; /**< the number of them */
} cc_translation_unit_t;

/** A variable of the function a GNU nested function is defined in, visible where it is defined. */
typedef struct {
    const char *name;
    int storage;
    const struct cc_ctype *ctype; /**< its declared type (cc_ctype.h) */
} cc_outer_var_t;

int cc_nested_outer_vars(const cc_translation_unit_t *tu, const cc_function_t *f, cc_outer_var_t **out,
                         size_t *count);
int cc_nested_uses_outer(const cc_translation_unit_t *tu, const cc_function_t *f);
const cc_function_t *cc_find_function_def(const cc_translation_unit_t *tu, const char *name);

int cc_parse_file(const char *path, cc_translation_unit_t *out, cc_diag_t *diag);

/**
 * Free an expression tree: its nodes, their names, arguments and
 * statement-expression bodies.  For a pass that replaces part of the tree.
 *
 * @param e  the expression, or NULL
 */
void cc_expr_free(cc_expr_t *e);

/**
 * A deep copy of an expression tree, as the parser makes for a compound
 * assignment's second reading of its left operand.
 *
 * @param e  the expression
 * @return   the copy, or NULL when out of memory (or @p e is NULL)
 */
cc_expr_t *cc_expr_clone(const cc_expr_t *e);

/**
 * The run-time bound of one array level.
 *
 * @param b  the bounds, or NULL
 * @param k  the level, 0 outermost
 * @return   the bound, or NULL where it is a constant (or past the slots)
 */
cc_expr_t *cc_bounds_at(const cc_bounds_t *b, size_t k);

/**
 * Whether any level has a run-time bound.
 *
 * @param b  the bounds, or NULL
 * @return   1 or 0
 */
int cc_bounds_any(const cc_bounds_t *b);

/**
 * Free the bounds' expressions and slots, leaving them empty.
 *
 * @param b  the bounds, or NULL
 */
void cc_bounds_free(cc_bounds_t *b);
int cc_sema_check(const cc_translation_unit_t *tu, cc_diag_t *diag);
void cc_tu_free(cc_translation_unit_t *tu);
int cc_builtin_is_recognized(const char *name);
int cc_builtin_bswap_bits(const char *name);
int cc_preprocess_file(const char *in_path, const char *out_path, const char *std_mode,
                       const char *const *flags, size_t flag_count, cc_diag_t *diag);

#endif
