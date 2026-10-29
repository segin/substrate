/*
 * options.c: the settings of the compilation in progress.
 */

#include "cc_options.h"

#include <stddef.h>

static cc_options_t g_current = {
    {"gnu17", 2017, 1}, CC_TARGET_X86_64, 0, 0, 0, 0, 0, 0, 0, 0, 0, -1, -1, 6, 1, 1, 1, CC_FPMATH_AUTO, 0, 0, NULL, 0, 0, 0, 1, 0, 0, 0, 0, 0,
};

/**
 * Fill @p o with the defaults.
 *
 * @param o  the options to reset
 */
void cc_options_init(cc_options_t *o) {
    cc_lang_from_std(NULL, &o->lang);
    o->target = CC_TARGET_X86_64;
    o->code16 = 0;
    o->opt_level = 0;
    o->debug = 0;
    o->pic = 0;
    o->pie = 0;
    o->warn_all = 0;
    o->warn_error = 0;
    o->pedantic = 0;
    o->pedantic_errors = 0;
    o->gnu89_inline = -1;
    o->implicit_funcdecl = -1;
    o->i386_isa_level = 6;
    o->i386_sse2 = 1;
    o->i386_mmx = 1;
    o->i386_sse = 1;
    o->i386_fpmath = CC_FPMATH_AUTO;
    o->i386_regparm = 0;
    o->i386_reg_struct_return = 0;
    o->dump_ir = NULL;
    o->dump_ir_all = 0;
    o->trivial_auto_var_init_zero = 0;
    o->trivial_auto_var_init_pattern = 0;
    o->unwind_tables = 1;
    o->i386_stack_boundary = 0;
    o->cmodel_kernel = 0;
    o->omit_frame_pointer = 0;
    o->no_sse = 0;
    o->no_jump_tables = 0;
}

/**
 * Make @p o the settings of the compilation in progress.
 *
 * @param o  the settings
 */
void cc_options_select(const cc_options_t *o) {
    g_current = *o;
    if (g_current.i386_isa_level < 3) {
        g_current.i386_isa_level = 3;
    } else if (g_current.i386_isa_level > 7) {
        g_current.i386_isa_level = 7;
    }
    cc_lang_select(&g_current.lang);
    cc_target_select(g_current.target);
}

/**
 * The settings of the compilation in progress.
 *
 * @return the settings
 */
const cc_options_t *cc_options_current(void) {
    return &g_current;
}

/**
 * Whether GNU89 `inline` semantics apply.
 *
 * @return 1 if they do, else 0
 */
int cc_options_gnu89_inline(void) {
    if (g_current.gnu89_inline >= 0) {
        return g_current.gnu89_inline;
    }
    return cc_lang_gnu() && !cc_lang_at_least(1999);
}

/**
 * Whether a call to an undeclared function declares it implicitly.
 *
 * @return 1 if it does, else 0
 */
int cc_options_implicit_funcdecl(void) {
    if (g_current.implicit_funcdecl >= 0) {
        return g_current.implicit_funcdecl;
    }
    return cc_lang_gnu() || !cc_lang_at_least(1999);
}
