#include "as_elf_emit.h"

#include "elfobj.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef STV_DEFAULT
#define STV_DEFAULT 0
#define STV_INTERNAL 1
#define STV_HIDDEN 2
#define STV_PROTECTED 3
#endif

#ifndef R_ARM_ABS32
#define R_ARM_ABS32 2
#endif
#ifndef R_AARCH64_ABS64
#define R_AARCH64_ABS64 257
#endif

typedef struct {
    char *name;
    elf_symbol_t *sym;
} emit_sym_t;

typedef struct {
    const as_elf_cfg_t *cfg;
    const as_parse_result_t *parsed;
    elfobj_t *obj;
    elf_section_t *text_sec;
    elf_section_t *data_sec;
    emit_sym_t *sym_map;
    size_t sym_count;
    char *errbuf;
    size_t errbuf_sz;
} emit_ctx_t;

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} bytebuf_t;

static void set_err(emit_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

static int bytebuf_reserve(bytebuf_t *b, size_t extra) {
    unsigned char *next;

    if (b->len + extra <= b->cap) {
        return 0;
    }
    {
        size_t ncap = b->cap == 0 ? 256 : b->cap;
        while (ncap < b->len + extra) {
            ncap *= 2;
        }
        next = (unsigned char *)realloc(b->data, ncap);
        if (next == NULL) {
            return -1;
        }
        b->data = next;
        b->cap = ncap;
    }
    return 0;
}

static int bytebuf_append(bytebuf_t *b, const void *p, size_t n) {
    if (bytebuf_reserve(b, n) != 0) {
        return -1;
    }
    memcpy(b->data + b->len, p, n);
    b->len += n;
    return 0;
}

static int bytebuf_append_zeros(bytebuf_t *b, size_t n) {
    if (bytebuf_reserve(b, n) != 0) {
        return -1;
    }
    memset(b->data + b->len, 0, n);
    b->len += n;
    return 0;
}

static int bytebuf_append_u64_le(bytebuf_t *b, uint64_t v, unsigned width) {
    unsigned i;

    if (width < 1 || width > 8) {
        return -1;
    }
    if (bytebuf_reserve(b, width) != 0) {
        return -1;
    }
    for (i = 0; i < width; ++i) {
        b->data[b->len + i] = (unsigned char)((v >> (i * 8)) & 0xffu);
    }
    b->len += width;
    return 0;
}

static uint8_t map_bind(as_sym_bind_t bind) {
    switch (bind) {
    case AS_SYM_BIND_GLOBAL:
        return STB_GLOBAL;
    case AS_SYM_BIND_WEAK:
        return STB_WEAK;
    default:
        return STB_LOCAL;
    }
}

static uint8_t map_type(as_sym_type_t type) {
    switch (type) {
    case AS_SYM_TYPE_FUNCTION:
        return STT_FUNC;
    case AS_SYM_TYPE_OBJECT:
    case AS_SYM_TYPE_COMMON:
        return STT_OBJECT;
    case AS_SYM_TYPE_TLS_OBJECT:
        return STT_TLS;
    default:
        return STT_NOTYPE;
    }
}

static uint8_t map_vis(as_sym_visibility_t vis) {
    switch (vis) {
    case AS_SYM_VIS_INTERNAL:
        return STV_INTERNAL;
    case AS_SYM_VIS_HIDDEN:
        return STV_HIDDEN;
    case AS_SYM_VIS_PROTECTED:
        return STV_PROTECTED;
    default:
        return STV_DEFAULT;
    }
}

static uint32_t reloc_type_for_machine(unsigned machine) {
    switch (machine) {
    case EM_386:
        return R_386_32;
    case EM_X86_64:
        return R_X86_64_64;
    case EM_ARM:
        return R_ARM_ABS32;
    case EM_AARCH64:
        return R_AARCH64_ABS64;
    default:
        return R_386_32;
    }
}

static elf_symbol_t *find_emit_symbol(emit_ctx_t *ctx, const char *name) {
    size_t i;

    for (i = 0; i < ctx->sym_count; ++i) {
        if (strcmp(ctx->sym_map[i].name, name) == 0) {
            return ctx->sym_map[i].sym;
        }
    }
    return NULL;
}

static int append_emit_symbol(emit_ctx_t *ctx, const char *name, elf_symbol_t *sym) {
    emit_sym_t *next;

    next = (emit_sym_t *)realloc(ctx->sym_map, (ctx->sym_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    ctx->sym_map = next;
    ctx->sym_map[ctx->sym_count].name = xstrdup(name);
    ctx->sym_map[ctx->sym_count].sym = sym;
    if (ctx->sym_map[ctx->sym_count].name == NULL) {
        return -1;
    }
    ctx->sym_count++;
    return 0;
}

static int emit_data_program(emit_ctx_t *ctx, const as_data_program_t *data) {
    bytebuf_t buf;
    size_t i;

    memset(&buf, 0, sizeof(buf));

    for (i = 0; i < data->count; ++i) {
        const as_data_op_t *op = &data->items[i];
        size_t j;

        switch (op->kind) {
        case AS_DATA_INT:
            for (j = 0; j < op->u.ints.count; ++j) {
                if (bytebuf_append_u64_le(&buf, (uint64_t)op->u.ints.values[j], op->u.ints.width) != 0) {
                    free(buf.data);
                    return -1;
                }
            }
            break;
        case AS_DATA_FLOAT:
            for (j = 0; j < op->u.floats.count; ++j) {
                if (op->u.floats.is_double) {
                    double d = op->u.floats.values[j];
                    if (bytebuf_append(&buf, &d, sizeof(d)) != 0) {
                        free(buf.data);
                        return -1;
                    }
                } else {
                    float f = (float)op->u.floats.values[j];
                    if (bytebuf_append(&buf, &f, sizeof(f)) != 0) {
                        free(buf.data);
                        return -1;
                    }
                }
            }
            break;
        case AS_DATA_STRING:
            if (bytebuf_append(&buf, op->u.str.bytes, op->u.str.len) != 0) {
                free(buf.data);
                return -1;
            }
            if (op->u.str.nul_terminated && bytebuf_append_zeros(&buf, 1) != 0) {
                free(buf.data);
                return -1;
            }
            break;
        case AS_DATA_ZERO:
            if (bytebuf_append_zeros(&buf, (size_t)op->u.zero.count) != 0) {
                free(buf.data);
                return -1;
            }
            break;
        case AS_DATA_FILL:
            for (j = 0; j < (size_t)op->u.fill.repeat; ++j) {
                if (bytebuf_append_u64_le(&buf, op->u.fill.value, (unsigned)op->u.fill.size) != 0) {
                    free(buf.data);
                    return -1;
                }
            }
            break;
        case AS_DATA_ORG:
            if (op->u.org.offset > buf.len && bytebuf_append_zeros(&buf, (size_t)(op->u.org.offset - buf.len)) != 0) {
                free(buf.data);
                return -1;
            }
            break;
        case AS_DATA_INCBIN: {
            FILE *fp;
            unsigned char tmp[512];
            size_t nread;
            unsigned long long skip_left = op->u.incbin.skip;
            unsigned long long take_left = op->u.incbin.has_count ? op->u.incbin.count : ~0ULL;

            fp = fopen(op->u.incbin.path, "rb");
            if (fp == NULL) {
                free(buf.data);
                return -1;
            }
            while (skip_left > 0) {
                size_t chunk = skip_left > sizeof(tmp) ? sizeof(tmp) : (size_t)skip_left;
                nread = fread(tmp, 1, chunk, fp);
                if (nread == 0) {
                    break;
                }
                skip_left -= nread;
            }
            while (take_left > 0) {
                size_t want = take_left > sizeof(tmp) ? sizeof(tmp) : (size_t)take_left;
                nread = fread(tmp, 1, want, fp);
                if (nread == 0) {
                    break;
                }
                if (bytebuf_append(&buf, tmp, nread) != 0) {
                    fclose(fp);
                    free(buf.data);
                    return -1;
                }
                if (op->u.incbin.has_count) {
                    take_left -= nread;
                }
            }
            fclose(fp);
            break;
        }
        default:
            break;
        }
    }

    if (ctx->data_sec != NULL && elf_section_set_data(ctx->data_sec, buf.data, buf.len) != ELF_OK) {
        free(buf.data);
        return -1;
    }
    free(buf.data);
    return 0;
}

static int collect_directive_presence(const as_parse_result_t *parsed, int *has_file_loc, int *has_cfi) {
    size_t i;

    *has_file_loc = 0;
    *has_cfi = 0;
    for (i = 0; i < parsed->count; ++i) {
        const as_stmt_t *st = &parsed->items[i];
        if (st->kind != AS_STMT_DIRECTIVE) {
            continue;
        }
        if (strcmp(st->u.directive.name, ".file") == 0 || strcmp(st->u.directive.name, ".loc") == 0) {
            *has_file_loc = 1;
        }
        if (strncmp(st->u.directive.name, ".cfi_", 5) == 0) {
            *has_cfi = 1;
        }
    }
    return 0;
}

static int ensure_section_exists(emit_ctx_t *ctx, const char *name, uint32_t type, uint64_t flags,
                                 uint64_t align, const void *data, size_t data_sz) {
    elf_section_t *sec = elf_find_section(ctx->obj, name);
    if (sec == NULL) {
        sec = elf_add_section(ctx->obj, name, type, flags);
        if (sec == NULL) {
            return -1;
        }
    }
    if (elf_section_set_align(sec, align) != ELF_OK) {
        return -1;
    }
    if (data != NULL && data_sz > 0 && elf_section_set_data(sec, data, data_sz) != ELF_OK) {
        return -1;
    }
    return 0;
}

static int emit_symbols(emit_ctx_t *ctx, const as_symtab_t *symtab) {
    size_t i;

    for (i = 0; i < symtab->count; ++i) {
        const as_symbol_t *s = &symtab->items[i];
        elf_symbol_t *esym;

        esym = elf_add_symbol(ctx->obj, s->name, 0, s->size, map_bind(s->bind), map_type(s->type));
        if (esym == NULL) {
            return -1;
        }
        if (elf_symbol_set_visibility(esym, map_vis(s->visibility)) != ELF_OK) {
            return -1;
        }
        if (s->version != NULL && elf_symbol_set_version(esym, 1) != ELF_OK) {
            return -1;
        }
        if (s->is_common) {
            if (elf_symbol_set_shndx(esym, SHN_COMMON) != ELF_OK) {
                return -1;
            }
        } else if (s->defined) {
            if (ctx->text_sec != NULL && elf_symbol_define(esym, ctx->text_sec, 0) != ELF_OK) {
                return -1;
            }
        }

        if (append_emit_symbol(ctx, s->name, esym) != 0) {
            return -1;
        }
    }

    return 0;
}

static int add_reloc_for_symbol(emit_ctx_t *ctx, const char *name, uint64_t *offset_inout) {
    elf_symbol_t *sym;
    uint32_t rtype;

    sym = find_emit_symbol(ctx, name);
    if (sym == NULL) {
        return 0;
    }
    if (ctx->text_sec == NULL) {
        return 0;
    }

    rtype = reloc_type_for_machine(ctx->cfg != NULL ? ctx->cfg->machine : EM_386);
    if (elf_add_relocation(ctx->text_sec, *offset_inout, sym, rtype, 0) != ELF_OK) {
        return -1;
    }

    *offset_inout += (ctx->cfg != NULL && ctx->cfg->is_64) ? 8 : 4;
    return 0;
}

static int walk_expr_relocs(emit_ctx_t *ctx, const as_expr_t *e, uint64_t *off) {
    if (e == NULL) {
        return 0;
    }
    if (e->kind == AS_EXPR_SYMBOL && e->symbol != NULL) {
        if (add_reloc_for_symbol(ctx, e->symbol, off) != 0) {
            return -1;
        }
    }
    if (walk_expr_relocs(ctx, e->lhs, off) != 0) {
        return -1;
    }
    if (walk_expr_relocs(ctx, e->rhs, off) != 0) {
        return -1;
    }
    return 0;
}

static int emit_relocations(emit_ctx_t *ctx) {
    size_t i;
    uint64_t off = 0;

    for (i = 0; i < ctx->parsed->count; ++i) {
        const as_stmt_t *st = &ctx->parsed->items[i];
        size_t j;

        if (st->kind != AS_STMT_INSTRUCTION) {
            continue;
        }
        for (j = 0; j < st->u.instr.operand_count; ++j) {
            const as_operand_t *op = &st->u.instr.operands[j];
            if (op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) {
                if (walk_expr_relocs(ctx, op->u.expr, &off) != 0) {
                    return -1;
                }
            } else if (op->kind == AS_OPERAND_MEMORY) {
                if (walk_expr_relocs(ctx, op->u.mem.disp, &off) != 0) {
                    return -1;
                }
            }
        }
    }

    return 0;
}

int as_elf_emit_file(const as_parse_result_t *parsed,
                     const as_section_state_t *sections,
                     const as_symtab_t *symtab,
                     const as_data_program_t *data,
                     const as_elf_cfg_t *cfg,
                     const char *out_path,
                     char *errbuf,
                     size_t errbuf_sz) {
    emit_ctx_t ctx;
    elfobj_class_t cls;
    size_t i;
    int has_file_loc = 0;
    int has_cfi = 0;

    if (parsed == NULL || sections == NULL || symtab == NULL || data == NULL || cfg == NULL || out_path == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = cfg;
    ctx.parsed = parsed;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    cls = cfg->is_64 ? ELFOBJ_CLASS_64 : ELFOBJ_CLASS_32;
    ctx.obj = elf_create(ET_REL, (uint16_t)cfg->machine, cls, ELFOBJ_ENDIAN_LE);
    if (ctx.obj == NULL) {
        set_err(&ctx, "elf_create failed");
        return -1;
    }

    for (i = 0; i < sections->count; ++i) {
        const as_section_t *s = &sections->items[i];
        elf_section_t *es = elf_add_section(ctx.obj, s->name, s->type, s->flags);
        if (es == NULL) {
            set_err(&ctx, "failed to add section %s", s->name);
            goto fail;
        }
        if (elf_section_set_align(es, s->align > 0 ? s->align : 1) != ELF_OK) {
            set_err(&ctx, "failed to set align on %s", s->name);
            goto fail;
        }
        if (s->group != NULL) {
            (void)elf_section_set_group(es, 1, s->comdat);
        }

        if (strcmp(s->name, ".text") == 0 && s->subsection == 0) {
            static unsigned char text_pad[512];
            ctx.text_sec = es;
            if (elf_section_set_data(es, text_pad, sizeof(text_pad)) != ELF_OK) {
                set_err(&ctx, "failed to seed .text data");
                goto fail;
            }
        }
        if (strcmp(s->name, ".data") == 0 && s->subsection == 0) {
            ctx.data_sec = es;
        }
    }

    if (ctx.data_sec == NULL) {
        ctx.data_sec = elf_add_section(ctx.obj, ".data", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
        if (ctx.data_sec == NULL || elf_section_set_align(ctx.data_sec, 4) != ELF_OK) {
            set_err(&ctx, "failed to create .data");
            goto fail;
        }
    }
    if (ctx.text_sec == NULL) {
        static unsigned char text_pad2[512];
        ctx.text_sec = elf_add_section(ctx.obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
        if (ctx.text_sec == NULL || elf_section_set_align(ctx.text_sec, 16) != ELF_OK ||
            elf_section_set_data(ctx.text_sec, text_pad2, sizeof(text_pad2)) != ELF_OK) {
            set_err(&ctx, "failed to create .text");
            goto fail;
        }
    }

    if (emit_data_program(&ctx, data) != 0) {
        set_err(&ctx, "failed to emit data program");
        goto fail;
    }

    if (emit_symbols(&ctx, symtab) != 0) {
        set_err(&ctx, "failed to emit symbols");
        goto fail;
    }

    if (emit_relocations(&ctx) != 0) {
        set_err(&ctx, "failed to emit relocations");
        goto fail;
    }

    if (ensure_section_exists(&ctx, ".note.GNU-stack", SHT_PROGBITS, 0, 1, NULL, 0) != 0) {
        set_err(&ctx, "failed to emit .note.GNU-stack");
        goto fail;
    }

    if (cfg->machine == EM_X86_64 && cfg->x86_64_isa_level >= 2) {
        static const unsigned char gnu_prop[] = {
            4, 0, 0, 0, 16, 0, 0, 0, 5, 0, 0, 0,
            'G', 'N', 'U', 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
            0, 0, 0, 0,
        };
        if (ensure_section_exists(&ctx, ".note.gnu.property", SHT_NOTE, SHF_ALLOC, 4, gnu_prop, sizeof(gnu_prop)) != 0) {
            set_err(&ctx, "failed to emit .note.gnu.property");
            goto fail;
        }
    }

    collect_directive_presence(parsed, &has_file_loc, &has_cfi);
    if (has_file_loc) {
        static const unsigned char dbg_line_stub[] = {0x00, 0x00, 0x00, 0x00, 0x02, 0x00};
        if (ensure_section_exists(&ctx, ".debug_line", SHT_PROGBITS, 0, 1, dbg_line_stub, sizeof(dbg_line_stub)) != 0) {
            set_err(&ctx, "failed to emit .debug_line");
            goto fail;
        }
    }
    if (has_cfi) {
        static const unsigned char eh_stub[] = {0x14, 0x00, 0x00, 0x00};
        if (ensure_section_exists(&ctx, ".eh_frame", SHT_PROGBITS, SHF_ALLOC, 4, eh_stub, sizeof(eh_stub)) != 0 ||
            ensure_section_exists(&ctx, ".eh_frame_hdr", SHT_PROGBITS, SHF_ALLOC, 4, eh_stub, sizeof(eh_stub)) != 0) {
            set_err(&ctx, "failed to emit .eh_frame/.eh_frame_hdr");
            goto fail;
        }
    }

    if (elf_finalize(ctx.obj) != ELF_OK) {
        set_err(&ctx, "elf_finalize failed");
        goto fail;
    }
    if (elf_write_file(ctx.obj, out_path) != ELF_OK) {
        set_err(&ctx, "elf_write_file failed: %s", out_path);
        goto fail;
    }

    for (i = 0; i < ctx.sym_count; ++i) {
        free(ctx.sym_map[i].name);
    }
    free(ctx.sym_map);
    elf_close(ctx.obj);
    return 0;

fail:
    for (i = 0; i < ctx.sym_count; ++i) {
        free(ctx.sym_map[i].name);
    }
    free(ctx.sym_map);
    elf_close(ctx.obj);
    return -1;
}
