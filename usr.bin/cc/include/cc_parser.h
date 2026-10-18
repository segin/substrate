#ifndef CC_PARSER_H
#define CC_PARSER_H

/*
 * cc_parser.h: the parser's state, and what it shares with the semantic
 * work it still does while parsing (frontend/parse_sema.c).
 *
 * C cannot be parsed without some semantics: a name is a typedef name or
 * not, `typeof (e)` names the type of an expression, and an array bound
 * or an enumerator's value is a constant expression the interned types
 * carry.  frontend/parser.c is the syntax; frontend/parse_sema.c owns the
 * variable table, the typing of expressions the parser needs, and the
 * evaluation of constant expressions in the parser's context.  Nothing
 * outside these two files includes this header.
 */

#include "cc_frontend.h"
#include "cc_consteval.h"
#include "cc_ctype.h"
#include "cc_diag.h"
#include "cc_lexer.h"

#include <stddef.h>
#include <stdint.h>

/** A typedef name in scope. */
typedef struct {
    char *name;              /**< its name */
    int depth;               /**< the scope depth it was declared at */
    const cc_ctype_t *ctype; /**< the type it names */
    long align;              /**< the alignment its declaration's `aligned` gives the type, or 0 (gcc) */
} typedef_entry_t;

/** A variable in scope, as the parser tracks it. */
typedef struct {
    char *name;               /**< its name */
    int depth;                /**< the scope depth it was declared at */
    const cc_ctype_t *ctype;  /**< its type */
    const cc_ctype_t *const_type; /**< a constexpr integer object: its type, the value's; else NULL */
    uint64_t const_bits;      /**< that value's bits, before conversion to const_type */
    long align;               /**< the alignment its declaration asks for (`aligned`, `_Alignas`, a typedef's), or 0 */
    int is_constexpr;         /**< a constexpr object: its name is a constant in another's initializer */
    const cc_expr_t *const_init; /**< a constexpr structure: its canonical initializer (its declaration's); else NULL */
} var_entry_t;

/** An enumeration constant. */
typedef struct {
    char *name;              /**< its name */
    long value;              /**< its value */
    uint64_t bits;           /**< its value's bits, as wide as its type */
    const cc_ctype_t *ctype; /**< the type of its value */
} enum_const_entry_t;

/** An enumeration tag. */
typedef struct {
    char *tag;               /**< the tag */
    const cc_ctype_t *ctype; /**< the type its specifiers give */
    int complete;            /**< its enumerator list has been seen */
} enum_tag_entry_t;

/** A GNU local label (`__label__`). */
typedef struct {
    char *name;        /**< its name in the source */
    char *unique_name; /**< the function-wide name it is given */
    int depth;         /**< the scope depth it was declared at */
} local_label_entry_t;

/** The parser's state. */
typedef struct {
    cc_lexer_t lx;
    cc_token_t tok;
    cc_diag_t *diag;
    cc_translation_unit_t *tu;
    typedef_entry_t *typedefs;
    size_t typedef_count;
    size_t typedef_cap;
    var_entry_t *vars;
    size_t var_count;
    size_t var_cap;
    cc_struct_def_t *structs;
    size_t struct_count;
    size_t struct_cap;
    int is_probe; /**< a throwaway copy with its own struct table: records nothing for the interned types */
    enum_const_entry_t *enum_consts;
    size_t enum_const_count;
    size_t enum_const_cap;
    enum_tag_entry_t *enum_tags;
    size_t enum_tag_count;
    size_t enum_tag_cap;
    local_label_entry_t *local_labels;
    size_t local_label_count;
    size_t local_label_cap;
    size_t local_label_unique;
    cc_function_t *hoisted_funcs;
    size_t hoisted_func_count;
    size_t hoisted_func_cap;
    int scope_depth;
    size_t file_literal_count; /**< file-scope compound literals hoisted into globals so far */
    int parse_depth;
    int last_storage;
    const cc_ctype_t *last_spec_ctype;  /**< the type the last declaration specifiers named */
    long last_spec_align; /**< the alignment of the typedef name they used (typedef_entry_t.align), or 0 */
} parser_t;

/** A constant expression's value, as the parser evaluates it. */
typedef cc_const_t parser_const_value_t;

/* parser.c */

/** Report error @p msg at the parser's current token. */
void parser_error(parser_t *p, const char *msg);

/**
 * The member @p name (of @p len bytes) of structure @p sid, looked for in
 * its anonymous members too.
 *
 * @return the member, or NULL if it has none of that name
 */
const cc_struct_member_t *struct_member_find_n(const parser_t *p, int sid, const char *name, size_t len);

/**
 * The type of function @p f as declared, for the parser.
 *
 * @return the type, or NULL where its declaration did not record it
 */
const cc_ctype_t *parser_function_ctype(const cc_function_t *f);

/**
 * The size of type @p t as the parser knows it so far: its structures
 * are the parser's, which the translation unit receives only at the end.
 *
 * @return the size in bytes, or -1 if not known
 */
long parser_ct_size(const parser_t *p, const cc_ctype_t *t);

/**
 * The bound string literal @p e gives an array of unknown bound: its
 * code units, the terminating null included.
 */
long parser_string_literal_array_bound(const cc_expr_t *e);

/**
 * The bound an initializer list gives an array of unknown bound whose
 * elements have type @p elem: a lone string literal's length for an
 * array of characters, the structures a list of brace-elided members
 * makes up, else the number of items.
 *
 * @param p     the parser
 * @param elem  the element type
 * @param init  the CC_EXPR_INIT_LIST
 * @return      the bound, or 0 if it cannot be told
 */
long parser_init_list_bound_ct(parser_t *p, const cc_ctype_t *elem, const cc_expr_t *init);

/* parse_sema.c */

/**
 * The innermost variable in scope named by the @p len bytes at @p name.
 *
 * @return its index in the parser's variable table, or -1 if none
 */
int var_find_visible_n(const parser_t *p, const char *name, size_t len);

/**
 * Declare a variable in the current scope, or redeclare one already
 * declared in it.
 *
 * @param p      the parser
 * @param name   its name; an empty or NULL name declares nothing
 * @param ctype  its type
 * @return       0, or -1 when out of memory
 */
int var_push(parser_t *p, const char *name, const cc_ctype_t *ctype);

/**
 * Check a constexpr object just declared in the current scope against
 * C23 6.7.1's constraints, and record its value so its name is an integer
 * constant expression (C23 6.6p7).  It needs an initializer; an integer
 * one's is an integer constant expression its type can represent, and a
 * pointer's is null.  Only an integer object no wider than 64 bits gets a
 * value recorded.
 *
 * @param p      the parser
 * @param name   its name
 * @param ctype  its type
 * @param init   its initializer; may be NULL
 * @return       0, or -1 with the parser's diagnostic set
 */
int parser_note_constexpr(parser_t *p, const char *name, const cc_ctype_t *ctype, const cc_expr_t *init);

/**
 * Record on member access @p m (`a.x`, `a.b.x`) the value it names when
 * it is a named constant (C23 6.6p7): an integer member, not a bit-field,
 * of a constexpr structure, its value read from the structure's
 * initializer -- so that the access is an integer constant expression.
 * Anything else is left as it is.
 *
 * @param p  the parser
 * @param m  the CC_EXPR_MEMBER just built
 */
void parser_note_constexpr_member(parser_t *p, cc_expr_t *m);

/** End the scopes deeper than @p depth: forget the variables declared in them. */
void var_pop_to_depth(parser_t *p, int depth);

/** Free the parser's variable table. */
void parser_free_vars(parser_t *p);

/**
 * The type of expression @p e as far as the parser can tell before sema
 * checks it -- what `typeof (e)` names: a named object's or function's
 * declared type, a literal's, a cast's, and the types C gives the
 * operators applied to them.
 *
 * @param p  the parser
 * @param e  the expression
 * @return   the type, or NULL where the parser cannot tell it
 */
const cc_ctype_t *parser_expr_ctype(parser_t *p, const cc_expr_t *e);

/**
 * The alignment `_Alignof` or `__alignof__` gives expression @p e, as gcc
 * gives it: a named object's declared alignment (`aligned`, `_Alignas`, an
 * aligned typedef) where above its type's, a member's as its structure
 * placed it; else @p align, its type's.
 *
 * @param p      the parser
 * @param e      the operand
 * @param align  the alignment of its type
 * @return       the alignment
 */
long parser_object_align(parser_t *p, const cc_expr_t *e, long align);

/**
 * Evaluate an integer constant expression in the parser's context.
 *
 * @param p    the parser
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 */
int eval_const_expr_value(parser_t *p, const cc_expr_t *e, parser_const_value_t *out);

/**
 * Evaluate an array bound's integer constant expression to a long.
 *
 * @param p    the parser
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression or
 *             its value does not fit a long
 */
int eval_const_array_bound_expr(parser_t *p, const cc_expr_t *e, long *out);

/**
 * Evaluate a case label's integer constant expression to a long long, a
 * width every case value fits on any host (a host long may be 32 bits).
 * An unsigned value keeps its bits: 0xffffffffu stays 4294967295.
 *
 * @param p    the parser
 * @param e    the expression
 * @param out  receives the value
 * @return     0, or -1 if @p e is not an integer constant expression
 */
int eval_const_case_value(parser_t *p, const cc_expr_t *e, long long *out);

/**
 * Put the initializer list at @p slot for an object of type @p t in the one
 * form the rest of the compiler reads (C11 6.7.9): every designator
 * resolved, every elided brace put back, every aggregate a list of its
 * parts in order -- a gap before the last part given holding a zero -- and
 * a union's list its one member's initializer, designated where that is
 * not the first member.  Nothing is changed where @p t is not an array or
 * a structure, or the list is one string for an array of characters.
 *
 * @param p        the parser
 * @param t        the object's type
 * @param slot     the initializer; replaced by its canonical form
 * @param out_len  receives an array's length as initialized (the bound an
 *                 array of unknown bound takes), or 0; may be NULL
 * @return         0, or -1 on error (diagnosed)
 */
int parser_canon_initializer(parser_t *p, const cc_ctype_t *t, cc_expr_t **slot, long *out_len);

#endif
