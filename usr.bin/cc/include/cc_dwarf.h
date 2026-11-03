#ifndef CC_DWARF_H
#define CC_DWARF_H

/*
 * DWARF debug information for -g: the compile unit, its types, its
 * functions and variables, in .debug_info, .debug_abbrev, .debug_str and
 * (for functions in more than one section) .debug_rnglists or
 * .debug_ranges, written as assembler directives the assembler turns
 * into bytes and relocations.  The line table (.debug_line) stays the
 * assembler's, made of the .file and .loc directives the code carries.
 *
 * The emitter of a module calls cc_dwarf_begin() first, the target's
 * emitter tells it about each function it writes -- where it starts and
 * ends, and where the frame puts each variable's stack slot -- and
 * cc_dwarf_finish() writes the sections at the end.  What is described
 * comes from the translation unit the module was lowered from
 * (cc_ssa_module_t.src_tu), types as their declarations spelled them
 * (cc_dtype.h).
 */

#include <stdio.h>

#include "cc_frontend.h"
#include "cc_ssa.h"

/**
 * Under -g, before the middle passes inline anything: remember which
 * functions each function calls, and the symbols it names.  The
 * functions inlined into the code
 * written -- those its functions call, and theirs, that are not written
 * themselves -- are then described as gcc describes them, as abstract
 * instances (DW_AT_inline), with their parameters and variables: the
 * types those use are the unit's too (Linux's tracepoint records, whose
 * only use is in an always_inline function, are in vmlinux's BTF so).
 *
 * @param m  the module, lowered and legalized
 * @return   0, or -1 when out of memory
 */
int cc_dwarf_note_calls(const cc_ssa_module_t *m);

/**
 * Start the debug information of a module: the source file's line-table
 * entries and the label the CU's code range starts at.
 *
 * @param fp        the assembly being written
 * @param m         the module, with its src_tu
 * @param src_path  the source file's name
 * @param version   the DWARF version, 4 or 5
 * @return          0, or -1 when out of memory
 */
int cc_dwarf_begin(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int version);

/**
 * Whether debug information is being written (cc_dwarf_begin() was called
 * and cc_dwarf_finish() not yet).
 *
 * @return 1 or 0
 */
int cc_dwarf_active(void);

/**
 * A function's code starts here: print the label its DW_AT_low_pc names,
 * right after the function's own.
 *
 * @param fp  the assembly being written
 * @param f   the function
 */
void cc_dwarf_func_begin(FILE *fp, const cc_ssa_function_t *f);

/**
 * Stack allocation @p value of the function being written lives at
 * @p cfa_off from the canonical frame address (the CFA its CFI
 * describes), for the whole of the function's body.
 *
 * @param value    the STACKALLOC's value
 * @param cfa_off  its offset from the CFA
 */
void cc_dwarf_func_slot(int value, long cfa_off);

/**
 * A function's code ends here: print the label its DW_AT_high_pc names,
 * and record the function, with the slots cc_dwarf_func_slot() gave.
 *
 * @param fp       the assembly being written
 * @param f        the function
 * @param section  the section its code is in
 */
void cc_dwarf_func_end(FILE *fp, const cc_ssa_function_t *f, const char *section);

/**
 * The line-table file number of a file of the translation unit, its .file
 * directive printed the first time it is asked for (the source file is 1).
 *
 * @param fp        the assembly being written
 * @param src_file  one more than the file's index in the unit's files; 0 for the source file
 * @return          the number
 */
unsigned cc_dwarf_file_number(FILE *fp, int src_file);

/**
 * Write the debug sections and forget the module.
 *
 * @param fp    the assembly being written
 * @param diag  receives the error
 * @return      0, or -1 when out of memory
 */
int cc_dwarf_finish(FILE *fp, cc_diag_t *diag);

/**
 * Forget the module without writing anything: emission failed.
 */
void cc_dwarf_abort(void);

#endif
