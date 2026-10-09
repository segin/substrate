/*
 * ld_resolve.c -- what is said about symbols when asked: --trace,
 * --trace-symbol, --warn-common.  The table of the link's symbols, and
 * who defines and refers to what, is ld_symtab.c.
 */

#include "ld.h"

static int trace_symbol_requested(const ld_ctx_t *ctx, const char *name) {
    size_t i;

    if (ctx->opt.trace_symbols.count == 0 || name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < ctx->opt.trace_symbols.count; ++i) {
        if (strcmp(ctx->opt.trace_symbols.items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

void emit_trace_inputs(const ld_ctx_t *ctx, const objvec_t *inputs) {
    size_t i;

    if (!ctx->opt.trace_inputs) {
        return;
    }
    for (i = 0; i < inputs->count; ++i) {
        fprintf(stderr, "ld: trace: input %s\n", inputs->names[i]);
    }
}

void emit_trace_symbols(const ld_ctx_t *ctx, const objvec_t *inputs) {
    size_t i;

    if (ctx->opt.trace_symbols.count == 0) {
        return;
    }
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;

            if (sym == NULL) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (!trace_symbol_requested(ctx, name)) {
                continue;
            }
            if (elf_symbol_shndx(sym) != SHN_UNDEF) {
                fprintf(stderr, "ld: trace-symbol: %s defined in %s\n", name, inputs->names[i]);
            } else {
                fprintf(stderr, "ld: trace-symbol: %s referenced by %s\n", name, inputs->names[i]);
            }
        }
    }
}

int emit_common_symbol_warnings(ld_ctx_t *ctx, const objvec_t *inputs) {
    size_t i;

    if (!ctx->opt.warn_common) {
        return 0;
    }
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;
            uint8_t bind;

            if (sym == NULL) {
                continue;
            }
            if (elf_symbol_shndx(sym) != SHN_COMMON) {
                continue;
            }
            bind = elf_symbol_bind(sym);
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (name == NULL || name[0] == '\0') {
                continue;
            }
            if (ld_warn(ctx, "common symbol `%s` in %s", name, inputs->names[i]) != 0) {
                return -1;
            }
        }
    }
    return 0;
}
