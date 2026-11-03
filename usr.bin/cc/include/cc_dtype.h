#ifndef CC_DTYPE_H
#define CC_DTYPE_H

/*
 * Types as a declaration spelled them, for debug information.
 *
 * The compiler's own types (cc_ctype.h) are canonical: `u32` and
 * `unsigned int` are one node, and every enumerated type is the one
 * CC_CT_ENUM.  That is what C's type rules want, and what code generation
 * needs; a debugger and pahole want more -- the typedef names and the
 * enumeration a declaration used, as gcc records them in DWARF.
 *
 * A debug type is a graph of its own beside the canonical one: a typedef
 * name, an enumeration, a qualifier on either, and the pointers, arrays
 * and functions built on them.  Each node knows the canonical type it
 * stands for (ct), so a declaration can carry both and the two can never
 * disagree: where no sugar is involved a debug type is just a leaf
 * holding the canonical type.  Nodes are interned like the canonical
 * ones, so equal pointers mean the same spelling.  The parser builds them
 * beside the canonical types; nothing but debug information reads them.
 */

#include <stddef.h>
#include <stdint.h>

#include "cc_ctype.h"

/** What a debug type node is. */
typedef enum {
    CC_DT_PLAIN = 0, /**< no sugar: just ct */
    CC_DT_TYPEDEF,   /**< a typedef name: name, standing for of */
    CC_DT_ENUM,      /**< an enumeration: id indexes the enumeration definitions (cc_dt_enum_def()) */
    CC_DT_QUAL,      /**< qualifiers (quals) on a typedef name or an enumeration (of) */
    CC_DT_POINTER,   /**< pointer to of, the pointer itself qualified by quals */
    CC_DT_ARRAY,     /**< count elements of of; count < 0 when the bound is not known */
    CC_DT_FUNCTION   /**< function returning of, taking params */
} cc_dtype_kind_t;

typedef struct cc_dtype cc_dtype_t;

/** A debug type.  Never built directly: use the constructors below. */
struct cc_dtype {
    cc_dtype_kind_t kind;
    const cc_ctype_t *ct;           /**< the canonical type it stands for */
    const cc_dtype_t *of;           /**< the type named, qualified, pointed to, the element or the return type */
    unsigned quals;                 /**< QUAL, POINTER: CC_CQ_* of this level */
    const char *name;               /**< TYPEDEF: its name (interned) */
    int id;                         /**< ENUM: its definition */
    long count;                     /**< ARRAY: the bound; TYPEDEF: the alignment its `aligned` gives, or 0 */
    size_t param_count;             /**< FUNCTION: the number of parameters */
    const cc_dtype_t *const *params; /**< FUNCTION: their types */
    unsigned char variadic;         /**< FUNCTION: ends in `...` */
    unsigned char prototyped;       /**< FUNCTION: has a parameter list */
};

/** One enumerator of an enumeration definition. */
typedef struct {
    const char *name; /**< its name (owned by the definition table) */
    uint64_t bits;    /**< its value's bits */
} cc_dt_enumerator_t;

/** An enumeration as its definition declares it. */
typedef struct {
    const char *tag;              /**< its tag, or NULL (owned by the definition table) */
    cc_dt_enumerator_t *items;    /**< its enumerators, in order */
    size_t count;                 /**< how many */
    size_t cap;                   /**< room for */
    int has_negative;             /**< a value is negative */
    int complete;                 /**< its list has been seen */
    size_t line;                  /**< where its specifier is */
    int src_file;                 /**< one more than its file's index in the unit's files; 0 unknown */
} cc_dt_enum_def_t;

/**
 * Forget every debug type and enumeration definition: the pipeline's
 * cc_ct_reset() calls this once per translation unit.
 */
void cc_dt_reset(void);

/**
 * A debug type without sugar.
 *
 * @param ct  the canonical type
 * @return    the node, or NULL if @p ct is NULL or memory runs out
 */
const cc_dtype_t *cc_dt_plain(const cc_ctype_t *ct);

/**
 * A typedef name.
 *
 * @param name   its name
 * @param of     the type it names
 * @param align  the alignment its declaration's `aligned` gives, or 0
 * @return       the node, or NULL
 */
const cc_dtype_t *cc_dt_typedef(const char *name, const cc_dtype_t *of, long align);

/**
 * An enumeration.
 *
 * @param def  its definition (cc_dt_enum_define())
 * @param ct   the canonical type it is (CC_CT_ENUM, or a packed one's integer type)
 * @return     the node, or NULL
 */
const cc_dtype_t *cc_dt_enum(int def, const cc_ctype_t *ct);

/**
 * A debug type with qualifiers added, as cc_ct_qualified() adds them to
 * its canonical type.
 *
 * @param t      the type
 * @param quals  CC_CQ_* to add
 * @return       the node, or NULL
 */
const cc_dtype_t *cc_dt_qualified(const cc_dtype_t *t, unsigned quals);

/**
 * Pointer to a debug type.
 *
 * @param to     the pointee
 * @param quals  the pointer's own qualifiers
 * @return       the node, or NULL
 */
const cc_dtype_t *cc_dt_pointer(const cc_dtype_t *to, unsigned quals);

/**
 * Array of a debug type.
 *
 * @param of     the element type
 * @param count  the bound, or -1
 * @return       the node, or NULL
 */
const cc_dtype_t *cc_dt_array(const cc_dtype_t *of, long count);

/**
 * A function type.
 *
 * @param ret          the return type
 * @param params       the parameter types (adjusted, but with their own
 *                     qualifiers as written, which gcc's DWARF keeps and
 *                     the function type has not); copied
 * @param param_count  how many
 * @param variadic     it ends in `...`
 * @param prototyped   it has a parameter list
 * @return             the node, or NULL
 */
const cc_dtype_t *cc_dt_function(const cc_dtype_t *ret, const cc_dtype_t *const *params, size_t param_count,
                                 int variadic, int prototyped);

/**
 * The type a parameter of type @p t has, as cc_ct_decay() adjusts it: an
 * array a pointer to its element, a function a pointer to itself, the
 * qualifiers gone.
 *
 * @param t  the type
 * @return   the node, or NULL
 */
const cc_dtype_t *cc_dt_decay(const cc_dtype_t *t);

/**
 * The spelling @p t gives canonical type @p ct, as far as the two agree:
 * @p t itself when it stands for @p ct, else its sugar kept where the
 * structure of the two matches -- an array's bound a later declaration or
 * an initializer gave, an attribute's change -- and dropped where not.
 *
 * @param t   the spelling, or NULL
 * @param ct  the type the declaration has now
 * @return    a node standing for @p ct, or NULL if @p ct is NULL or memory runs out
 */
const cc_dtype_t *cc_dt_fit(const cc_dtype_t *t, const cc_ctype_t *ct);

/**
 * Start an enumeration definition.
 *
 * @param tag       its tag, or NULL
 * @param tag_len   the tag's length
 * @param line      where its specifier is
 * @param src_file  one more than its file's index; 0 unknown
 * @return          its index, or -1 when out of memory
 */
int cc_dt_enum_define(const char *tag, size_t tag_len, size_t line, int src_file);

/**
 * The enumeration definition a tag names: for a reference (`enum e x;`),
 * the one the tag has, or a new incomplete one the definition that comes
 * later fills (an enumeration a prototype or a member names before its
 * definition, as GNU C allows, is still that enumeration); for a
 * definition, that incomplete one, or a new one.
 *
 * @param tag       the tag
 * @param tag_len   its length
 * @param defining  the specifier defines it (its list follows)
 * @param line      where the specifier is
 * @param src_file  one more than its file's index; 0 unknown
 * @return          the definition's index, or -1 when out of memory
 */
int cc_dt_enum_tag(const char *tag, size_t tag_len, int defining, size_t line, int src_file);

/**
 * Add an enumerator to definition @p def.
 *
 * @param def        the definition
 * @param name       its name
 * @param name_len   the name's length
 * @param bits       its value's bits
 * @param negative   the value is negative
 * @return           0, or -1 when out of memory
 */
int cc_dt_enum_add(int def, const char *name, size_t name_len, uint64_t bits, int negative);

/**
 * Mark definition @p def complete: its enumerator list is closed.
 *
 * @param def  the definition
 */
void cc_dt_enum_complete(int def);

/**
 * An enumeration definition.
 *
 * @param def  its index
 * @return     the definition, or NULL for none
 */
const cc_dt_enum_def_t *cc_dt_enum_def(int def);

#endif
