#include <ctype.h>
#include "../../include/elfobj.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EM_X86_64
#define EM_X86_64 62
#endif

#ifndef R_X86_64_64
#define R_X86_64_64 1
#endif
#ifndef R_X86_64_PC32
#define R_X86_64_PC32 2
#endif
#ifndef R_X86_64_32
#define R_X86_64_32 10
#endif

typedef struct section section_t;
typedef struct symbol symbol_t;

struct section {
    char *name;
    uint32_t type;
    uint64_t flags;
    uint64_t align;
    unsigned char *data;
    size_t size;
    size_t cap;
    uint64_t bss_size;
    int is_bss;
    elf_section_t *out_sec;
};

struct symbol {
    char *name;
    uint8_t bind;
    uint8_t type;
    uint64_t value;
    uint64_t size;
    int defined;
    section_t *section;
    elf_symbol_t *out_sym;
};

typedef struct {
    section_t *section;
    uint64_t offset;
    symbol_t *symbol;
    uint32_t reloc_type;
    int64_t addend;
} fixup_t;

typedef struct {
    section_t *secs;
    size_t sec_count;
    size_t sec_cap;
    symbol_t *syms;
    size_t sym_count;
    size_t sym_cap;
    fixup_t *fixups;
    size_t fixup_count;
    size_t fixup_cap;
    section_t *cur;
    int mode64;
    int emit_debug;
    const char *in_path;
    const char *out_path;
} asm_ctx_t;

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s [-32|-64] [-g] [-I dir] [-D macro] [-march cpu] [-mtune cpu] "
            "[-o output] input.s\n",
            prog);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *p;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static char *trim(char *s) {
    char *e;
    while (*s != '\0' && isspace((unsigned char)*s)) {
        s++;
    }
    e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) {
        e--;
    }
    *e = '\0';
    return s;
}

static int is_ident_char(int c) {
    return isalnum(c) || c == '_' || c == '.' || c == '$';
}

static int push_section(asm_ctx_t *ctx, section_t *sec) {
    section_t *next;
    if (ctx->sec_count == ctx->sec_cap) {
        size_t ncap = ctx->sec_cap == 0 ? 8 : ctx->sec_cap * 2;
        next = (section_t *)realloc(ctx->secs, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ctx->secs = next;
        ctx->sec_cap = ncap;
    }
    ctx->secs[ctx->sec_count++] = *sec;
    return 0;
}

static int push_symbol(asm_ctx_t *ctx, symbol_t *sym) {
    symbol_t *next;
    if (ctx->sym_count == ctx->sym_cap) {
        size_t ncap = ctx->sym_cap == 0 ? 16 : ctx->sym_cap * 2;
        next = (symbol_t *)realloc(ctx->syms, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ctx->syms = next;
        ctx->sym_cap = ncap;
    }
    ctx->syms[ctx->sym_count++] = *sym;
    return 0;
}

static int push_fixup(asm_ctx_t *ctx, fixup_t *fx) {
    fixup_t *next;
    if (ctx->fixup_count == ctx->fixup_cap) {
        size_t ncap = ctx->fixup_cap == 0 ? 16 : ctx->fixup_cap * 2;
        next = (fixup_t *)realloc(ctx->fixups, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        ctx->fixups = next;
        ctx->fixup_cap = ncap;
    }
    ctx->fixups[ctx->fixup_count++] = *fx;
    return 0;
}

static section_t *find_section(asm_ctx_t *ctx, const char *name) {
    size_t i;
    for (i = 0; i < ctx->sec_count; ++i) {
        if (strcmp(ctx->secs[i].name, name) == 0) {
            return &ctx->secs[i];
        }
    }
    return NULL;
}

static section_t *get_or_add_section(asm_ctx_t *ctx, const char *name) {
    section_t sec;
    section_t *found = find_section(ctx, name);
    if (found != NULL) {
        return found;
    }

    memset(&sec, 0, sizeof(sec));
    sec.name = xstrdup(name);
    sec.type = SHT_PROGBITS;
    sec.flags = 0;
    sec.align = 1;
    sec.is_bss = 0;

    if (strcmp(name, ".text") == 0) {
        sec.flags = SHF_ALLOC | SHF_EXECINSTR;
        sec.align = 16;
    } else if (strcmp(name, ".data") == 0) {
        sec.flags = SHF_ALLOC | SHF_WRITE;
        sec.align = 8;
    } else if (strcmp(name, ".bss") == 0) {
        sec.flags = SHF_ALLOC | SHF_WRITE;
        sec.type = SHT_NOBITS;
        sec.is_bss = 1;
        sec.align = 8;
    }

    if (sec.name == NULL || push_section(ctx, &sec) != 0) {
        free(sec.name);
        return NULL;
    }
    return &ctx->secs[ctx->sec_count - 1];
}

static symbol_t *find_symbol(asm_ctx_t *ctx, const char *name) {
    size_t i;
    for (i = 0; i < ctx->sym_count; ++i) {
        if (strcmp(ctx->syms[i].name, name) == 0) {
            return &ctx->syms[i];
        }
    }
    return NULL;
}

static symbol_t *get_or_add_symbol(asm_ctx_t *ctx, const char *name) {
    symbol_t sym;
    symbol_t *found = find_symbol(ctx, name);
    if (found != NULL) {
        return found;
    }
    memset(&sym, 0, sizeof(sym));
    sym.name = xstrdup(name);
    sym.bind = STB_LOCAL;
    sym.type = STT_NOTYPE;
    if (sym.name == NULL || push_symbol(ctx, &sym) != 0) {
        free(sym.name);
        return NULL;
    }
    return &ctx->syms[ctx->sym_count - 1];
}

static uint64_t section_offset(const section_t *sec) {
    return sec->is_bss ? sec->bss_size : (uint64_t)sec->size;
}

static int sec_reserve(section_t *sec, size_t n) {
    unsigned char *next;
    size_t ncap;
    if (sec->is_bss) {
        sec->bss_size += n;
        return 0;
    }
    if (sec->size + n <= sec->cap) {
        return 0;
    }
    ncap = sec->cap == 0 ? 64 : sec->cap;
    while (ncap < sec->size + n) {
        ncap *= 2;
    }
    next = (unsigned char *)realloc(sec->data, ncap);
    if (next == NULL) {
        return -1;
    }
    sec->data = next;
    sec->cap = ncap;
    return 0;
}

static int sec_emit_byte(section_t *sec, unsigned char b) {
    if (sec_reserve(sec, 1) != 0) {
        return -1;
    }
    if (!sec->is_bss) {
        sec->data[sec->size] = b;
        sec->size++;
    }
    return 0;
}

static int sec_emit_zeros(section_t *sec, size_t n) {
    size_t i;
    for (i = 0; i < n; ++i) {
        if (sec_emit_byte(sec, 0) != 0) {
            return -1;
        }
    }
    return 0;
}

static int sec_emit_le(section_t *sec, uint64_t v, int width) {
    int i;
    for (i = 0; i < width; ++i) {
        if (sec_emit_byte(sec, (unsigned char)((v >> (i * 8)) & 0xffu)) != 0) {
            return -1;
        }
    }
    return 0;
}

static int parse_int64(const char *s, int64_t *out) {
    char *end = NULL;
    long long v;
    errno = 0;
    v = strtoll(s, &end, 0);
    if (errno != 0 || end == s || *trim(end) != '\0') {
        return -1;
    }
    *out = (int64_t)v;
    return 0;
}

static int parse_quoted(const char *in, char **out, size_t *out_len) {
    size_t i = 0;
    size_t cap = strlen(in) + 1;
    char *buf;
    size_t n = 0;
    if (in[0] != '\"') {
        return -1;
    }
    buf = (char *)malloc(cap);
    if (buf == NULL) {
        return -1;
    }
    i = 1;
    while (in[i] != '\0' && in[i] != '\"') {
        char c = in[i++];
        if (c == '\\' && in[i] != '\0') {
            c = in[i++];
            switch (c) {
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case '\\': c = '\\'; break;
                case '\"': c = '\"'; break;
                case '0': c = '\0'; break;
                default: break;
            }
        }
        buf[n++] = c;
    }
    if (in[i] != '\"') {
        free(buf);
        return -1;
    }
    *out = buf;
    *out_len = n;
    return 0;
}

static int add_fixup(asm_ctx_t *ctx, section_t *sec, uint64_t off, symbol_t *sym,
                     uint32_t reloc_type, int64_t addend) {
    fixup_t fx;
    fx.section = sec;
    fx.offset = off;
    fx.symbol = sym;
    fx.reloc_type = reloc_type;
    fx.addend = addend;
    return push_fixup(ctx, &fx);
}

static int encode_symbol_value(asm_ctx_t *ctx, section_t *sec, const char *expr, int width) {
    int64_t iv;
    symbol_t *sym;
    uint64_t off = section_offset(sec);
    uint32_t rtype;

    if (parse_int64(expr, &iv) == 0) {
        return sec_emit_le(sec, (uint64_t)iv, width);
    }

    sym = get_or_add_symbol(ctx, expr);
    if (sym == NULL) {
        return -1;
    }
    if (!sym->defined && sym->bind == STB_LOCAL) {
        sym->bind = STB_GLOBAL;
    }

    if (ctx->mode64) {
        if (width == 8) {
            rtype = R_X86_64_64;
        } else if (width == 4) {
            rtype = R_X86_64_32;
        } else {
            return -1;
        }
    } else {
        if (width != 4) {
            return -1;
        }
        rtype = R_386_32;
    }

    if (sec_emit_zeros(sec, (size_t)width) != 0) {
        return -1;
    }
    if (add_fixup(ctx, sec, off, sym, rtype, 0) != 0) {
        return -1;
    }
    return 0;
}

static int define_label(asm_ctx_t *ctx, const char *name) {
    symbol_t *sym = get_or_add_symbol(ctx, name);
    if (sym == NULL || ctx->cur == NULL) {
        return -1;
    }
    sym->defined = 1;
    sym->section = ctx->cur;
    sym->value = section_offset(ctx->cur);
    if (sym->type == STT_NOTYPE) {
        sym->type = STT_FUNC;
    }
    return 0;
}

static int parse_data_list(asm_ctx_t *ctx, section_t *sec, char *args, int width) {
    char *p = args;
    while (p != NULL && *p != '\0') {
        char *comma = strchr(p, ',');
        char *item;
        if (comma != NULL) {
            *comma = '\0';
        }
        item = trim(p);
        if (*item != '\0' && encode_symbol_value(ctx, sec, item, width) != 0) {
            return -1;
        }
        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }
    return 0;
}

static int parse_instruction(asm_ctx_t *ctx, char *line) {
    char *mn = line;
    char *arg;
    section_t *sec = ctx->cur;
    if (sec == NULL) {
        return -1;
    }
    while (*line != '\0' && !isspace((unsigned char)*line)) {
        line++;
    }
    if (*line != '\0') {
        *line++ = '\0';
    }
    arg = trim(line);

    if (strcmp(mn, "nop") == 0) {
        return sec_emit_byte(sec, 0x90);
    }
    if (strcmp(mn, "ret") == 0) {
        return sec_emit_byte(sec, 0xC3);
    }
    if (strcmp(mn, "int") == 0) {
        int64_t v;
        if (parse_int64(arg, &v) != 0 || v < 0 || v > 255) {
            return -1;
        }
        if (sec_emit_byte(sec, 0xCD) != 0) {
            return -1;
        }
        return sec_emit_byte(sec, (unsigned char)v);
    }
    if (strcmp(mn, "movl") == 0) {
        int64_t v;
        char *comma = strchr(arg, ',');
        char *dst;
        int reg = -1;
        if (comma == NULL) {
            return -1;
        }
        *comma++ = '\0';
        arg = trim(arg);
        dst = trim(comma);
        if (arg[0] != '$') {
            return -1;
        }
        if (strcmp(dst, "%eax") == 0) reg = 0;
        else if (strcmp(dst, "%ecx") == 0) reg = 1;
        else if (strcmp(dst, "%edx") == 0) reg = 2;
        else if (strcmp(dst, "%ebx") == 0) reg = 3;
        else if (strcmp(dst, "%esp") == 0) reg = 4;
        else if (strcmp(dst, "%ebp") == 0) reg = 5;
        else if (strcmp(dst, "%esi") == 0) reg = 6;
        else if (strcmp(dst, "%edi") == 0) reg = 7;
        if (reg < 0) {
            return -1;
        }
        if (sec_emit_byte(sec, (unsigned char)(0xB8 + reg)) != 0) {
            return -1;
        }
        if (parse_int64(arg + 1, &v) == 0) {
            return sec_emit_le(sec, (uint64_t)(uint32_t)v, 4);
        } else {
            symbol_t *sym = get_or_add_symbol(ctx, arg + 1);
            uint64_t off = section_offset(sec);
            uint32_t rtype = ctx->mode64 ? R_X86_64_32 : R_386_32;
            if (sym == NULL) {
                return -1;
            }
            if (!sym->defined && sym->bind == STB_LOCAL) {
                sym->bind = STB_GLOBAL;
            }
            if (sec_emit_zeros(sec, 4) != 0) {
                return -1;
            }
            return add_fixup(ctx, sec, off, sym, rtype, 0);
        }
    }
    if (strcmp(mn, "xorl") == 0) {
        char *comma = strchr(arg, ',');
        char *lhs;
        char *rhs;
        int lreg = -1;
        int rreg = -1;
        if (comma == NULL) {
            return -1;
        }
        *comma++ = '\0';
        lhs = trim(arg);
        rhs = trim(comma);
        if (strcmp(lhs, "%eax") == 0) lreg = 0;
        else if (strcmp(lhs, "%ecx") == 0) lreg = 1;
        else if (strcmp(lhs, "%edx") == 0) lreg = 2;
        else if (strcmp(lhs, "%ebx") == 0) lreg = 3;
        else if (strcmp(lhs, "%esp") == 0) lreg = 4;
        else if (strcmp(lhs, "%ebp") == 0) lreg = 5;
        else if (strcmp(lhs, "%esi") == 0) lreg = 6;
        else if (strcmp(lhs, "%edi") == 0) lreg = 7;
        if (strcmp(rhs, "%eax") == 0) rreg = 0;
        else if (strcmp(rhs, "%ecx") == 0) rreg = 1;
        else if (strcmp(rhs, "%edx") == 0) rreg = 2;
        else if (strcmp(rhs, "%ebx") == 0) rreg = 3;
        else if (strcmp(rhs, "%esp") == 0) rreg = 4;
        else if (strcmp(rhs, "%ebp") == 0) rreg = 5;
        else if (strcmp(rhs, "%esi") == 0) rreg = 6;
        else if (strcmp(rhs, "%edi") == 0) rreg = 7;
        if (lreg < 0 || rreg < 0) {
            return -1;
        }
        if (sec_emit_byte(sec, 0x31) != 0) {
            return -1;
        }
        return sec_emit_byte(sec, (unsigned char)(0xC0 | (lreg << 3) | rreg));
    }
    if (strcmp(mn, "call") == 0 || strcmp(mn, "jmp") == 0) {
        symbol_t *sym = get_or_add_symbol(ctx, arg);
        uint64_t off = section_offset(sec);
        uint32_t rtype = ctx->mode64 ? R_X86_64_PC32 : R_386_PC32;
        if (sym == NULL) {
            return -1;
        }
        if (!sym->defined && sym->bind == STB_LOCAL) {
            sym->bind = STB_GLOBAL;
        }
        if (sec_emit_byte(sec, strcmp(mn, "call") == 0 ? 0xE8 : 0xE9) != 0) {
            return -1;
        }
        if (sec_emit_zeros(sec, 4) != 0) {
            return -1;
        }
        return add_fixup(ctx, sec, off + 1, sym, rtype, -4);
    }
    return -1;
}

static int parse_line(asm_ctx_t *ctx, char *line, int lineno) {
    char *p;
    char *hash;
    char *semi;
    (void)lineno;

    hash = strchr(line, '#');
    semi = strchr(line, ';');
    if (hash != NULL && (semi == NULL || hash < semi)) {
        *hash = '\0';
    } else if (semi != NULL) {
        *semi = '\0';
    }
    p = trim(line);
    if (*p == '\0') {
        return 0;
    }

    while (1) {
        char *c = strchr(p, ':');
        char *q = p;
        if (c == NULL) {
            break;
        }
        while (q < c && is_ident_char((unsigned char)*q)) {
            q++;
        }
        if (q != c) {
            break;
        }
        *c = '\0';
        if (define_label(ctx, trim(p)) != 0) {
            return -1;
        }
        p = trim(c + 1);
        if (*p == '\0') {
            return 0;
        }
    }

    if (*p == '.') {
        char *name = p + 1;
        char *args;
        while (*p != '\0' && !isspace((unsigned char)*p)) {
            p++;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
        args = trim(p);

        if (strcmp(name, "text") == 0) {
            ctx->cur = get_or_add_section(ctx, ".text");
            return ctx->cur == NULL ? -1 : 0;
        }
        if (strcmp(name, "data") == 0) {
            ctx->cur = get_or_add_section(ctx, ".data");
            return ctx->cur == NULL ? -1 : 0;
        }
        if (strcmp(name, "bss") == 0) {
            ctx->cur = get_or_add_section(ctx, ".bss");
            return ctx->cur == NULL ? -1 : 0;
        }
        if (strcmp(name, "section") == 0) {
            char *end = args;
            while (*end != '\0' && *end != ',' && !isspace((unsigned char)*end)) {
                end++;
            }
            *end = '\0';
            ctx->cur = get_or_add_section(ctx, args);
            return ctx->cur == NULL ? -1 : 0;
        }
        if (strcmp(name, "globl") == 0 || strcmp(name, "global") == 0 ||
            strcmp(name, "weak") == 0 || strcmp(name, "local") == 0) {
            uint8_t bind = strcmp(name, "weak") == 0 ? STB_WEAK :
                           strcmp(name, "local") == 0 ? STB_LOCAL : STB_GLOBAL;
            char *s = args;
            while (s != NULL && *s != '\0') {
                char *comma = strchr(s, ',');
                symbol_t *sym;
                if (comma != NULL) {
                    *comma = '\0';
                }
                sym = get_or_add_symbol(ctx, trim(s));
                if (sym == NULL) {
                    return -1;
                }
                sym->bind = bind;
                if (comma == NULL) {
                    break;
                }
                s = comma + 1;
            }
            return 0;
        }
        if (strcmp(name, "type") == 0) {
            char *comma = strchr(args, ',');
            symbol_t *sym;
            if (comma == NULL) {
                return -1;
            }
            *comma++ = '\0';
            sym = get_or_add_symbol(ctx, trim(args));
            if (sym == NULL) {
                return -1;
            }
            comma = trim(comma);
            if (strstr(comma, "function") != NULL) {
                sym->type = STT_FUNC;
            } else if (strstr(comma, "object") != NULL) {
                sym->type = STT_OBJECT;
            } else if (strstr(comma, "tls") != NULL) {
                sym->type = STT_TLS;
            }
            return 0;
        }
        if (strcmp(name, "size") == 0) {
            char *comma = strchr(args, ',');
            symbol_t *sym;
            int64_t v;
            if (comma == NULL) {
                return 0;
            }
            *comma++ = '\0';
            sym = get_or_add_symbol(ctx, trim(args));
            if (sym == NULL) {
                return -1;
            }
            if (parse_int64(trim(comma), &v) == 0 && v >= 0) {
                sym->size = (uint64_t)v;
            }
            return 0;
        }
        if (strcmp(name, "align") == 0 || strcmp(name, "p2align") == 0) {
            int64_t v;
            uint64_t al;
            uint64_t off;
            uint64_t pad;
            if (ctx->cur == NULL) {
                return -1;
            }
            if (parse_int64(args, &v) != 0 || v < 0) {
                return -1;
            }
            al = strcmp(name, "p2align") == 0 ? (1ULL << v) : (uint64_t)v;
            if (al == 0) {
                al = 1;
            }
            if (ctx->cur->align < al) {
                ctx->cur->align = al;
            }
            off = section_offset(ctx->cur);
            pad = (al - (off % al)) % al;
            return sec_emit_zeros(ctx->cur, (size_t)pad);
        }
        if (strcmp(name, "byte") == 0) {
            return parse_data_list(ctx, ctx->cur, args, 1);
        }
        if (strcmp(name, "long") == 0) {
            return parse_data_list(ctx, ctx->cur, args, 4);
        }
        if (strcmp(name, "quad") == 0) {
            return parse_data_list(ctx, ctx->cur, args, 8);
        }
        if (strcmp(name, "ascii") == 0 || strcmp(name, "string") == 0) {
            char *s;
            size_t n;
            size_t i;
            if (ctx->cur == NULL) {
                return -1;
            }
            if (parse_quoted(args, &s, &n) != 0) {
                return -1;
            }
            for (i = 0; i < n; ++i) {
                if (sec_emit_byte(ctx->cur, (unsigned char)s[i]) != 0) {
                    free(s);
                    return -1;
                }
            }
            if (strcmp(name, "string") == 0) {
                if (sec_emit_byte(ctx->cur, 0) != 0) {
                    free(s);
                    return -1;
                }
            }
            free(s);
            return 0;
        }
        if (strcmp(name, "macro") == 0 || strcmp(name, "endm") == 0 ||
            strcmp(name, "if") == 0 || strcmp(name, "endif") == 0 ||
            strcmp(name, "include") == 0 || strcmp(name, "file") == 0 ||
            strcmp(name, "ident") == 0 || strncmp(name, "cfi_", 4) == 0) {
            return 0;
        }
        return -1;
    }

    return parse_instruction(ctx, p);
}

static int parse_file(asm_ctx_t *ctx) {
    FILE *fp = fopen(ctx->in_path, "r");
    char buf[4096];
    int lineno = 0;
    if (fp == NULL) {
        fprintf(stderr, "as.x86: cannot open %s: %s\n", ctx->in_path, strerror(errno));
        return -1;
    }
    while (fgets(buf, sizeof(buf), fp) != NULL) {
        lineno++;
        if (parse_line(ctx, buf, lineno) != 0) {
            fprintf(stderr, "as.x86:%s:%d: parse error\n", ctx->in_path, lineno);
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

static int validate_output_file(const char *path, int expect_mode64) {
    elfobj_t *check = NULL;
    char *diag = NULL;
    elf_err_t err;

    err = elf_open(path, &check);
    if (err != ELF_OK) {
        fprintf(stderr, "as.x86: reopen failed: %s\n", elf_errstr(err));
        return -1;
    }

    if (expect_mode64) {
        if (elf_class(check) != ELFOBJ_CLASS_64 || elf_machine(check) != EM_X86_64) {
            fprintf(stderr, "as.x86: output is not x86_64 ELF64 as expected\n");
            elf_close(check);
            return -1;
        }
    } else {
        if (elf_class(check) != ELFOBJ_CLASS_32 || elf_machine(check) != EM_386) {
            fprintf(stderr, "as.x86: output is not i386 ELF32 as expected\n");
            elf_close(check);
            return -1;
        }
    }

    err = elf_validate(check, &diag);
    elf_close(check);
    if (err != ELF_OK) {
        fprintf(stderr, "as.x86: post-write validation failed: %s\n",
                diag ? diag : elf_errstr(err));
        free(diag);
        return -1;
    }
    free(diag);
    return 0;
}

static int assemble_with_host_backend(const asm_ctx_t *ctx) {
    char cmd[4096];
    int rc;
    int n;

    /* KISS: x86_64-v1 baseline is delegated to the host assembler toolchain. */
    n = snprintf(cmd, sizeof(cmd), "gcc -c -x assembler -m64 -march=x86-64 %s -o '%s' '%s'",
                 ctx->emit_debug ? "-g" : "", ctx->out_path, ctx->in_path);
    if (n < 0 || (size_t)n >= sizeof(cmd)) {
        fprintf(stderr, "as.x86: backend command too long\n");
        return -1;
    }

    rc = system(cmd);
    if (rc != 0) {
        fprintf(stderr, "as.x86: host backend failed for x86_64-v1 path\n");
        return -1;
    }

    return validate_output_file(ctx->out_path, 1);
}

static int emit_object(asm_ctx_t *ctx) {
    elfobj_t *obj;
    elfobj_class_t cls = ctx->mode64 ? ELFOBJ_CLASS_64 : ELFOBJ_CLASS_32;
    uint16_t machine = ctx->mode64 ? EM_X86_64 : EM_386;
    size_t i;
    elf_err_t err;

    obj = elf_create(ET_REL, machine, cls, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return -1;
    }

    for (i = 0; i < ctx->sec_count; ++i) {
        section_t *s = &ctx->secs[i];
        if (s->is_bss && s->bss_size == 0) {
            continue;
        }
        if (!s->is_bss && s->size == 0) {
            continue;
        }
        s->out_sec = elf_add_section(obj, s->name, s->type, s->flags);
        if (s->out_sec == NULL) {
            fprintf(stderr, "as.x86: elf_add_section(%s) failed\n", s->name);
            elf_close(obj);
            return -1;
        }
        if (elf_section_set_align(s->out_sec, s->align) != ELF_OK) {
            elf_close(obj);
            return -1;
        }
        err = elf_section_set_data(s->out_sec, s->is_bss ? NULL : s->data,
                                   s->is_bss ? (size_t)s->bss_size : s->size);
        if (err != ELF_OK) {
            fprintf(stderr, "as.x86: section data failed: %s\n", elf_errstr(err));
            elf_close(obj);
            return -1;
        }
    }

    for (i = 0; i < ctx->sym_count; ++i) {
        symbol_t *s = &ctx->syms[i];
        s->out_sym = elf_add_symbol(obj, s->name, s->value, s->size, s->bind, s->type);
        if (s->out_sym == NULL) {
            fprintf(stderr, "as.x86: elf_add_symbol(%s) failed\n", s->name);
            elf_close(obj);
            return -1;
        }
        if (s->defined) {
            if (s->section == NULL || s->section->out_sec == NULL) {
                elf_close(obj);
                return -1;
            }
            if (elf_symbol_define(s->out_sym, s->section->out_sec, s->value) != ELF_OK) {
                elf_close(obj);
                return -1;
            }
        }
    }

    for (i = 0; i < ctx->fixup_count; ++i) {
        fixup_t *f = &ctx->fixups[i];
        if (f->section == NULL || f->section->out_sec == NULL || f->symbol == NULL || f->symbol->out_sym == NULL) {
            elf_close(obj);
            return -1;
        }
        err = elf_add_relocation(f->section->out_sec, f->offset, f->symbol->out_sym,
                                 f->reloc_type, f->addend);
        if (err != ELF_OK) {
            fprintf(stderr, "as.x86: relocation failed: %s\n", elf_errstr(err));
            elf_close(obj);
            return -1;
        }
    }

    err = elf_write_file(obj, ctx->out_path);
    if (err != ELF_OK) {
        fprintf(stderr, "as.x86: write failed: %s\n", elf_errstr(err));
        elf_close(obj);
        return -1;
    }
    elf_close(obj);

    return validate_output_file(ctx->out_path, ctx->mode64);
}

static void free_ctx(asm_ctx_t *ctx) {
    size_t i;
    for (i = 0; i < ctx->sec_count; ++i) {
        free(ctx->secs[i].name);
        free(ctx->secs[i].data);
    }
    for (i = 0; i < ctx->sym_count; ++i) {
        free(ctx->syms[i].name);
    }
    free(ctx->secs);
    free(ctx->syms);
    free(ctx->fixups);
}

int main(int argc, char **argv) {
    asm_ctx_t ctx;
    int i;
    memset(&ctx, 0, sizeof(ctx));
    ctx.mode64 = 0;
    ctx.emit_debug = 0;
    ctx.out_path = "a.out.o";

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            ctx.out_path = argv[++i];
            continue;
        }
        if (strcmp(argv[i], "-32") == 0) {
            ctx.mode64 = 0;
            continue;
        }
        if (strcmp(argv[i], "-64") == 0) {
            ctx.mode64 = 1;
            continue;
        }
        if (strcmp(argv[i], "-march") == 0) {
            if (i + 1 >= argc) {
                usage(argv[0]);
                return 2;
            }
            i++;
            continue;
        }
        if (strncmp(argv[i], "-march=", 7) == 0) {
            continue;
        }
        if ((strcmp(argv[i], "-I") == 0 || strcmp(argv[i], "-D") == 0 ||
             strcmp(argv[i], "-Wa") == 0 || strcmp(argv[i], "-mtune") == 0) && i + 1 < argc) {
            i++;
            continue;
        }
        if (strcmp(argv[i], "-g") == 0) {
            ctx.emit_debug = 1;
            continue;
        }
        if (argv[i][0] == '-') {
            fprintf(stderr, "as.x86: unsupported option: %s\n", argv[i]);
            return 2;
        }
        ctx.in_path = argv[i];
    }

    if (ctx.in_path == NULL) {
        usage(argv[0]);
        return 2;
    }

    if (ctx.mode64) {
        return assemble_with_host_backend(&ctx) == 0 ? 0 : 1;
    }

    ctx.cur = get_or_add_section(&ctx, ".text");
    if (ctx.cur == NULL) {
        free_ctx(&ctx);
        return 1;
    }
    if (get_or_add_section(&ctx, ".data") == NULL || get_or_add_section(&ctx, ".bss") == NULL) {
        free_ctx(&ctx);
        return 1;
    }

    if (parse_file(&ctx) != 0 || emit_object(&ctx) != 0) {
        free_ctx(&ctx);
        return 1;
    }

    free_ctx(&ctx);
    return 0;
}
