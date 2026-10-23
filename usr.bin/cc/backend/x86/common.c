/*
 * common.c: what the x86 emitters share (cc_x86.h) -- the frame layout,
 * the integer register cache, inline asm, sections, data and string
 * literals -- and cc_emit_gas(), which writes the module's data and hands
 * its functions to the target's emitter (x86_64.c, i386.c).
 */

#include "cc_asm.h"
#include "cc_backend.h"
#include "cc_diag.h"
#include "cc_options.h"
#include "cc_types.h"
#include "cc_util.h"
#include "cc_x86.h"

#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/** The SSE eightbytes (bit 0, bit 1) an aggregate's ABI byte names, and ABI_SSE_WHOLE for a vector's. */
unsigned abi64_sse_mask(unsigned char abi) {
    return ((abi & CC_ABI_SSE0) != 0 ? 1u : 0u) | ((abi & CC_ABI_SSE1) != 0 ? 2u : 0u) |
           ((abi & CC_ABI_VEC128) != 0 ? ABI_SSE_WHOLE : 0u);
}

/**
 * The general and xmm registers an aggregate of @p size bytes needs when
 * its eightbytes in @p sse are SSE and the rest INTEGER.
 */
void abi64_agg_reg_need(size_t size, unsigned sse, size_t *ngpr, size_t *nxmm) {
    size_t n = (size + 7u) / 8u;
    size_t k;

    *ngpr = 0;
    *nxmm = 0;
    if ((sse & ABI_SSE_WHOLE) != 0) {
        *nxmm = 1;
        return;
    }
    for (k = 0; k < (n == 0 ? 1 : n) && k < 2; ++k) {
        if ((sse & (1u << k)) != 0) {
            ++*nxmm;
        } else {
            ++*ngpr;
        }
    }
}

static unsigned long g_aux_label_serial = 0;

long double x86_64_special_ldouble_const(long code) {
    if (code == 1) {
        return LDBL_MIN;
    }
    if (code == 2) {
#ifdef __LDBL_DENORM_MIN__
        return __LDBL_DENORM_MIN__;
#else
        return 0.0L;
#endif
    }
    if (code == 3) {
        return LDBL_MAX;
    }
    return 0.0L;
}

const char *arg_reg64_gpr(size_t idx) {
    static const char *regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    if (idx >= sizeof(regs) / sizeof(regs[0])) {
        return NULL;
    }
    return regs[idx];
}

const char *arg_reg64_xmm(size_t idx) {
    static const char *regs[] = {"%xmm0", "%xmm1", "%xmm2", "%xmm3",
                                 "%xmm4", "%xmm5", "%xmm6", "%xmm7"};
    if (idx >= sizeof(regs) / sizeof(regs[0])) {
        return NULL;
    }
    return regs[idx];
}

/**
 * Record an emission error.  It has no line; the file being compiled is
 * kept as its path.  The first one recorded is kept.
 *
 * @param d    the diagnostic
 * @param msg  the message
 */
void set_diag(cc_diag_t *d, const char *msg) {
    cc_diag_set(d, NULL, 0, 0, msg);
}

unsigned long next_aux_label_id(void) {
    return g_aux_label_serial++;
}


const char *setcc_int_mnemonic(cc_cmp_kind_t k, int is_unsigned) {
    switch (k) {
    case CC_CMP_EQ:
        return "sete";
    case CC_CMP_NE:
        return "setne";
    case CC_CMP_LT:
        return is_unsigned ? "setb" : "setl";
    case CC_CMP_LE:
        return is_unsigned ? "setbe" : "setle";
    case CC_CMP_GT:
        return is_unsigned ? "seta" : "setg";
    case CC_CMP_GE:
        return is_unsigned ? "setae" : "setge";
    }
    return "sete";
}

static const char *pick_tmp8_excluding(const char *dst_reg, int is_64bit) {
    if (is_64bit) {
        if (strcmp(dst_reg, "%r10") != 0) return "%r10b";
        return "%r11b";
    }
    if (strcmp(dst_reg, "%edx") != 0) return "%dl";
    return "%cl";
}

void emit_setcc_zext_to_reg(FILE *fp, const char *dst_reg, int is_64bit) {
    const char *dst8 = is_64bit ? reg64_to8(dst_reg) : reg32_to8(dst_reg);
    if (is_64bit) {
        fprintf(fp, "\tmovzbq %s, %s\n", dst8, dst_reg);
    } else {
        fprintf(fp, "\tmovzbl %s, %s\n", dst8, dst_reg);
    }
}

void emit_float_setcc_to_reg(FILE *fp, cc_cmp_kind_t k, const char *dst_reg, int is_64bit) {
    const char *primary = NULL;
    const char *secondary = NULL;
    const char *combine = NULL;
    const char *dst8;
    const char *tmp8;

    switch (k) {
    case CC_CMP_EQ:
        primary = "sete";
        secondary = "setnp";
        combine = "andb";
        break;
    case CC_CMP_NE:
        primary = "setne";
        secondary = "setp";
        combine = "orb";
        break;
    case CC_CMP_LT:
        primary = "setb";
        secondary = "setnp";
        combine = "andb";
        break;
    case CC_CMP_LE:
        primary = "setbe";
        secondary = "setnp";
        combine = "andb";
        break;
    case CC_CMP_GT:
        primary = "seta";
        break;
    case CC_CMP_GE:
        primary = "setae";
        break;
    }

    dst8 = is_64bit ? reg64_to8(dst_reg) : reg32_to8(dst_reg);
    tmp8 = pick_tmp8_excluding(dst_reg, is_64bit);
    if (secondary != NULL && combine != NULL) {
        fprintf(fp, "\t%s %s\n", secondary, tmp8);
        fprintf(fp, "\t%s %s\n", primary, dst8);
        fprintf(fp, "\t%s %s, %s\n", combine, tmp8, dst8);
    } else if (primary != NULL) {
        fprintf(fp, "\t%s %s\n", primary, dst8);
    }
    emit_setcc_zext_to_reg(fp, dst_reg, is_64bit);
}

void emit_local_label(FILE *fp, const char *fn, int label) {
    fprintf(fp, ".L%s_%d", fn, label);
}

static int asm_constraint_is_immediate(const char *c) {
    size_t i;
    if (c == NULL) {
        return 0;
    }
    for (i = 0; c[i] != '\0'; ++i) {
        unsigned char ch = (unsigned char)c[i];
        if (ch == 'i' || ch == 'n' || ch == 'e' || (ch >= 'I' && ch <= 'P')) {
            return 1;
        }
    }
    return 0;
}

/**
 * Whether constant @p v may be an immediate under constraint @p c: some
 * letter of it takes v, as gcc reads x86 constraints -- i and n any
 * constant, e a signed and Z an unsigned 32-bit one, I 0..31, J 0..63,
 * K -128..127, L 0xff or 0xffff, M 0..3, N 0..255 (an in/out port), O
 * 0..127.  A constant no letter takes goes to the constraint's register or
 * memory alternative instead: Linux's `bts %1, %0` with "Ir" (nr) and
 * `outw %w0, %w1` with "Nd" (port) need %ecx or %dx for a value out of
 * range, and the assembler refuses `btsl $4294967295` or `outw $0x3f8`.
 */
static int asm_immediate_fits(const char *c, long long v) {
    size_t i;

    for (i = 0; c != NULL && c[i] != '\0'; ++i) {
        switch (c[i]) {
        case 'i':
        case 'n':
            return 1;
        case 'e':
            if (v >= INT32_MIN && v <= INT32_MAX) return 1;
            break;
        case 'Z':
            if (v >= 0 && v <= (long long)UINT32_MAX) return 1;
            break;
        case 'I':
            if (v >= 0 && v <= 31) return 1;
            break;
        case 'J':
            if (v >= 0 && v <= 63) return 1;
            break;
        case 'K':
            if (v >= -128 && v <= 127) return 1;
            break;
        case 'L':
            if (v == 0xff || v == 0xffff) return 1;
            break;
        case 'M':
            if (v >= 0 && v <= 3) return 1;
            break;
        case 'N':
            if (v >= 0 && v <= 255) return 1;
            break;
        case 'O':
            if (v >= 0 && v <= 127) return 1;
            break;
        default:
            break;
        }
    }
    return 0;
}

static const char *asm_constraint_fixed_reg64(const char *c) {
    if (c == NULL) {
        return NULL;
    }
    if (cc_asm_constraint_has(c, 'a')) {
        return "%rax";
    }
    if (cc_asm_constraint_has(c, 'b')) {
        return "%rbx";
    }
    if (cc_asm_constraint_has(c, 'c')) {
        return "%rcx";
    }
    if (cc_asm_constraint_has(c, 'd')) {
        return "%rdx";
    }
    if (cc_asm_constraint_has(c, 'S')) {
        return "%rsi";
    }
    if (cc_asm_constraint_has(c, 'D')) {
        return "%rdi";
    }
    return NULL;
}

static const char *asm_constraint_fixed_reg32(const char *c) {
    if (c == NULL) {
        return NULL;
    }
    if (cc_asm_constraint_has(c, 'a')) {
        return "%eax";
    }
    if (cc_asm_constraint_has(c, 'b')) {
        return "%ebx";
    }
    if (cc_asm_constraint_has(c, 'c')) {
        return "%ecx";
    }
    if (cc_asm_constraint_has(c, 'd')) {
        return "%edx";
    }
    if (cc_asm_constraint_has(c, 'S')) {
        return "%esi";
    }
    if (cc_asm_constraint_has(c, 'D')) {
        return "%edi";
    }
    if (cc_asm_constraint_has(c, 'A')) {
        /* %edx:%eax, of an 8-byte operand (asm_pair_operand()): named by its low word */
        return "%eax";
    }
    return NULL;
}

/**
 * Whether an i386 asm operand of @p size bytes and constraint @p c is held in the pair %edx:%eax ("A" of a
 * 64-bit value: Linux's do_div() hands its dividend in and its quotient out so).
 */
static int asm_pair_operand(const char *c, int is_64bit, int size) {
    return !is_64bit && size == 8 && c != NULL && cc_asm_constraint_has(c, 'A');
}

static int append_text(char **dst, size_t *len, size_t *cap, const char *text, size_t n) {
    char *next;
    if (*len + n + 1 > *cap) {
        size_t ncap = *cap == 0 ? 64 : *cap;
        while (*len + n + 1 > ncap) {
            ncap *= 2;
        }
        next = (char *)realloc(*dst, ncap);
        if (next == NULL) {
            return -1;
        }
        *dst = next;
        *cap = ncap;
    }
    memcpy(*dst + *len, text, n);
    *len += n;
    (*dst)[*len] = '\0';
    return 0;
}

static int append_c(char **dst, size_t *len, size_t *cap, char ch) {
    return append_text(dst, len, cap, &ch, 1);
}

static int find_named_operand_index(const char *name, char **names, size_t count) {
    size_t i;
    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (names[i] != NULL && strcmp(names[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_named_goto_index(const char *name, char **names, size_t count) {
    size_t i;
    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (names[i] != NULL && strcmp(names[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * Render an inline asm template: replace each operand reference with its
 * operand's text and each goto-label reference with the label.
 *
 * @param tmpl           the template
 * @param operand_texts  each operand's text, outputs then inputs
 * @param operand_names  each operand's `[name]`, or NULL
 * @param op_count       how many operands
 * @param label_base     the number of the first goto label, `%l<n>`:
 *                       op_count plus one for each `+` output
 * @param goto_labels    each goto label's IR block
 * @param goto_names     each goto label's name
 * @param goto_count     how many goto labels
 * @param fn_name        the enclosing function, for local label names
 * @param diag           receives an error
 * @return               the rendered text (caller frees), or NULL
 */
static int x86_gpr_index(const char *name);
static const char *asm_operand_with_modifier(char mod, const char *text, char *buf, size_t bufsz);

static char *render_inline_asm_template(const char *tmpl, char **operand_texts, char **operand_names, size_t op_count,
                                        size_t label_base, const int *goto_labels, char **goto_names,
                                        size_t goto_count, const char *fn_name, cc_diag_t *diag) {
    size_t i = 0;
    size_t len = 0;
    size_t cap = 0;
    char *out = NULL;
    if (tmpl == NULL) {
        return NULL;
    }
    while (tmpl[i] != '\0') {
        int strip_dollar = 0;
        char mod = 0;
        char mbuf[96];
        if (tmpl[i] != '%') {
            if (append_c(&out, &len, &cap, tmpl[i]) != 0) {
                free(out);
                return NULL;
            }
            i++;
            continue;
        }
        i++;
        if (tmpl[i] == '%') {
            if (append_c(&out, &len, &cap, '%') != 0) {
                free(out);
                return NULL;
            }
            i++;
            continue;
        }
        if (tmpl[i] == 'c') {
            strip_dollar = 1;
            i++;
        } else if (tmpl[i] == 'P' || tmpl[i] == 'q' || tmpl[i] == 'k' || tmpl[i] == 'w' || tmpl[i] == 'b' ||
                   tmpl[i] == 'h' || tmpl[i] == 'z' || tmpl[i] == 'n' || tmpl[i] == 'a' || tmpl[i] == 'A' ||
                   tmpl[i] == 'V' || tmpl[i] == 'L' || tmpl[i] == 'p' || tmpl[i] == 'B' || tmpl[i] == 'R') {
            /* a GCC operand modifier: asm_operand_with_modifier() applies the ones that change the text */
            mod = tmpl[i];
            i++;
        }
        if (tmpl[i] >= '0' && tmpl[i] <= '9') {
            long n = 0;
            const char *op_text;
            size_t op_len;
            while (tmpl[i] >= '0' && tmpl[i] <= '9') {
                n = n * 10 + (long)(tmpl[i] - '0');
                i++;
            }
            if (n < 0 || (size_t)n >= op_count || operand_texts[n] == NULL) {
                set_diag(diag, "asm template operand index is out of range");
                free(out);
                return NULL;
            }
            op_text = asm_operand_with_modifier(mod, operand_texts[n], mbuf, sizeof(mbuf));
            op_len = strlen(op_text);
            if (strip_dollar && op_len > 0 && op_text[0] == '$') {
                op_text++;
                op_len--;
            }
            if (append_text(&out, &len, &cap, op_text, op_len) != 0) {
                free(out);
                return NULL;
            }
            continue;
        }
        if (tmpl[i] == 'l') {
            int label_id = -1;
            i++;
            if (tmpl[i] >= '0' && tmpl[i] <= '9') {
                long n = 0;
                while (tmpl[i] >= '0' && tmpl[i] <= '9') {
                    n = n * 10 + (long)(tmpl[i] - '0');
                    i++;
                }
                if (goto_labels == NULL || n < 0 || (size_t)n < label_base || (size_t)n - label_base >= goto_count) {
                    char msg[160];
                    snprintf(msg, sizeof(msg),
                             "asm template goto-label index is out of range (n=%ld first label=%zu labels=%zu)", n,
                             label_base, goto_count);
                    set_diag(diag, msg);
                    free(out);
                    return NULL;
                }
                n -= (long)label_base;
                label_id = goto_labels[n];
            } else if (tmpl[i] == '[') {
                size_t b = ++i;
                int idx;
                while (tmpl[i] != '\0' && tmpl[i] != ']') {
                    i++;
                }
                if (tmpl[i] != ']') {
                    set_diag(diag, "asm template has unterminated goto label");
                    free(out);
                    return NULL;
                }
                {
                    char *nm = (char *)malloc(i - b + 1);
                    if (nm == NULL) {
                        free(out);
                        return NULL;
                    }
                    memcpy(nm, tmpl + b, i - b);
                    nm[i - b] = '\0';
                    idx = find_named_goto_index(nm, goto_names, goto_count);
                    free(nm);
                }
                if (idx < 0 || goto_labels == NULL || (size_t)idx >= goto_count) {
                    set_diag(diag, "asm template references unknown goto label");
                    free(out);
                    return NULL;
                }
                label_id = goto_labels[idx];
                i++;
            } else {
                set_diag(diag, "asm template has malformed %l label reference");
                free(out);
                return NULL;
            }
            {
                char lbuf[160];
                snprintf(lbuf, sizeof(lbuf), ".L%s_%d", fn_name != NULL ? fn_name : "__fn", label_id);
                if (append_text(&out, &len, &cap, lbuf, strlen(lbuf)) != 0) {
                    free(out);
                    return NULL;
                }
            }
            continue;
        }
        if (tmpl[i] == '[') {
            size_t b = ++i;
            int idx;
            const char *op_text;
            size_t op_len;
            while (tmpl[i] != '\0' && tmpl[i] != ']') {
                i++;
            }
            if (tmpl[i] != ']') {
                set_diag(diag, "asm template has unterminated named operand");
                free(out);
                return NULL;
            }
            {
                char *nm = (char *)malloc(i - b + 1);
                if (nm == NULL) {
                    free(out);
                    return NULL;
                }
                memcpy(nm, tmpl + b, i - b);
                nm[i - b] = '\0';
                idx = find_named_operand_index(nm, operand_names, op_count);
                free(nm);
            }
            if (idx < 0 || operand_texts[idx] == NULL) {
                set_diag(diag, "asm template references unknown named operand");
                free(out);
                return NULL;
            }
            op_text = asm_operand_with_modifier(mod, operand_texts[idx], mbuf, sizeof(mbuf));
            op_len = strlen(op_text);
            if (strip_dollar && op_len > 0 && op_text[0] == '$') {
                op_text++;
                op_len--;
            }
            if (append_text(&out, &len, &cap, op_text, op_len) != 0) {
                free(out);
                return NULL;
            }
            i++;
            continue;
        }
        {
            char msg[96];
            unsigned char ch = (unsigned char)tmpl[i];
            if (ch == '\0') {
                snprintf(msg, sizeof(msg), "asm template has truncated '%%' reference");
            } else if (ch >= 32 && ch < 127) {
                snprintf(msg, sizeof(msg), "asm template has unsupported '%%%c' reference", ch);
            } else {
                snprintf(msg, sizeof(msg), "asm template has unsupported '%%' reference (0x%02x)", ch);
            }
            set_diag(diag, msg);
        }
        free(out);
        return NULL;
    }
    if (out == NULL) {
        out = (char *)malloc(1);
        if (out != NULL) {
            out[0] = '\0';
        }
    }
    return out;
}

/* Returns a freshly-allocated copy of `src` where control characters
 * (newline, tab, carriage return) appearing *inside* a double-quoted
 * string literal are replaced with their backslash-escaped form. This
 * keeps multi-line .ascii / .string contents emitted by inline asm
 * templates on a single physical line so the assembler's line-oriented
 * lexer can tokenize them. Returns NULL on allocation failure. */
static char *escape_strings_in_asm_template(const char *src) {
    size_t cap;
    size_t len = 0;
    char *out;
    int in_string = 0;
    int prev_was_backslash = 0;
    size_t i;
    if (src == NULL) {
        return NULL;
    }
    cap = strlen(src) + 16;
    out = (char *)malloc(cap);
    if (out == NULL) {
        return NULL;
    }
    for (i = 0; src[i] != '\0'; ++i) {
        char ch = src[i];
        const char *replacement = NULL;
        if (in_string && !prev_was_backslash) {
            if (ch == '\n') {
                replacement = "\\n";
            } else if (ch == '\t') {
                replacement = "\\t";
            } else if (ch == '\r') {
                replacement = "\\r";
            }
        }
        if (replacement != NULL) {
            size_t rl = strlen(replacement);
            if (len + rl + 1 >= cap) {
                cap = cap * 2 + rl + 1;
                {
                    char *grown = (char *)realloc(out, cap);
                    if (grown == NULL) {
                        free(out);
                        return NULL;
                    }
                    out = grown;
                }
            }
            memcpy(out + len, replacement, rl);
            len += rl;
            prev_was_backslash = 0;
            continue;
        }
        if (len + 2 >= cap) {
            cap = cap * 2 + 2;
            {
                char *grown = (char *)realloc(out, cap);
                if (grown == NULL) {
                    free(out);
                    return NULL;
                }
                out = grown;
            }
        }
        out[len++] = ch;
        if (ch == '"' && !prev_was_backslash) {
            in_string = !in_string;
            prev_was_backslash = 0;
        } else if (ch == '\\' && in_string && !prev_was_backslash) {
            prev_was_backslash = 1;
        } else {
            prev_was_backslash = 0;
        }
    }
    out[len] = '\0';
    return out;
}

static int emit_asm_lines(FILE *fp, const char *text) {
    static const char *const x86_prefixes[] = {
        "lock",
        "rep",
        "repe",
        "repz",
        "repne",
        "repnz",
        "bnd",
    };
    size_t prefix_i;
    const char *p = text;
    const char *line = text;
    int have_pending_prefix = 0;
    char pending_prefix[16];
    if (text == NULL) {
        return 0;
    }

    pending_prefix[0] = '\0';
    (void)prefix_i;

    #define IS_WS(ch) ((ch) == ' ' || (ch) == '\t' || (ch) == '\r')
    #define EMIT_SEGMENT(seg_lo, seg_hi)                                                                     \
        do {                                                                                                 \
            const char *_lo = (seg_lo);                                                                      \
            const char *_hi = (seg_hi);                                                                      \
            while (_lo < _hi && IS_WS(*_lo)) _lo++;                                                          \
            while (_hi > _lo && IS_WS(*(_hi - 1))) _hi--;                                                    \
            if (_hi > _lo) {                                                                                 \
                size_t _len = (size_t)(_hi - _lo);                                                           \
                int _is_prefix = 0;                                                                          \
                for (prefix_i = 0; prefix_i < sizeof(x86_prefixes) / sizeof(x86_prefixes[0]); ++prefix_i) { \
                    if (_len == strlen(x86_prefixes[prefix_i]) &&                                            \
                        strncmp(_lo, x86_prefixes[prefix_i], _len) == 0) {                                   \
                        _is_prefix = 1;                                                                      \
                        break;                                                                               \
                    }                                                                                        \
                }                                                                                            \
                if (have_pending_prefix) {                                                                   \
                    fprintf(fp, "\t%s %.*s\n", pending_prefix, (int)_len, _lo);                             \
                    have_pending_prefix = 0;                                                                 \
                    pending_prefix[0] = '\0';                                                                \
                } else if (_is_prefix) {                                                                     \
                    if (_len >= sizeof(pending_prefix)) _len = sizeof(pending_prefix) - 1;                  \
                    memcpy(pending_prefix, _lo, _len);                                                       \
                    pending_prefix[_len] = '\0';                                                             \
                    have_pending_prefix = 1;                                                                 \
                } else {                                                                                     \
                    fprintf(fp, "\t%.*s\n", (int)_len, _lo);                                                 \
                }                                                                                            \
            }                                                                                                \
        } while (0)

    while (*p != '\0') {
        if (*p == '\n') {
            const char *seg = line;
            const char *q = line;
            while (q < p) {
                if (*q == ';') {
                    EMIT_SEGMENT(seg, q);
                    seg = q + 1;
                }
                q++;
            }
            EMIT_SEGMENT(seg, p);
            line = p + 1;
        }
        p++;
    }
    if (p != line) {
        const char *seg = line;
        const char *q = line;
        while (q < p) {
            if (*q == ';') {
                EMIT_SEGMENT(seg, q);
                seg = q + 1;
            }
            q++;
        }
        EMIT_SEGMENT(seg, p);
    }
    if (have_pending_prefix) {
        fprintf(fp, "\t%s\n", pending_prefix);
    }

    #undef EMIT_SEGMENT
    #undef IS_WS
    return 0;
}

const char *ssa_op_name(cc_ssa_opcode_t op) {
    switch (op) {
    case CC_SSA_PARAM: return "param";
    case CC_SSA_CONST: return "const";
    case CC_SSA_STR: return "str";
    case CC_SSA_GADDR: return "gaddr";
    case CC_SSA_LADDR: return "laddr";
    case CC_SSA_ADD: return "add";
    case CC_SSA_SUB: return "sub";
    case CC_SSA_MUL: return "mul";
    case CC_SSA_DIV: return "div";
    case CC_SSA_AND: return "and";
    case CC_SSA_OR: return "or";
    case CC_SSA_XOR: return "xor";
    case CC_SSA_SHL: return "shl";
    case CC_SSA_SHR: return "shr";
    case CC_SSA_ADDR: return "addr";
    case CC_SSA_LOAD: return "load";
    case CC_SSA_STORE: return "store";
    case CC_SSA_MOV: return "mov";
    case CC_SSA_CMP: return "cmp";
    case CC_SSA_I2F: return "i2f";
    case CC_SSA_F2I: return "f2i";
    case CC_SSA_FROUND32: return "fround32";
    case CC_SSA_FROUND64: return "fround64";
    case CC_SSA_EXT: return "ext";
    case CC_SSA_LO32: return "lo32";
    case CC_SSA_HI32: return "hi32";
    case CC_SSA_PAIR: return "pair";
    case CC_SSA_UMULW: return "umulw";
    case CC_SSA_STACKALLOC: return "stackalloc";
    case CC_SSA_DYNALLOC: return "dynalloc";
    case CC_SSA_CHAIN: return "chain";
    case CC_SSA_FRAMEADDR: return "frameaddr";
    case CC_SSA_REGREAD: return "regread";
    case CC_SSA_LABEL: return "label";
    case CC_SSA_BR: return "br";
    case CC_SSA_BR_COND: return "br_cond";
    case CC_SSA_VA_START: return "va_start";
    case CC_SSA_CALL: return "call";
    case CC_SSA_CALLI: return "calli";
    case CC_SSA_ASM: return "asm";
    case CC_SSA_TRAP: return "trap";
    case CC_SSA_RET: return "ret";
    default: return "unknown";
    }
}

static int find_store_instr_index_for_ptr_before(const cc_ssa_function_t *f, int ptr_value, size_t max_instr_index);

static int eval_const_i64_for_value(const cc_ssa_function_t *f, const int *def_index, int value, unsigned char *visiting,
                                    long *out_imm) {
    const cc_ssa_instr_t *in;
    long a;
    long b;

    if (f == NULL || def_index == NULL || visiting == NULL || out_imm == NULL) {
        return -1;
    }
    if (value < 0 || value >= f->value_count) {
        return -1;
    }
    if (cc_ssa_value_class(f, value) != CC_VAL_I64) {
        return -1;
    }
    if (visiting[value]) {
        return -1;
    }
    if (def_index[value] < 0 || (size_t)def_index[value] >= f->instr_count) {
        return -1;
    }
    in = &f->instrs[def_index[value]];
    if (in->dst != value) {
        return -1;
    }

    visiting[value] = 1;
    switch (in->op) {
    case CC_SSA_CONST:
        *out_imm = in->imm;
        visiting[value] = 0;
        return 0;

    case CC_SSA_MOV:
        if (eval_const_i64_for_value(f, def_index, in->lhs, visiting, out_imm) == 0) {
            visiting[value] = 0;
            return 0;
        }
        break;
    case CC_SSA_LOAD:
        if (in->lhs >= 0) {
            int store_idx = find_store_instr_index_for_ptr_before(f, in->lhs, (size_t)def_index[value]);
            if (store_idx >= 0) {
                const cc_ssa_instr_t *st = &f->instrs[store_idx];
                if (st->rhs >= 0 && eval_const_i64_for_value(f, def_index, st->rhs, visiting, out_imm) == 0) {
                    visiting[value] = 0;
                    return 0;
                }
            }
        }
        break;

    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_DIV:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
        if (eval_const_i64_for_value(f, def_index, in->lhs, visiting, &a) != 0) {
            break;
        }
        /* Sign-/zero-extension lowering emits SHL/SHR with rhs == -1 and the
         * count in `in->imm`. Honour that immediate form when folding. */
        if (in->rhs < 0) {
            if (in->op != CC_SSA_SHL && in->op != CC_SSA_SHR) {
                break;
            }
            b = in->imm;
        } else if (eval_const_i64_for_value(f, def_index, in->rhs, visiting, &b) != 0) {
            break;
        }
        if (in->op == CC_SSA_ADD) {
            *out_imm = a + b;
        } else if (in->op == CC_SSA_SUB) {
            *out_imm = a - b;
        } else if (in->op == CC_SSA_MUL) {
            *out_imm = a * b;
        } else if (in->op == CC_SSA_DIV) {
            if (b == 0) {
                break;
            }
            *out_imm = in->is_unsigned ? (long)((unsigned long)a / (unsigned long)b) : (a / b);
        } else if (in->op == CC_SSA_AND) {
            *out_imm = a & b;
        } else if (in->op == CC_SSA_OR) {
            *out_imm = a | b;
        } else if (in->op == CC_SSA_XOR) {
            *out_imm = a ^ b;
        } else if (in->op == CC_SSA_SHL) {
            *out_imm = a << (b & 63);
        } else {
            *out_imm = in->is_unsigned ? (long)((unsigned long)a >> (b & 63)) : (a >> (b & 63));
        }
        visiting[value] = 0;
        return 0;

    case CC_SSA_CMP:
        if (eval_const_i64_for_value(f, def_index, in->lhs, visiting, &a) != 0 ||
            eval_const_i64_for_value(f, def_index, in->rhs, visiting, &b) != 0) {
            break;
        }
        if (in->cmp_kind == CC_CMP_EQ) {
            *out_imm = (a == b);
        } else if (in->cmp_kind == CC_CMP_NE) {
            *out_imm = (a != b);
        } else if (in->cmp_kind == CC_CMP_LT) {
            *out_imm = in->is_unsigned ? ((unsigned long)a < (unsigned long)b) : (a < b);
        } else if (in->cmp_kind == CC_CMP_LE) {
            *out_imm = in->is_unsigned ? ((unsigned long)a <= (unsigned long)b) : (a <= b);
        } else if (in->cmp_kind == CC_CMP_GT) {
            *out_imm = in->is_unsigned ? ((unsigned long)a > (unsigned long)b) : (a > b);
        } else {
            *out_imm = in->is_unsigned ? ((unsigned long)a >= (unsigned long)b) : (a >= b);
        }
        visiting[value] = 0;
        return 0;

    case CC_SSA_STACKALLOC:
        break;

    default:
        break;
    }

    visiting[value] = 0;
    return -1;
}

static int find_const_i64_for_value(const cc_ssa_function_t *f, int value, size_t max_instr_index, long *out_imm) {
    int *def_index;
    unsigned char *visiting;
    size_t i;
    size_t limit;
    int rc;

    if (f == NULL || value < 0 || value >= f->value_count || out_imm == NULL) {
        return -1;
    }
    def_index = (int *)malloc((size_t)f->value_count * sizeof(*def_index));
    visiting = (unsigned char *)calloc((size_t)f->value_count, sizeof(*visiting));
    if (def_index == NULL || visiting == NULL) {
        free(def_index);
        free(visiting);
        return -1;
    }
    for (i = 0; i < (size_t)f->value_count; ++i) {
        def_index[i] = -1;
    }
    limit = max_instr_index + 1;
    if (limit > f->instr_count) {
        limit = f->instr_count;
    }
    for (i = 0; i < limit; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        if (in->dst >= 0 && in->dst < f->value_count) {
            def_index[in->dst] = (int)i;
        }
    }
    rc = eval_const_i64_for_value(f, def_index, value, visiting, out_imm);
    free(def_index);
    free(visiting);
    return rc;
}

static int find_def_instr_index_before(const cc_ssa_function_t *f, int value, size_t max_instr_index) {
    size_t i;

    if (f == NULL || value < 0 || f->instr_count == 0) {
        return -1;
    }
    if (max_instr_index >= f->instr_count) {
        max_instr_index = f->instr_count - 1;
    }
    for (i = max_instr_index + 1; i > 0; --i) {
        const cc_ssa_instr_t *in = &f->instrs[i - 1];
        if (in->dst == value) {
            return (int)(i - 1);
        }
    }
    return -1;
}

static int pointer_addr_key_before(const cc_ssa_function_t *f, int value, size_t max_instr_index, int depth,
                                   int *base_out, long *off_out) {
    int def_idx;
    const cc_ssa_instr_t *in;
    long imm;

    if (f == NULL || base_out == NULL || off_out == NULL || value < 0 || depth > 32) {
        return -1;
    }
    def_idx = find_def_instr_index_before(f, value, max_instr_index);
    if (def_idx < 0) {
        *base_out = value;
        *off_out = 0;
        return 0;
    }
    in = &f->instrs[def_idx];
    if (in->op == CC_SSA_MOV && in->lhs >= 0) {
        return pointer_addr_key_before(f, in->lhs, (size_t)def_idx, depth + 1, base_out, off_out);
    }
    if ((in->op == CC_SSA_ADD || in->op == CC_SSA_SUB) && in->lhs >= 0 && in->rhs >= 0) {
        int base;
        long off;
        if (pointer_addr_key_before(f, in->lhs, (size_t)def_idx, depth + 1, &base, &off) == 0 &&
            find_const_i64_for_value(f, in->rhs, (size_t)def_idx, &imm) == 0) {
            *base_out = base;
            *off_out = off + (in->op == CC_SSA_SUB ? -imm : imm);
            return 0;
        }
        if (in->op == CC_SSA_ADD &&
            pointer_addr_key_before(f, in->rhs, (size_t)def_idx, depth + 1, &base, &off) == 0 &&
            find_const_i64_for_value(f, in->lhs, (size_t)def_idx, &imm) == 0) {
            *base_out = base;
            *off_out = off + imm;
            return 0;
        }
    }
    if (in->op == CC_SSA_STACKALLOC || in->op == CC_SSA_ADDR || in->op == CC_SSA_GADDR || in->op == CC_SSA_LADDR) {
        *base_out = value;
        *off_out = 0;
        return 0;
    }
    *base_out = value;
    *off_out = 0;
    return 0;
}

static int find_store_instr_index_for_ptr_before(const cc_ssa_function_t *f, int ptr_value, size_t max_instr_index) {
    size_t i;
    int ptr_root;
    int ptr_base = -1;
    long ptr_off = 0;

    if (f == NULL || ptr_value < 0 || f->instr_count == 0) {
        return -1;
    }
    if (max_instr_index >= f->instr_count) {
        max_instr_index = f->instr_count - 1;
    }
    ptr_root = ptr_value;
    {
        int steps;
        for (steps = 0; steps < 32; ++steps) {
            int def_idx = find_def_instr_index_before(f, ptr_root, max_instr_index);
            if (def_idx < 0) {
                break;
            }
            if (f->instrs[def_idx].op == CC_SSA_MOV && f->instrs[def_idx].lhs >= 0) {
                ptr_root = f->instrs[def_idx].lhs;
                continue;
            }
            break;
        }
    }
    pointer_addr_key_before(f, ptr_value, max_instr_index, 0, &ptr_base, &ptr_off);
    for (i = max_instr_index + 1; i > 0; --i) {
        const cc_ssa_instr_t *in = &f->instrs[i - 1];
        if (in->op == CC_SSA_STORE && in->lhs >= 0 && in->rhs >= 0) {
            int lhs_root = in->lhs;
            int lhs_base = -1;
            long lhs_off = 0;
            int steps;
            for (steps = 0; steps < 32; ++steps) {
                int def_idx = find_def_instr_index_before(f, lhs_root, (size_t)(i - 1));
                if (def_idx < 0) {
                    break;
                }
                if (f->instrs[def_idx].op == CC_SSA_MOV && f->instrs[def_idx].lhs >= 0) {
                    lhs_root = f->instrs[def_idx].lhs;
                    continue;
                }
                break;
            }
            if (lhs_root == ptr_root) {
                return (int)(i - 1);
            }
            if (ptr_base >= 0 &&
                pointer_addr_key_before(f, in->lhs, (size_t)(i - 1), 0, &lhs_base, &lhs_off) == 0 &&
                lhs_base == ptr_base && lhs_off == ptr_off) {
                return (int)(i - 1);
            }
        }
    }
    return -1;
}

static int resolve_asm_immediate_symbol(const cc_ssa_function_t *f, int value, size_t max_instr_index, size_t fn_index,
                                        int depth, char *out, size_t outsz) {
    int def_idx;
    int store_idx;
    long imm;
    const cc_ssa_instr_t *def;

    if (f == NULL || value < 0 || out == NULL || outsz == 0) {
        return -1;
    }
    if (depth > 32) {
        return -1;
    }
    if (find_const_i64_for_value(f, value, max_instr_index, &imm) == 0) {
        snprintf(out, outsz, "$%ld", imm);
        return 0;
    }

    def_idx = find_def_instr_index_before(f, value, max_instr_index);
    if (def_idx < 0) {
        return -1;
    }
    def = &f->instrs[def_idx];

    if (def->op == CC_SSA_MOV) {
        return resolve_asm_immediate_symbol(f, def->lhs, max_instr_index, fn_index, depth + 1, out, outsz);
    }
    if (def->op == CC_SSA_LOAD && def->lhs >= 0) {
        store_idx = find_store_instr_index_for_ptr_before(f, def->lhs, (size_t)def_idx);
        if (store_idx >= 0) {
            const cc_ssa_instr_t *st = &f->instrs[store_idx];
            if (find_const_i64_for_value(f, st->rhs, (size_t)store_idx, &imm) == 0) {
                snprintf(out, outsz, "$%ld", imm);
                return 0;
            }
            if (resolve_asm_immediate_symbol(f, st->rhs, (size_t)store_idx, fn_index, depth + 1, out, outsz) == 0) {
                return 0;
            }
        }
        for (store_idx = def_idx - 1; store_idx >= 0; --store_idx) {
            const cc_ssa_instr_t *st = &f->instrs[store_idx];
            if (st->op != CC_SSA_STORE || st->rhs < 0) {
                continue;
            }
            if (find_const_i64_for_value(f, st->rhs, (size_t)store_idx, &imm) == 0) {
                snprintf(out, outsz, "$%ld", imm);
                return 0;
            }
            if (resolve_asm_immediate_symbol(f, st->rhs, (size_t)store_idx, fn_index, depth + 1, out, outsz) == 0) {
                return 0;
            }
        }
        for (store_idx = def_idx - 1; store_idx >= 0; --store_idx) {
            const cc_ssa_instr_t *prev = &f->instrs[store_idx];
            if (prev->op == CC_SSA_CONST) {
                snprintf(out, outsz, "$%lld", prev->imm);
                return 0;
            }
        }
        snprintf(out, outsz, "$0");
        return 0;
    }
    if (def->op == CC_SSA_CONST) {
        snprintf(out, outsz, "$%lld", def->imm);
        return 0;
    }
    if (def->op == CC_SSA_STR) {
        snprintf(out, outsz, "$.L__cc_str_%zu_%d", fn_index, def_idx);
        return 0;
    }
    if (def->op == CC_SSA_GADDR && def->sym != NULL && def->sym[0] != '\0') {
        snprintf(out, outsz, "$%s", def->sym);
        return 0;
    }
    if (def->op == CC_SSA_LADDR) {
        snprintf(out, outsz, "$.L%s_%d", f->name != NULL ? f->name : "__fn", def->label);
        return 0;
    }
    if ((def->op == CC_SSA_ADD || def->op == CC_SSA_SUB) && def->lhs >= 0 && def->rhs >= 0) {
        /*
         * an address and a constant: Linux's static_cpu_has() tests `%a[cap_byte]`, "i" of
         * &boot_cpu_data.x86_capability[bit >> 3], gaddr + 40 + 1 -- which was `$0`, a test of address 0
         */
        int sym_side = def->lhs;
        int k_side = def->rhs;
        long k;
        size_t used;

        if (find_const_i64_for_value(f, k_side, max_instr_index, &k) != 0 && def->op == CC_SSA_ADD) {
            sym_side = def->rhs;
            k_side = def->lhs;
        }
        if (find_const_i64_for_value(f, k_side, max_instr_index, &k) != 0 ||
            resolve_asm_immediate_symbol(f, sym_side, max_instr_index, fn_index, depth + 1, out, outsz) != 0) {
            return -1;
        }
        used = strlen(out);
        if (def->op == CC_SSA_SUB) {
            k = -k;
        }
        {
            /* an offset already after the symbol (`sym+40`) takes this one in: `sym+41` */
            size_t at = used;

            while (at > 1 && out[at - 1] >= '0' && out[at - 1] <= '9') {
                at--;
            }
            if (at > 2 && at < used && (out[at - 1] == '+' || out[at - 1] == '-')) {
                k += strtol(out + at - 1, NULL, 10);
                used = at - 1;
                out[used] = '\0';
            }
        }
        if (k != 0 && snprintf(out + used, outsz - used, "%+ld", k) >= (int)(outsz - used)) {
            return -1;
        }
        return 0;
    }
    return -1;
}

static int c_escape_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

static void emit_asm_escaped_byte(FILE *fp, unsigned char ch) {
    if (ch == '\n') {
        fputs("\\n", fp);
        return;
    }
    if (ch == '\r') {
        fputs("\\r", fp);
        return;
    }
    if (ch == '\t') {
        fputs("\\t", fp);
        return;
    }
    if (ch == '\b') {
        fputs("\\b", fp);
        return;
    }
    if (ch == '\f') {
        fputs("\\f", fp);
        return;
    }
    if (ch == '\v') {
        fputs("\\v", fp);
        return;
    }
    if (ch == '"' || ch == '\\') {
        fputc('\\', fp);
        fputc((int)ch, fp);
        return;
    }
    if (ch >= 0x20 && ch < 0x7f) {
        fputc((int)ch, fp);
        return;
    }
    fprintf(fp, "\\%03o", (unsigned)ch);
}

static void emit_asciz_literal(FILE *fp, const char *literal) {
    const char *p = literal;
    int quoted = 0;

    if (p == NULL) {
        p = "\"\"";
    }
    if (*p == '"') {
        quoted = 1;
        p++;
    }

    fputs("\t.asciz \"", fp);
    while (*p != '\0') {
        unsigned char out;

        if (quoted && *p == '"') {
            break;
        }

        if (*p != '\\') {
            out = (unsigned char)*p;
            p++;
            emit_asm_escaped_byte(fp, out);
            continue;
        }

        p++;
        if (*p == '\0') {
            break;
        }

        switch (*p) {
        case 'a':
            out = '\a';
            p++;
            break;
        case 'b':
            out = '\b';
            p++;
            break;
        case 'f':
            out = '\f';
            p++;
            break;
        case 'n':
            out = '\n';
            p++;
            break;
        case 'r':
            out = '\r';
            p++;
            break;
        case 't':
            out = '\t';
            p++;
            break;
        case 'v':
            out = '\v';
            p++;
            break;
        case '\\':
            out = '\\';
            p++;
            break;
        case '"':
            out = '"';
            p++;
            break;
        case '\'':
            out = '\'';
            p++;
            break;
        case '?':
            out = '?';
            p++;
            break;
        case 'x': {
            int hv;
            unsigned value = 0;
            int seen = 0;

            p++;
            hv = c_escape_hex_value(*p);
            while (hv >= 0) {
                seen = 1;
                value = (value << 4) | (unsigned)hv;
                p++;
                hv = c_escape_hex_value(*p);
            }
            out = (unsigned char)(seen ? (value & 0xffu) : (unsigned)'x');
            break;
        }
        default:
            if (*p >= '0' && *p <= '7') {
                int digits = 0;
                unsigned value = 0;

                while (digits < 3 && *p >= '0' && *p <= '7') {
                    value = (value << 3) | (unsigned)(*p - '0');
                    p++;
                    digits++;
                }
                out = (unsigned char)(value & 0xffu);
            } else {
                out = (unsigned char)*p;
                p++;
            }
            break;
        }

        emit_asm_escaped_byte(fp, out);
    }
    fputs("\"\n", fp);
}

void emit_string_literal_label(FILE *fp, size_t fn_index, size_t instr_index, const char *literal,
                                      const char *restore_sec) {
    fprintf(fp, "\t.section .rodata\n");
    fprintf(fp, ".L__cc_str_%zu_%zu:\n", fn_index, instr_index);
    emit_asciz_literal(fp, literal);
    if (restore_sec != NULL && restore_sec[0] != '\0') {
        fprintf(fp, "\t.section %s,\"ax\",@progbits\n", restore_sec);
    } else {
        fprintf(fp, "\t.text\n");
    }
}

static void emit_data_section(FILE *fp, const char *section_name) {
    if (section_name != NULL && section_name[0] != '\0') {
        if (strncmp(section_name, ".note", 5) == 0) {
            fprintf(fp, "\t.section %s,\"a\",@note\n", section_name);
        } else {
            fprintf(fp, "\t.section %s,\"aw\",@progbits\n", section_name);
        }
    } else {
        fprintf(fp, "\t.data\n");
    }
}

static void emit_bss_section(FILE *fp, const char *section_name) {
    int is_nobits = 1;
    if (section_name != NULL && section_name[0] != '\0') {
        if (strcmp(section_name, ".bss") != 0 &&
            strstr(section_name, ".bss") == NULL &&
            strstr(section_name, ".tbss") == NULL) {
            is_nobits = 0;
        }
        fprintf(fp, "\t.section %s,\"aw\",@%s\n", section_name, is_nobits ? "nobits" : "progbits");
    } else {
        fprintf(fp, "\t.bss\n");
    }
}

void emit_text_section(FILE *fp, const char *section_name) {
    if (section_name != NULL && section_name[0] != '\0') {
        fprintf(fp, "\t.section %s,\"ax\",@progbits\n", section_name);
    } else {
        fprintf(fp, "\t.text\n");
    }
}

/**
 * Write each file-scope `asm("...")` of @p m as it was written, in source order, and go back to
 * .text after them: whatever section one of them leaves current, the code that follows is not in it.
 *
 * @param fp the assembly output
 * @param m  the module
 */
static void emit_toplevel_asm(FILE *fp, const cc_ssa_module_t *m) {
    size_t i;

    if (m->toplevel_asm_count == 0) {
        return;
    }
    for (i = 0; i < m->toplevel_asm_count; ++i) {
        fprintf(fp, "%s\n", m->toplevel_asm[i]);
    }
    fprintf(fp, "\t.text\n");
}

static void emit_compiler_stamp(FILE *fp) {
    /* in .comment, as gcc's: no part of what is loaded (it was in .rodata, 26 bytes in every program's image) */
    fprintf(fp, "\n\t.ident \"Substrate C Compiler v0.1\"\n");
}

void emit_visibility_attr(FILE *fp, const char *name, cc_attr_flags_t attr_flags) {
    if (name == NULL || name[0] == '\0') {
        return;
    }
    if ((attr_flags & CC_ATTR_VIS_HIDDEN) != 0) {
        fprintf(fp, ".hidden %s\n", name);
    } else if ((attr_flags & CC_ATTR_VIS_PROTECTED) != 0) {
        fprintf(fp, ".protected %s\n", name);
    } else if ((attr_flags & CC_ATTR_VIS_INTERNAL) != 0) {
        fprintf(fp, ".internal %s\n", name);
    }
}

void emit_lifecycle_attr(FILE *fp, const char *section, const char *section_type, const char *name, int is_64bit) {
    if (name == NULL || name[0] == '\0') {
        return;
    }
    fprintf(fp, ".section %s,\"aw\",@%s\n", section, section_type);
    fprintf(fp, "%s %s\n", is_64bit ? ".quad" : ".long", name);
}

static size_t decoded_c_string_len(const char *literal) {
    const char *p;
    size_t n = 0;
    if (literal == NULL) {
        return 0;
    }
    p = literal;
    if (*p == '"') {
        p++;
    }
    while (*p != '\0' && *p != '"') {
        if (*p != '\\') {
            n++;
            p++;
            continue;
        }
        p++;
        if (*p == '\0') {
            break;
        }
        if (*p == 'x') {
            int seen = 0;
            p++;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                seen = 1;
                p++;
            }
            n += seen ? 1 : 0;
            continue;
        }
        if (*p >= '0' && *p <= '7') {
            int digits = 0;
            while (digits < 3 && *p >= '0' && *p <= '7') {
                p++;
                digits++;
            }
            n++;
            continue;
        }
        n++;
        p++;
    }
    return n;
}

/**
 * The size of a global object: the size lowering gave it, else its
 * element size times its element count, the count taken from its
 * initializer where the declaration left it out.
 *
 * @param g             the global
 * @param pointer_size  the target's pointer size, for an object of no known size
 * @return              the size in bytes
 */
static long global_object_size_bytes(const cc_ssa_global_t *g, int pointer_size) {
    long sz;

    if (g->size_bytes > 0) {
        return g->size_bytes;
    }
    sz = g->elem_size;
    if (g->is_array) {
        long elem = g->elem_size > 0 ? g->elem_size : 1;
        long elems = g->array_len;

        if (elems <= 0 && g->init_item_count > 0) {
            elems = (long)g->init_item_count;
        } else if (elems <= 0 && g->init_is_string && g->data_kind == CC_SSA_DATA_CHAR) {
            elems = (long)(decoded_c_string_len(g->init_str) + 1);
        } else if (elems <= 0) {
            elems = 1;
        }
        sz = elem * elems;
    }
    if (sz <= 0) {
        sz = pointer_size;
    }
    return sz;
}

/* value is long long: a long is 32 bits when cc itself runs on i386 */
static void emit_integer_data(FILE *fp, long size, long long value) {
    if (size <= 1) {
        fprintf(fp, "\t.byte %lld\n", value);
    } else if (size == 2) {
        fprintf(fp, "\t.short %lld\n", value);
    } else if (size == 4) {
        fprintf(fp, "\t.long %lld\n", value);
    } else {
        fprintf(fp, "\t.quad %lld\n", value);
    }
}

static void emit_raw_bytes(FILE *fp, const unsigned char *bytes, long size) {
    long i;
    for (i = 0; i < size; ++i) {
        fprintf(fp, "\t.byte 0x%02x\n", bytes[i]);
    }
}

static void emit_float_data(FILE *fp, long size, long double value) {
    if (size == 2) {
        /* _Float16 */
        unsigned short bits = cc_float16_bits((double)value);
        unsigned char b[2];

        b[0] = (unsigned char)(bits & 0xff);
        b[1] = (unsigned char)(bits >> 8);
        emit_raw_bytes(fp, b, 2);
    } else if (size == 4) {
        union {
            float f;
            unsigned char b[4];
        } u;
        u.f = (float)value;
        emit_raw_bytes(fp, u.b, 4);
    } else if (size == 8) {
        union {
            double d;
            unsigned char b[8];
        } u;
        u.d = (double)value;
        emit_raw_bytes(fp, u.b, 8);
    } else {
        unsigned char raw[16];
        long ncopy = (long)sizeof(long double);
        memset(raw, 0, sizeof(raw));
#if LDBL_MANT_DIG == 64 && LDBL_MAX_EXP == 16384
        if (ncopy > 10) {
            ncopy = 10;
        }
#endif
        if (ncopy > 16) {
            ncopy = 16;
        }
        memcpy(raw, &value, (size_t)ncopy);
        emit_raw_bytes(fp, raw, size < 16 ? size : 16);
        if (size > 16) {
            long i;
            for (i = 16; i < size; ++i) {
                fprintf(fp, "\t.byte 0x00\n");
            }
        }
    }
}

static long default_object_align(long sz) {
    if (sz >= 8) {
        return 8;
    }
    if (sz >= 4) {
        return 4;
    }
    if (sz >= 2) {
        return 2;
    }
    return 1;
}

static int global_emit_rank(const cc_ssa_global_t *g) {
    int rank = 0;
    if (g == NULL) {
        return -1;
    }
    if ((g->storage & CC_STORAGE_EXTERN) == 0) {
        rank += 100;
    }
    if (g->has_init) {
        rank += 50;
    }
    if ((g->attr_flags & CC_ATTR_WEAK) == 0) {
        rank += 5;
    }
    return rank;
}

static size_t select_global_emit_index(const cc_ssa_module_t *m, size_t idx) {
    const cc_ssa_global_t *g;
    size_t j;
    size_t best;
    int best_rank;

    if (m == NULL || idx >= m->global_count) {
        return idx;
    }
    g = &m->globals[idx];
    if (g->name == NULL || g->name[0] == '\0') {
        return idx;
    }
    best = idx;
    best_rank = global_emit_rank(g);
    for (j = 0; j < m->global_count; ++j) {
        const cc_ssa_global_t *cand = &m->globals[j];
        int cand_rank;
        if (cand->name == NULL || strcmp(cand->name, g->name) != 0) {
            continue;
        }
        cand_rank = global_emit_rank(cand);
        if (cand_rank > best_rank) {
            best_rank = cand_rank;
            best = j;
        } else if (cand_rank == best_rank && j > best) {
            best = j;
        }
    }
    return best;
}

/**
 * Say that @p name is a data object of @p size bytes, as gcc does: an STT_OBJECT with its size in the symbol
 * table.  It was an untyped, unsized symbol, and a tool that looks for an object by its type found nothing
 * (Linux's scripts/sorttable, after main_extable_sort_needed, failed the vmlinux link).
 */
static void emit_object_type_size(FILE *fp, const char *name, long size) {
    fprintf(fp, ".type %s, @object\n", name);
    fprintf(fp, ".size %s, %ld\n", name, size > 0 ? size : 0);
}

static int emit_globals(FILE *fp, const cc_ssa_module_t *m, int pointer_size, cc_diag_t *diag) {
    size_t i;
    for (i = 0; i < m->global_count; ++i) {
        const cc_ssa_global_t *g = &m->globals[i];
        size_t best_i = select_global_emit_index(m, i);
        long sz = global_object_size_bytes(g, pointer_size);
        long align = default_object_align(sz);
        int is_static = (g->storage & CC_STORAGE_STATIC) != 0;
        int is_extern = (g->storage & CC_STORAGE_EXTERN) != 0;
        const char *data_sec = g->attr_section != NULL && g->attr_section[0] != '\0' ? g->attr_section : ".data";
        const char *bss_sec = g->attr_section != NULL && g->attr_section[0] != '\0' ? g->attr_section : ".bss";

        if (pointer_size == 4 && g->attr_section != NULL && g->attr_section[0] != '\0' && g->type_align > 0) {
            /*
             * i386, in a section of its own making: its type's alignment, as gcc has it.  The objects a
             * program puts in one are walked as an array of them (Linux's .videocards, of 28-byte
             * card_info: at 8, each was padded to 32, and probe_cards() called into the middle of the
             * next).  x86-64 keeps the size's, gcc aligning an aggregate of 16 bytes or more to 16 there.
             */
            align = g->type_align;
        }
        if ((g->attr_flags & CC_ATTR_PACKED) != 0) {
            align = 1;
        }
        if (g->type_align > align) {
            /* what its type needs: long double, __int128, vectors, members _Alignas raises */
            align = g->type_align;
        }
        if (g->attr_align > align) {
            align = g->attr_align;
        }

        if (g->name == NULL || g->name[0] == '\0') {
            set_diag(diag, "malformed global symbol");
            return -1;
        }
        if (best_i != i) {
            continue;
        }
        if ((g->attr_flags & CC_ATTR_ALIAS) != 0 && g->attr_alias != NULL && g->attr_alias[0] != '\0') {
            if (!is_static) {
                if ((g->attr_flags & CC_ATTR_WEAK) != 0) {
                    fprintf(fp, ".weak %s\n", g->name);
                } else {
                    fprintf(fp, ".globl %s\n", g->name);
                }
            }
            emit_visibility_attr(fp, g->name, g->attr_flags);
            fprintf(fp, ".set %s, %s\n", g->name, g->attr_alias);
            continue;
        }
        if (is_extern) {
            continue;
        }
        if (!g->has_init) {
            emit_bss_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? bss_sec : NULL);
            if (!is_static) {
                if ((g->attr_flags & CC_ATTR_WEAK) != 0) {
                    fprintf(fp, ".weak %s\n", g->name);
                } else {
                    fprintf(fp, ".globl %s\n", g->name);
                }
            }
            emit_visibility_attr(fp, g->name, g->attr_flags);
            if (align > 1) {
                fprintf(fp, ".align %ld\n", align);
            }
            emit_object_type_size(fp, g->name, sz);
            fprintf(fp, "%s:\n", g->name);
            fprintf(fp, "\t.zero %ld\n", sz);
            continue;
        }
        emit_data_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? data_sec : NULL);
        if (!is_static) {
            if ((g->attr_flags & CC_ATTR_WEAK) != 0) {
                fprintf(fp, ".weak %s\n", g->name);
            } else {
                fprintf(fp, ".globl %s\n", g->name);
            }
        }
        emit_visibility_attr(fp, g->name, g->attr_flags);
        if (align > 1) {
            fprintf(fp, ".align %ld\n", align);
        }
        emit_object_type_size(fp, g->name, sz);
        fprintf(fp, "%s:\n", g->name);
        if (g->init_item_count > 0) {
            if (g->init_items[0].init_size > 0 || g->init_items[0].init_is_zero_fill) {
                size_t j;
                long emitted = 0;

                for (j = 0; j < g->init_item_count; ++j) {
                    const cc_ssa_global_init_item_t *it = &g->init_items[j];
                    if (it->init_is_string) {
                        fprintf(fp, "\t.section .rodata\n");
                        fprintf(fp, ".L__cc_gstream_%zu_%zu:\n", i, j);
                        emit_asciz_literal(fp, it->init_str);
                        emit_data_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? data_sec : NULL);
                    }
                }

                for (j = 0; j < g->init_item_count; ++j) {
                    const cc_ssa_global_init_item_t *it = &g->init_items[j];
                    long item_size = it->init_size > 0 ? it->init_size : 1;
                    if (it->init_is_zero_fill) {
                        fprintf(fp, "\t.zero %ld\n", item_size);
                        emitted += item_size;
                        continue;
                    }
                    if (it->init_is_string) {
                        if (item_size == 4) {
                            fprintf(fp, "\t.long .L__cc_gstream_%zu_%zu\n", i, j);
                        } else if (item_size == 8) {
                            fprintf(fp, "\t.quad .L__cc_gstream_%zu_%zu\n", i, j);
                        } else {
                            set_diag(diag, "string initializer pointer size must be 4 or 8 bytes");
                            return -1;
                        }
                        emitted += item_size;
                        continue;
                    }
                    if (it->init_is_symbol) {
                        if (item_size == 4) {
                            fprintf(fp, "\t.long %s\n", it->init_sym != NULL ? it->init_sym : "0");
                        } else if (item_size == 8) {
                            fprintf(fp, "\t.quad %s\n", it->init_sym != NULL ? it->init_sym : "0");
                        } else {
                            set_diag(diag, "symbol initializer size must be 4 or 8 bytes");
                            return -1;
                        }
                        emitted += item_size;
                        continue;
                    }
                    if (it->init_is_float && (item_size == 4 || item_size == 8 || item_size == 16)) {
                        emit_float_data(fp, item_size, it->init_f_ld);
                        emitted += item_size;
                        continue;
                    }
                    if (item_size != 1 && item_size != 2 && item_size != 4 && item_size != 8) {
                        set_diag(diag, "unsupported generic global initializer item size");
                        return -1;
                    }
                    emit_integer_data(fp, item_size, it->init_i);
                    emitted += item_size;
                }
                if (emitted < sz) {
                    fprintf(fp, "\t.zero %ld\n", sz - emitted);
                } else if (emitted > sz) {
                    set_diag(diag, "generic global initializer stream exceeds object size");
                    return -1;
                }
                continue;
            }

            size_t j;
            cc_ssa_data_kind_t elem_kind = g->data_kind;
            long elem_size;
            long arr_elems;
            if (!g->is_array) {
                set_diag(diag, "initializer list requires array global");
                return -1;
            }
            elem_size = g->elem_size;
            if (elem_size <= 0) {
                set_diag(diag, "unsupported array element type in global initializer");
                return -1;
            }
            arr_elems = g->array_len > 0 ? g->array_len : (long)g->init_item_count;
            if (elem_size == 1 && elem_kind == CC_SSA_DATA_CHAR) {
                long emitted_bytes = 0;
                for (j = 0; j < g->init_item_count; ++j) {
                    const cc_ssa_global_init_item_t *it = &g->init_items[j];
                    if (it->init_is_string) {
                        size_t slen = decoded_c_string_len(it->init_str);
                        emit_asciz_literal(fp, it->init_str);
                        emitted_bytes += (long)(slen + 1);
                    } else {
                        emit_integer_data(fp, elem_size, it->init_i);
                        emitted_bytes += elem_size;
                    }
                }
                if (emitted_bytes < sz) {
                    fprintf(fp, "\t.zero %ld\n", sz - emitted_bytes);
                } else if (emitted_bytes > sz) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "string/byte initializer list for %s exceeds array size",
                                 g->name != NULL ? g->name : "<anon>");
                    }
                    return -1;
                }
                continue;
            }
            for (j = 0; j < g->init_item_count; ++j) {
                const cc_ssa_global_init_item_t *it = &g->init_items[j];
                if (!it->init_is_string) {
                    continue;
                }
                if (elem_kind != CC_SSA_DATA_POINTER) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "string element in initializer list for %s requires pointer element type",
                                 g->name != NULL ? g->name : "<anon>");
                    }
                    return -1;
                }
                fprintf(fp, "\t.section .rodata\n");
                fprintf(fp, ".L__cc_garr_%zu_%zu:\n", i, j);
                emit_asciz_literal(fp, it->init_str);
                emit_data_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? data_sec : NULL);
            }
            for (j = 0; j < g->init_item_count; ++j) {
                const cc_ssa_global_init_item_t *it = &g->init_items[j];
                if (it->init_is_string) {
                    if (pointer_size == 4) {
                        fprintf(fp, "\t.long .L__cc_garr_%zu_%zu\n", i, j);
                    } else {
                        fprintf(fp, "\t.quad .L__cc_garr_%zu_%zu\n", i, j);
                    }
                } else if (it->init_is_symbol) {
                    if (elem_kind != CC_SSA_DATA_POINTER) {
                        set_diag(diag, "symbol element in initializer list requires pointer element type");
                        return -1;
                    }
                    if (pointer_size == 4) {
                        fprintf(fp, "\t.long %s\n", it->init_sym != NULL ? it->init_sym : "0");
                    } else {
                        fprintf(fp, "\t.quad %s\n", it->init_sym != NULL ? it->init_sym : "0");
                    }
                } else if (it->init_is_float && elem_kind == CC_SSA_DATA_FLOAT) {
                    emit_float_data(fp, elem_size, it->init_f_ld);
                } else if (it->init_is_float && elem_kind == CC_SSA_DATA_BOOL) {
                    /* 0.25 is true; its truncation to an integer is not */
                    emit_integer_data(fp, elem_size, it->init_f_ld != 0.0L);
                } else {
                    emit_integer_data(fp, elem_size, it->init_i);
                }
            }
            if (g->size_bytes > 0) {
                /* the object's own size, which the legacy element count can overstate */
                if (sz > (long)g->init_item_count * elem_size) {
                    fprintf(fp, "\t.zero %ld\n", sz - (long)g->init_item_count * elem_size);
                }
            } else if (arr_elems > (long)g->init_item_count) {
                fprintf(fp, "\t.zero %ld\n", (arr_elems - (long)g->init_item_count) * elem_size);
            }
            continue;
        }
        if (g->init_is_string) {
            if (g->is_array) {
                size_t slen = decoded_c_string_len(g->init_str);
                emit_asciz_literal(fp, g->init_str);
                if ((long)(slen + 1) < sz) {
                    fprintf(fp, "\t.zero %ld\n", sz - (long)(slen + 1));
                }
            } else if (g->data_kind == CC_SSA_DATA_POINTER) {
                fprintf(fp, "\t.section .rodata\n");
                fprintf(fp, ".L__cc_gstr_%zu:\n", i);
                emit_asciz_literal(fp, g->init_str);
                emit_data_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? data_sec : NULL);
                if (pointer_size == 4) {
                    fprintf(fp, "\t.long .L__cc_gstr_%zu\n", i);
                } else {
                    fprintf(fp, "\t.quad .L__cc_gstr_%zu\n", i);
                }
            } else {
                set_diag(diag, "string initializer requires array or pointer global");
                return -1;
            }
            continue;
        }
        if (g->init_is_symbol) {
            if (!g->is_array && (g->data_kind == CC_SSA_DATA_FLOAT || g->data_kind == CC_SSA_DATA_OTHER)) {
                snprintf(diag->message, sizeof(diag->message), "symbol initializer requires pointer/integer global: %s",
                         g->name != NULL ? g->name : "<anon>");
                return -1;
            }
            if (sz <= 4 || pointer_size == 4) {
                fprintf(fp, "\t.long %s\n", g->init_sym != NULL ? g->init_sym : "0");
            } else {
                fprintf(fp, "\t.quad %s\n", g->init_sym != NULL ? g->init_sym : "0");
            }
            continue;
        }
        if (g->init_is_float && !g->is_array && g->data_kind == CC_SSA_DATA_FLOAT) {
            emit_float_data(fp, sz, g->init_f_ld);
            continue;
        }
        emit_integer_data(fp, sz, g->init_i);
    }
    fprintf(fp, ".text\n");
    return 0;
}

int module_symbol_is_extern_global(const cc_ssa_module_t *m, const char *sym) {
    size_t i;

    if (m == NULL || sym == NULL || sym[0] == '\0') {
        return 0;
    }
    for (i = 0; i < m->global_count; ++i) {
        const cc_ssa_global_t *g = &m->globals[i];

        if (g->name == NULL) {
            continue;
        }
        if (strcmp(g->name, sym) != 0) {
            continue;
        }
        return (g->storage & CC_STORAGE_EXTERN) != 0 ? 1 : 0;
    }
    return 0;
}

int module_symbol_binds_locally(const cc_ssa_module_t *m, const char *sym) {
    const cc_attr_flags_t local_vis = CC_ATTR_VIS_HIDDEN | CC_ATTR_VIS_PROTECTED | CC_ATTR_VIS_INTERNAL;
    const cc_options_t *opts = cc_options_current();
    int pie = opts != NULL && opts->pie; /* an executable's own symbols cannot be preempted */
    size_t n;
    size_t i;

    if (m == NULL || sym == NULL || sym[0] == '\0') {
        return 0;
    }
    if (sym[0] == '.') {
        return 1; /* a local label: a string literal's, a constant's */
    }
    /* `name+8`: the symbol is the name */
    n = strcspn(sym, "+-");
    for (i = 0; i < m->global_count; ++i) {
        const cc_ssa_global_t *g = &m->globals[i];

        if (g->name != NULL && strlen(g->name) == n && strncmp(g->name, sym, n) == 0) {
            return (g->storage & CC_STORAGE_STATIC) != 0 || (g->attr_flags & local_vis) != 0 ||
                   (pie && (g->storage & CC_STORAGE_EXTERN) == 0);
        }
    }
    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *fn = &m->funcs[i];

        if (fn->name != NULL && strlen(fn->name) == n && strncmp(fn->name, sym, n) == 0) {
            /* a function of the module is one defined here */
            return (fn->storage & CC_STORAGE_STATIC) != 0 || (fn->attr_flags & local_vis) != 0 || pie;
        }
    }
    for (i = 0; i < m->local_decl_count; ++i) {
        /* one only declared, static or hidden: Linux's decompressor passes its hidden error() by address */
        if (strlen(m->local_decls[i]) == n && strncmp(m->local_decls[i], sym, n) == 0) {
            return 1;
        }
    }
    return 0;
}

int slot_off(const slot_layout_t *lay, int v) {
    if (lay->word_of != NULL && lay->word_of[v] >= 0) {
        return -lay->slot_size * lay->slot_count - lay->bias - 4 * (lay->word_of[v] + 1);
    }
    return -lay->slot_size * (lay->slot_of[v] + 1) - lay->bias;
}

int stackalloc_off(const slot_layout_t *lay, int v) {
    if (lay == NULL || lay->stackalloc_off == NULL || v < 0 || v >= lay->value_count) {
        return 0;
    }
    return lay->stackalloc_off[v];
}

long value_size_bytes(const cc_ssa_function_t *f, int v, long fallback) {
    size_t i;
    if (fallback <= 0) {
        fallback = 4;
    }
    if (f == NULL || v < 0 || v >= f->value_count || cc_ssa_value_size(f, v) == 0) {
        return fallback;
    }
    if (f->param_values != NULL && f->param_abi != NULL) {
        for (i = 0; i < f->param_count; ++i) {
            if (f->param_values[i] == v && f->param_abi[i] == CC_CALL_ARG_ABI_LDOUBLE) {
                return 16;
            }
        }
    }
    return cc_ssa_value_size(f, v);
}

int is_stackalloc_value(const slot_layout_t *lay, int v) {
    return stackalloc_off(lay, v) != 0;
}

static void emit_frame_load_reg(FILE *fp, int is_64bit, long off, const char *reg) {
    if (is_64bit) {
        fprintf(fp, "\tmovq %ld(%%rbp), %s\n", off, reg);
    } else {
        fprintf(fp, "\tmovl %ld(%%ebp), %s\n", off, reg);
    }
}

static void emit_frame_store_reg(FILE *fp, int is_64bit, const char *reg, long off) {
    if (is_64bit) {
        fprintf(fp, "\tmovq %s, %ld(%%rbp)\n", reg, off);
    } else {
        fprintf(fp, "\tmovl %s, %ld(%%ebp)\n", reg, off);
    }
}

void emit_i386_copy_f64_param_to_slot(FILE *fp, const slot_layout_t *lay, int dst, int poff) {
    emit_frame_load_reg(fp, 0, poff, "%eax");
    emit_frame_store_reg(fp, 0, "%eax", slot_off(lay, dst));
    emit_frame_load_reg(fp, 0, poff + 4, "%eax");
    emit_frame_store_reg(fp, 0, "%eax", slot_off(lay, dst) + 4);
}

void int_regs_free(int_reg_state_t *st) {
    if (st == NULL) {
        return;
    }
    free(st->reg_val);
    free(st->val_reg);
    free(st->reg_dirty);
    free(st->reg_age);
    memset(st, 0, sizeof(*st));
}

int int_regs_init(int_reg_state_t *st, const cc_ssa_function_t *f, const char *const *regs, int reg_count, int is_64bit,
                         cc_diag_t *diag) {
    int i;
    memset(st, 0, sizeof(*st));
    st->regs = regs;
    st->reg_count = reg_count;
    st->is_64bit = is_64bit;

    st->reg_val = (int *)malloc((size_t)reg_count * sizeof(*st->reg_val));
    st->val_reg = (int *)malloc((size_t)(f->value_count > 0 ? f->value_count : 1) * sizeof(*st->val_reg));
    st->reg_dirty = (unsigned char *)malloc((size_t)reg_count * sizeof(*st->reg_dirty));
    st->reg_age = (unsigned long *)malloc((size_t)reg_count * sizeof(*st->reg_age));
    if (st->reg_val == NULL || st->val_reg == NULL || st->reg_dirty == NULL || st->reg_age == NULL) {
        int_regs_free(st);
        set_diag(diag, "out of memory initializing register allocator");
        return -1;
    }
    for (i = 0; i < reg_count; ++i) {
        st->reg_val[i] = -1;
        st->reg_dirty[i] = 0;
        st->reg_age[i] = 0;
    }
    for (i = 0; i < f->value_count; ++i) {
        st->val_reg[i] = -1;
    }
    st->tick = 1;
    st->cur_index = -1;
    st->alloc_count = reg_count;
    st->home = NULL;
    return 0;
}

int int_reg_index(const int_reg_state_t *st, const char *name) {
    int i;
    for (i = 0; i < st->reg_count; ++i) {
        if (strcmp(st->regs[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

int int_regs_spill_reg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int r) {
    int v;
    long off;
    if (r < 0 || r >= st->reg_count) {
        return 0;
    }
    v = st->reg_val[r];
    if (v < 0) {
        st->reg_dirty[r] = 0;
        return 0;
    }
    /*
     * On i386, a value written back after its last use stays in the register: its slot, if shared
     * (slot_end), may be another value's by now, and nothing reads it again -- a 64-bit one's high word
     * too, read only by what names the value.  (Only the i386 emitter keeps values in registers across
     * instructions; x86-64's flushes after each.)
     */
    if (st->reg_dirty[r] && cc_ssa_value_class(f, v) == CC_VAL_I64 && !is_stackalloc_value(lay, v) &&
        !(!st->is_64bit && lay->slot_end != NULL && v < lay->value_count && lay->slot_end[v] >= 0 &&
          st->cur_index > lay->slot_end[v])) {
        off = slot_off(lay, v);
        emit_frame_store_reg(fp, st->is_64bit, st->regs[r], off);
    }
    st->val_reg[v] = -1;
    st->reg_val[r] = -1;
    st->reg_dirty[r] = 0;
    return 0;
}

int int_regs_flush(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st) {
    int i;
    for (i = 0; i < st->reg_count; ++i) {
        int_regs_spill_reg(fp, f, lay, st, i);
    }
    return 0;
}

int int_regs_clobber_reg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st,
                                int reg) {
    if (reg < 0 || reg >= st->reg_count) {
        return 0;
    }
    return int_regs_spill_reg(fp, f, lay, st, reg);
}

static int instr_uses_value(const cc_ssa_instr_t *in, int value) {
    size_t i;
    if (in == NULL || value < 0) {
        return 0;
    }
    switch (in->op) {
    case CC_SSA_MOV:
    case CC_SSA_LOAD:
    case CC_SSA_ADDR:
    case CC_SSA_I2F:
    case CC_SSA_F2I:
    case CC_SSA_FROUND32:
    case CC_SSA_FROUND64:
    case CC_SSA_EXT:
    case CC_SSA_LO32:
    case CC_SSA_HI32:
        return in->lhs == value;
    case CC_SSA_PAIR:
    case CC_SSA_UMULW:
    case CC_SSA_STORE:
    case CC_SSA_DYNALLOC:
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_DIV:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
    case CC_SSA_CMP:
        return in->lhs == value || in->rhs == value;
    case CC_SSA_BR_COND:
    case CC_SSA_RET:
        return in->lhs == value;
    case CC_SSA_CALLI:
        if (in->lhs == value) {
            return 1;
        }
        /* fallthrough */
    case CC_SSA_CALL:
        if (in->call_chain > 0 && in->call_chain - 1 == value) {
            return 1;
        }
        for (i = 0; i < in->arg_count; ++i) {
            if (in->args[i] == value) {
                return 1;
            }
        }
        return 0;
    case CC_SSA_ASM:
        for (i = 0; i < in->asm_in_count; ++i) {
            int v = in->asm_in_values != NULL ? in->asm_in_values[i] : -1;

            if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
                v = CC_SSA_ASM_MEM_INDIRECT_DECODE(v);
            }
            if (v >= 0 && v == value) {
                return 1;
            }
        }
        return 0;
    default:
        return 0;
    }
}

static int value_next_use(const cc_ssa_function_t *f, int value, int at_index) {
    size_t i;
    if (f == NULL || value < 0 || at_index < -1) {
        return -1;
    }
    for (i = (size_t)(at_index + 1); i < f->instr_count; ++i) {
        if (instr_uses_value(&f->instrs[i], value)) {
            return (int)i;
        }
    }
    return -1;
}

static int int_regs_alloc(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int prefer,
                          int avoid) {
    int i;
    int victim = -1;
    int *next_use = NULL;

    if (prefer >= 0 && prefer < st->alloc_count) {
        if (st->reg_val[prefer] < 0) {
            return prefer;
        }
    }

    for (i = 0; i < st->alloc_count; ++i) {
        if (i == avoid) {
            continue;
        }
        if (st->reg_val[i] < 0) {
            return i;
        }
    }

    next_use = (int *)malloc((size_t)st->reg_count * sizeof(*next_use));
    if (next_use != NULL) {
        for (i = 0; i < st->reg_count; ++i) {
            if (st->reg_val[i] >= 0) {
                next_use[i] = value_next_use(f, st->reg_val[i], st->cur_index);
            } else {
                next_use[i] = -1;
            }
        }
        victim = cc_backend_pick_spill_victim(st->reg_val, next_use, st->reg_dirty, st->alloc_count, avoid,
                                              prefer < st->alloc_count ? prefer : -1);
        free(next_use);
    } else {
        victim = cc_backend_pick_spill_victim(st->reg_val, NULL, st->reg_dirty, st->alloc_count, avoid,
                                              prefer < st->alloc_count ? prefer : -1);
    }

    if (victim < 0) {
        victim = 0;
    }
    int_regs_spill_reg(fp, f, lay, st, victim);
    return victim;
}

int int_regs_bind(int_reg_state_t *st, int value, int reg, int dirty) {
    int old;
    if (value < 0 || reg < 0 || reg >= st->reg_count) {
        return -1;
    }
    old = st->val_reg[value];
    if (old >= 0 && old < st->reg_count && old != reg && st->reg_val[old] == value) {
        st->reg_val[old] = -1;
        st->reg_dirty[old] = 0;
    }
    if (st->reg_val[reg] >= 0 && st->reg_val[reg] != value) {
        st->val_reg[st->reg_val[reg]] = -1;
    }
    st->val_reg[value] = reg;
    st->reg_val[reg] = value;
    st->reg_dirty[reg] = (unsigned char)(dirty ? 1 : 0);
    st->reg_age[reg] = st->tick++;
    return reg;
}

int int_regs_load(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int value, int prefer,
                         int avoid) {
    int reg;
    long off;
    if (value < 0 || value >= f->value_count) {
        return -1;
    }
    if (st->home != NULL && st->home[value] >= 0) {
        /* it lives in its home register: read there, written only by moving a result in (i386) */
        return st->home[value];
    }
    reg = st->val_reg[value];
    if (reg >= 0 && reg < st->reg_count) {
        st->reg_age[reg] = st->tick++;
        return reg;
    }
    reg = int_regs_alloc(fp, f, lay, st, prefer, avoid);
    if (st->remat != NULL && st->remat[value]) {
        /*
         * a constant is made where it is read; its slot is written only for what reads that (i386).  Zero
         * is xorl, shorter: no flags are live where an operand is loaded (a compare reads its operands
         * first, and only stores come between it and the flags' reader).
         */
        if (st->remat[value] == 2) {
            fprintf(fp, "\tmovl $%s%+ld, %s\n", st->remat_sym[value], (long)st->remat_imm[value], st->regs[reg]);
        } else if ((unsigned int)st->remat_imm[value] == 0) {
            fprintf(fp, "\txorl %s, %s\n", st->regs[reg], st->regs[reg]);
        } else {
            fprintf(fp, "\tmovl $%ld, %s\n", (long)(int)(unsigned int)st->remat_imm[value], st->regs[reg]);
        }
        int_regs_bind(st, value, reg, 0);
        return reg;
    }
    if (is_stackalloc_value(lay, value)) {
        off = stackalloc_off(lay, value);
        if (st->is_64bit) {
            fprintf(fp, "\tleaq %ld(%%rbp), %s\n", off, st->regs[reg]);
        } else {
            fprintf(fp, "\tleal %ld(%%ebp), %s\n", off, st->regs[reg]);
        }
        int_regs_bind(st, value, reg, 0);
        return reg;
    }
    off = slot_off(lay, value);
    if (st->is_64bit) {
        fprintf(fp, "\tmovq %ld(%%rbp), %s\n", off, st->regs[reg]);
    } else {
        fprintf(fp, "\tmovl %ld(%%ebp), %s\n", off, st->regs[reg]);
    }
    int_regs_bind(st, value, reg, 0);
    return reg;
}

int int_regs_define(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int value, int prefer,
                           int avoid) {
    int reg = int_regs_alloc(fp, f, lay, st, prefer, avoid);
    int_regs_bind(st, value, reg, 1);
    return reg;
}

/**
 * Drop value @p v's register, without writing it back: its slot is about
 * to be written directly.
 *
 * @param st  the register state
 * @param v   the value
 */
void int_regs_forget(int_reg_state_t *st, int v) {
    int r = st->val_reg[v];

    if (r >= 0 && r < st->reg_count && st->reg_val[r] == v) {
        st->reg_val[r] = -1;
        st->reg_dirty[r] = 0;
    }
    st->val_reg[v] = -1;
}

int int_regs_scratch(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int avoid) {
    return int_regs_alloc(fp, f, lay, st, -1, avoid);
}

int int_regs_remap_dst(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int dst, int reg) {
    if (reg < 0 || reg >= st->reg_count) {
        return -1;
    }
    if (reg >= st->alloc_count) {
        /* a value's home register (i386) stays its: the result is a copy in one of the cache's */
        int t = int_regs_alloc(fp, f, lay, st, -1, -1);

        fprintf(fp, "\tmovl %s, %s\n", st->regs[reg], st->regs[t]);
        reg = t;
    }
    if (st->reg_val[reg] >= 0 && st->reg_val[reg] != dst && st->reg_dirty[reg]) {
        int_regs_spill_reg(fp, f, lay, st, reg);
    }
    return int_regs_bind(st, dst, reg, 1);
}

/**
 * Clear the save slot of every DYNALLOC that has one, so that the first
 * execution of each finds no earlier allocation to give back.  Emitted once,
 * right after the prologue.
 *
 * @param fp        output
 * @param f         the function
 * @param lay       its slot layout
 * @param is_64bit  nonzero for x86-64
 * @param diag      receives an error
 * @return          0, or -1 if a save slot has no frame offset
 */
int emit_dynalloc_slot_clears(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int is_64bit,
                                     cc_diag_t *diag) {
    size_t i;

    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        int off;

        if (in->op != CC_SSA_DYNALLOC || in->rhs < 0) {
            continue;
        }
        off = stackalloc_off(lay, in->rhs);
        if (off == 0) {
            set_diag(diag, "missing stack allocation offset for a dynamic allocation's save slot");
            return -1;
        }
        fprintf(fp, is_64bit ? "\tmovq $0, %d(%%rbp)\n" : "\tmovl $0, %d(%%ebp)\n", off);
    }
    return 0;
}

/**
 * Emit a DYNALLOC: take lhs bytes, rounded up to 16, off the stack and leave
 * the block's (16-aligned) address in dst.  With a save slot (rhs), the stack pointer is
 * first put back where the previous execution of this instruction found it,
 * then recorded there; so a variable-length array declared in a loop reuses
 * its storage instead of growing the stack each time round, and the storage
 * of anything allocated after it on the previous pass (whose scope has ended)
 * is given back with it.  Without one (alloca) the block lives until the
 * function returns.  Both back ends address every slot from the frame
 * pointer, so moving the stack pointer here disturbs nothing.
 *
 * @param fp        output
 * @param f         the function
 * @param lay       its slot layout
 * @param st        the integer register state
 * @param in        the instruction
 * @param index     its index, to name a private label
 * @param diag      receives an error
 * @return          0, or -1 on error
 */
int emit_dynalloc(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st,
                         const cc_ssa_instr_t *in, size_t index, cc_diag_t *diag) {
    const char *sp = st->is_64bit ? "%rsp" : "%esp";
    char sfx = st->is_64bit ? 'q' : 'l';
    int rs;
    int rd;

    rs = int_regs_load(fp, f, lay, st, in->lhs, -1, -1);
    rd = int_regs_define(fp, f, lay, st, in->dst, -1, rs);
    if (rs < 0 || rd < 0) {
        set_diag(diag, "out of registers for a dynamic stack allocation");
        return -1;
    }
    if (in->rhs >= 0) {
        int off = stackalloc_off(lay, in->rhs);
        const char *fp_reg = st->is_64bit ? "%rbp" : "%ebp";

        if (off == 0) {
            set_diag(diag, "missing stack allocation offset for a dynamic allocation's save slot");
            return -1;
        }
        fprintf(fp, "\tmov%c %d(%s), %s\n", sfx, off, fp_reg, st->regs[rd]);
        fprintf(fp, "\ttest%c %s, %s\n", sfx, st->regs[rd], st->regs[rd]);
        fprintf(fp, "\tjz .L__cc_dyn_%s_%zu\n", f->name, index);
        fprintf(fp, "\tmov%c %s, %s\n", sfx, st->regs[rd], sp);
        fprintf(fp, ".L__cc_dyn_%s_%zu:\n", f->name, index);
        fprintf(fp, "\tmov%c %s, %d(%s)\n", sfx, sp, off, fp_reg);
    }
    fprintf(fp, "\tmov%c %s, %s\n", sfx, st->regs[rs], st->regs[rd]);
    fprintf(fp, "\tadd%c $15, %s\n", sfx, st->regs[rd]);
    fprintf(fp, "\tand%c $-16, %s\n", sfx, st->regs[rd]);
    fprintf(fp, "\tsub%c %s, %s\n", sfx, st->regs[rd], sp);
    /* the i386 back end does not keep the stack pointer 16-aligned; align the block itself */
    fprintf(fp, "\tand%c $-16, %s\n", sfx, sp);
    fprintf(fp, "\tmov%c %s, %s\n", sfx, sp, st->regs[rd]);
    return 0;
}

void slot_layout_free(slot_layout_t *lay) {
    if (lay == NULL) {
        return;
    }
    free(lay->slot_of);
    free(lay->stackalloc_off);
    free(lay->live_end);
    free(lay->slot_end);
    free(lay->word_of);
    lay->slot_of = NULL;
    lay->stackalloc_off = NULL;
    lay->live_end = NULL;
    lay->slot_end = NULL;
    lay->word_of = NULL;
    lay->word_count = 0;
    lay->value_count = 0;
    lay->slot_count = 0;
    lay->slot_size = 0;
    lay->frame_bytes = 0;
}

static int allocate_slot(int *free_slots, int *free_count, int *next_slot) {
    int i;
    int best_i = -1;
    int best_slot = 0;

    if (*free_count == 0) {
        return (*next_slot)++;
    }

    for (i = 0; i < *free_count; ++i) {
        if (best_i < 0 || free_slots[i] < best_slot) {
            best_i = i;
            best_slot = free_slots[i];
        }
    }
    free_slots[best_i] = free_slots[*free_count - 1];
    (*free_count)--;
    return best_slot;
}

typedef void (*ssa_value_visit_fn)(int v, void *ctx);

static void for_each_instr_def(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, ssa_value_visit_fn fn, void *ctx) {
    size_t i;

    if (f == NULL || in == NULL || fn == NULL) {
        return;
    }
    if (in->dst >= 0 && in->dst < f->value_count) {
        fn(in->dst, ctx);
    }
    if (in->op == CC_SSA_ASM && in->asm_out_values != NULL) {
        for (i = 0; i < in->asm_out_count; ++i) {
            int v = in->asm_out_values[i];
            if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
                v = CC_SSA_ASM_MEM_INDIRECT_DECODE(v);
            }
            if (v >= 0 && v < f->value_count) {
                fn(v, ctx);
            }
        }
    }
}

static void for_each_instr_use(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, ssa_value_visit_fn fn, void *ctx) {
    size_t i;

    if (in == NULL || f == NULL || fn == NULL) {
        return;
    }
    switch (in->op) {
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_DIV:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
    case CC_SSA_CMP:
    case CC_SSA_PAIR:
    case CC_SSA_UMULW:
        if (in->lhs >= 0 && in->lhs < f->value_count) fn(in->lhs, ctx);
        if (in->rhs >= 0 && in->rhs < f->value_count) fn(in->rhs, ctx);
        break;
    case CC_SSA_ADDR:
    case CC_SSA_LOAD:
    case CC_SSA_MOV:
    case CC_SSA_I2F:
    case CC_SSA_F2I:
    case CC_SSA_FROUND32:
    case CC_SSA_FROUND64:
    case CC_SSA_EXT:
    case CC_SSA_LO32:
    case CC_SSA_HI32:
    case CC_SSA_BR_COND:
        if (in->lhs >= 0 && in->lhs < f->value_count) fn(in->lhs, ctx);
        break;
    case CC_SSA_RET:
        /* a 64-bit value returned on i386: rhs is its high word */
        if (in->lhs >= 0 && in->lhs < f->value_count) fn(in->lhs, ctx);
        if (in->rhs >= 0 && in->rhs < f->value_count) fn(in->rhs, ctx);
        break;
    case CC_SSA_STORE:
        if (in->lhs >= 0 && in->lhs < f->value_count) fn(in->lhs, ctx);
        if (in->rhs >= 0 && in->rhs < f->value_count) fn(in->rhs, ctx);
        break;
    case CC_SSA_CALLI:
        if (in->lhs >= 0 && in->lhs < f->value_count) fn(in->lhs, ctx);
        /* fallthrough */
    case CC_SSA_CALL:
        if (in->call_chain > 0 && in->call_chain - 1 < f->value_count) fn(in->call_chain - 1, ctx);
        if (in->args != NULL) {
            for (i = 0; i < in->arg_count; ++i) {
                if (in->args[i] >= 0 && in->args[i] < f->value_count) fn(in->args[i], ctx);
            }
        }
        break;
    case CC_SSA_ASM:
        if (in->asm_in_values != NULL) {
            for (i = 0; i < in->asm_in_count; ++i) {
                int v = in->asm_in_values[i];
                if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
                    v = CC_SSA_ASM_MEM_INDIRECT_DECODE(v);
                }
                if (v >= 0 && v < f->value_count) fn(v, ctx);
            }
        }
        break;
    default:
        if (in->lhs >= 0 && in->lhs < f->value_count) fn(in->lhs, ctx);
        if (in->rhs >= 0 && in->rhs < f->value_count) fn(in->rhs, ctx);
        break;
    }
}

void x86_for_each_use(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, void (*fn)(int v, void *ctx), void *ctx) {
    for_each_instr_use(f, in, fn, ctx);
}

typedef struct {
    int nvals;
    int at;
    int *first_use;
    int *last_use;
    const int *alias; /**< per value: another it reads in its place (build_slot_layout()), or -1; or NULL */
} slot_use_ctx_t;

typedef struct {
    int nvals;
    int at;
    int *def_at;
    unsigned char *multi_def;
} slot_def_ctx_t;

typedef struct {
    int nvals;
    int block;
    int *def_block;
    int *use_block;
    unsigned char *cross_block;
    const int *alias; /**< as slot_use_ctx_t's */
} slot_block_ctx_t;

static void slot_mark_use_cb(int v, void *ctxp) {
    slot_use_ctx_t *ctx = (slot_use_ctx_t *)ctxp;
    if (ctx == NULL || v < 0 || v >= ctx->nvals) {
        return;
    }
    if (ctx->at > ctx->last_use[v]) {
        ctx->last_use[v] = ctx->at;
    }
    if (ctx->first_use[v] < 0 || ctx->at < ctx->first_use[v]) {
        ctx->first_use[v] = ctx->at;
    }
    if (ctx->alias != NULL && ctx->alias[v] >= 0 && ctx->alias[v] != v) {
        /* read here in its place: a use of the other too */
        slot_mark_use_cb(ctx->alias[v], ctxp);
    }
}

static void slot_mark_def_cb(int v, void *ctxp) {
    slot_def_ctx_t *ctx = (slot_def_ctx_t *)ctxp;
    if (ctx == NULL || v < 0 || v >= ctx->nvals) {
        return;
    }
    if (ctx->def_at[v] >= 0) {
        ctx->multi_def[v] = 1;
    } else {
        ctx->def_at[v] = ctx->at;
    }
}

static void slot_mark_use_block_cb(int v, void *ctxp) {
    slot_block_ctx_t *ctx = (slot_block_ctx_t *)ctxp;
    if (ctx == NULL || v < 0 || v >= ctx->nvals) {
        return;
    }
    if (ctx->use_block[v] < 0) {
        ctx->use_block[v] = ctx->block;
    } else if (ctx->use_block[v] != ctx->block) {
        ctx->cross_block[v] = 1;
    }
    if (ctx->def_block[v] < 0 || ctx->def_block[v] != ctx->block) {
        ctx->cross_block[v] = 1;
    }
    if (ctx->alias != NULL && ctx->alias[v] >= 0 && ctx->alias[v] != v) {
        slot_mark_use_block_cb(ctx->alias[v], ctxp);
    }
}

static char *dup_printf(const char *fmt, long off, const char *bp) {
    char tmp[64];
    size_t n;
    char *out;
    snprintf(tmp, sizeof(tmp), fmt, off, bp);
    n = strlen(tmp);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, tmp, n + 1);
    return out;
}

static char *dup_cstr(const char *s) {
    size_t n;
    char *out;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    out = (char *)malloc(n);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, n);
    return out;
}

/** An inline asm operand's register to fill before the asm runs. */
typedef struct {
    const char *reg; /**< the register, full width */
    long off;        /**< the frame slot it is loaded from */
    int value;       /**< for an address: the value whose address it is */
    int is_address;  /**< load the address of @c value (an indirect memory output), not a slot */
} asm_load_t;


/** The general registers, by hardware number, under each width's name. */
static const struct {
    const char *n64, *n32, *n16, *n8;
} x86_gprs[16] = {
    {"%rax", "%eax", "%ax", "%al"},     {"%rcx", "%ecx", "%cx", "%cl"},     {"%rdx", "%edx", "%dx", "%dl"},
    {"%rbx", "%ebx", "%bx", "%bl"},     {"%rsp", "%esp", "%sp", "%spl"},    {"%rbp", "%ebp", "%bp", "%bpl"},
    {"%rsi", "%esi", "%si", "%sil"},    {"%rdi", "%edi", "%di", "%dil"},    {"%r8", "%r8d", "%r8w", "%r8b"},
    {"%r9", "%r9d", "%r9w", "%r9b"},    {"%r10", "%r10d", "%r10w", "%r10b"}, {"%r11", "%r11d", "%r11w", "%r11b"},
    {"%r12", "%r12d", "%r12w", "%r12b"}, {"%r13", "%r13d", "%r13w", "%r13b"}, {"%r14", "%r14d", "%r14w", "%r14b"},
    {"%r15", "%r15d", "%r15w", "%r15b"},
};

/**
 * The hardware number of a general register named in any width, with or
 * without its `%` -- "%ebx", "rbx", "bl", "r12d" -- as asm clobber lists
 * name them.
 *
 * @return 0..15, or -1 for no general register
 */
static int x86_gpr_index(const char *name) {
    static const char *const high8[4] = {"ah", "ch", "dh", "bh"};
    size_t i;

    if (name == NULL) {
        return -1;
    }
    if (name[0] == '%') {
        name++;
    }
    for (i = 0; i < 16; ++i) {
        if (strcmp(name, x86_gprs[i].n64 + 1) == 0 || strcmp(name, x86_gprs[i].n32 + 1) == 0 ||
            strcmp(name, x86_gprs[i].n16 + 1) == 0 || strcmp(name, x86_gprs[i].n8 + 1) == 0) {
            return (int)i;
        }
    }
    for (i = 0; i < 4; ++i) {
        if (strcmp(name, high8[i]) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * An inline asm operand's text under a GCC x86 operand modifier, as gcc
 * prints it: for a register, `b` `h` `w` `k` `q` its 8-bit low, 8-bit
 * high, 16-, 32- and 64-bit name, `V` the name without its `%`, `a` its
 * address `(%reg)`; for a constant `$n`, `c` `P` `a` the bare number and
 * `n` its negation.  Linux writes `movl %k0, %%ds` and
 * `extable_type_reg reg=%k0` for a u16 operand: the register's 32-bit
 * name, which extable_type_reg insists on.  Other modifiers, and a
 * memory operand under any, leave the text as it is.
 *
 * @param mod   the modifier letter, or 0 for none
 * @param text  the operand's text
 * @param buf   room for a changed text
 * @return      @p text or @p buf
 */
static const char *asm_operand_with_modifier(char mod, const char *text, char *buf, size_t bufsz) {
    static const char *const high8[4] = {"%ah", "%ch", "%dh", "%bh"};
    int r = text != NULL && text[0] == '%' ? x86_gpr_index(text) : -1;

    if (mod == 0 || text == NULL) {
        return text;
    }
    if (r >= 0) {
        switch (mod) {
        case 'b': return x86_gprs[r].n8;
        case 'h': return r < 4 ? high8[r] : text;
        case 'w': return x86_gprs[r].n16;
        case 'k': return x86_gprs[r].n32;
        case 'q': return x86_gprs[r].n64;
        case 'V': return text + 1;
        case 'a':
            snprintf(buf, bufsz, "(%s)", text);
            return buf;
        default: return text;
        }
    }
    if (text[0] == '$') {
        if (mod == 'c' || mod == 'P' || mod == 'a') {
            return text + 1;
        }
        if (mod == 'n') {
            char *end;
            long long v = strtoll(text + 1, &end, 0);

            if (end != text + 1 && *end == '\0') {
                snprintf(buf, bufsz, "%lld", -v);
                return buf;
            }
        }
    }
    return text;
}

const char *reg64_to8(const char *r) {
    int i = x86_gpr_index(r);
    return i >= 0 ? x86_gprs[i].n8 : "%al";
}

const char *reg64_to16(const char *r) {
    int i = x86_gpr_index(r);
    return i >= 0 ? x86_gprs[i].n16 : "%ax";
}

const char *reg64_to32(const char *r) {
    int i = x86_gpr_index(r);
    return i >= 0 ? x86_gprs[i].n32 : "%eax";
}

/** The callee-saved general registers, as a mask by hardware number: %ebx, %esi, %edi (i386); %rbx, %r12-%r15 (x86-64). */
static unsigned x86_callee_saved_mask(int is_64bit) {
    return is_64bit ? (1u << 3) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 15)
                    : (1u << 3) | (1u << 6) | (1u << 7);
}

/**
 * Choose a register for an inline asm operand: the first the constraint
 * allows that is not in @p avoid.  Caller-saved registers come first, so
 * that fewer have to be saved around the asm.
 *
 * @param is_64bit    64-bit code
 * @param constraint  the operand's constraint (`q` limits it to the byte-addressable four)
 * @param op_size     the operand's size: a byte operand in 32-bit code needs one of those four too
 * @param avoid       registers already spoken for, by hardware number
 * @return            the register's full-width name, or NULL if none is free
 */
static const char *asm_pick_reg(int is_64bit, const char *constraint, int op_size, unsigned avoid) {
    static const int order32_q[] = {0, 1, 2, 3};
    static const int order32_r[] = {0, 1, 2, 3, 6, 7};
    static const int order64_r[] = {10, 11, 8, 9, 1, 2, 0, 6, 7, 3, 12, 13, 14, 15};
    const int *order;
    size_t n;
    size_t i;

    if (is_64bit) {
        order = order64_r;
        n = sizeof(order64_r) / sizeof(order64_r[0]);
    } else if (cc_asm_constraint_has(constraint, 'q') || op_size == 1 ||
               (constraint != NULL && strncmp(constraint, "=@cc", 4) == 0)) {
        /* (a flag output is set by setcc, a byte: a register that has one) */
        order = order32_q;
        n = sizeof(order32_q) / sizeof(order32_q[0]);
    } else {
        order = order32_r;
        n = sizeof(order32_r) / sizeof(order32_r[0]);
    }
    for (i = 0; i < n; ++i) {
        if ((avoid & (1u << order[i])) == 0) {
            return is_64bit ? x86_gprs[order[i]].n64 : x86_gprs[order[i]].n32;
        }
    }
    return NULL;
}

/** The mask bit of a register named in any width; 0 for none. */
static unsigned x86_gpr_bit(const char *name) {
    int i = x86_gpr_index(name);
    return i >= 0 ? 1u << i : 0;
}

/* in 32-bit code only %eax..%ebx have byte registers */
const char *reg32_to8(const char *r) {
    int i = x86_gpr_index(r);
    return i >= 0 && i < 4 ? x86_gprs[i].n8 : "%al";
}

const char *reg32_to16(const char *r) {
    int i = x86_gpr_index(r);
    return i >= 0 && i < 8 ? x86_gprs[i].n16 : "%ax";
}

static int asm_operand_size(const unsigned char *sizes, size_t count, size_t idx, int default_size) {
    if (sizes != NULL && idx < count && sizes[idx] > 0) {
        return (int)sizes[idx];
    }
    return default_size;
}

static const char *reg_alias_for_size(const char *reg, int is_64bit, int op_size) {
    if (reg == NULL) {
        return NULL;
    }
    if (op_size <= 1) {
        return is_64bit ? reg64_to8(reg) : reg32_to8(reg);
    }
    if (op_size == 2) {
        return is_64bit ? reg64_to16(reg) : reg32_to16(reg);
    }
    if (op_size == 4 && is_64bit) {
        return reg64_to32(reg);
    }
    return reg;
}

static void emit_zero_extend_reg(FILE *fp, const char *reg, int is_64bit, int op_size) {
    if (reg == NULL) {
        return;
    }
    if (is_64bit) {
        if (op_size <= 1) {
            fprintf(fp, "\tmovzbq %s, %s\n", reg64_to8(reg), reg);
        } else if (op_size == 2) {
            fprintf(fp, "\tmovzwq %s, %s\n", reg64_to16(reg), reg);
        } else if (op_size == 4) {
            const char *r32 = reg64_to32(reg);
            fprintf(fp, "\tmovl %s, %s\n", r32, r32);
        }
    } else {
        if (op_size <= 1) {
            fprintf(fp, "\tmovzbl %s, %s\n", reg32_to8(reg), reg);
        } else if (op_size == 2) {
            fprintf(fp, "\tmovzwl %s, %s\n", reg32_to16(reg), reg);
        }
    }
}

void emit_load_ptr_value_reg(FILE *fp, const slot_layout_t *lay, int value, int is_64bit, const char *reg) {
    if (is_stackalloc_value(lay, value)) {
        long off = stackalloc_off(lay, value);
        if (is_64bit) {
            fprintf(fp, "\tleaq %ld(%%rbp), %s\n", off, reg);
        } else {
            fprintf(fp, "\tleal %ld(%%ebp), %s\n", off, reg);
        }
        return;
    }
    emit_frame_load_reg(fp, is_64bit, slot_off(lay, value), reg);
}

int emit_inline_asm(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, const cc_ssa_instr_t *in,
                           size_t fn_index, size_t instr_index, int is_64bit, cc_diag_t *diag) {
    size_t out_n = in->asm_out_count;
    size_t in_n = in->asm_in_count;
    size_t total = out_n + in_n;
    size_t i;
    char **op_text = NULL;
    char **op_names = NULL;
    const char **out_regs = NULL;
    int *out_write_back = NULL;
    int *out_tied_read = NULL;
    /*
     * The operands' registers are chosen first and loaded after: any
     * callee-saved register the asm uses has to be saved before the first
     * load.  Registers are tracked as masks by hardware number.
     */
    asm_load_t *loads = NULL;
    size_t load_count = 0;
    unsigned fixed_mask = 0;    /* named by a constraint (`a`, `D`, ...) of any operand */
    unsigned clobber_mask = 0;  /* in the clobber list */
    unsigned out_mask = 0;      /* holding an output */
    unsigned out_live_mask = 0; /* holding an output an input may not share: read (`+`, matched) or early-clobber */
    unsigned in_mask = 0;       /* holding an input */
    unsigned saved_mask = 0;    /* callee-saved, used, so saved around the asm */
    char *rendered = NULL;

    op_text = (char **)calloc(total, sizeof(*op_text));
    op_names = (char **)calloc(total, sizeof(*op_names));
    out_regs = (const char **)calloc(out_n, sizeof(*out_regs));
    out_write_back = (int *)calloc(out_n, sizeof(*out_write_back));
    out_tied_read = (int *)calloc(out_n, sizeof(*out_tied_read));
    loads = (asm_load_t *)calloc(2 * total + 1, sizeof(*loads)); /* (a pair operand loads two registers) */
    if ((total > 0 && (op_text == NULL || op_names == NULL)) || loads == NULL ||
        (out_n > 0 && (out_regs == NULL || out_write_back == NULL || out_tied_read == NULL))) {
        free(op_text);
        free(op_names);
        free(out_regs);
        free(out_write_back);
        free(out_tied_read);
        free(loads);
        set_diag(diag, "out of memory preparing inline asm operands");
        return -1;
    }

    for (i = 0; i < in_n; ++i) {
        const char *c = in->asm_in_constraints != NULL ? in->asm_in_constraints[i] : NULL;
        int tied = cc_asm_constraint_matched_operand(c);
        if (tied >= 0 && (size_t)tied < out_n) {
            /* which input: its value is what the output's register holds as the asm begins */
            out_tied_read[tied] = (int)i + 1;
        } else if (tied < 0 && !cc_asm_constraint_is_memory_only(c)) {
            fixed_mask |= x86_gpr_bit(is_64bit ? asm_constraint_fixed_reg64(c) : asm_constraint_fixed_reg32(c));
            if (asm_pair_operand(c, is_64bit, asm_operand_size(in->asm_in_sizes, in_n, i, 4))) {
                fixed_mask |= x86_gpr_bit("%edx");
            }
        }
    }
    for (i = 0; i < out_n; ++i) {
        const char *c = in->asm_out_constraints != NULL ? in->asm_out_constraints[i] : NULL;
        if (!cc_asm_constraint_is_memory_only(c)) {
            fixed_mask |= x86_gpr_bit(is_64bit ? asm_constraint_fixed_reg64(c) : asm_constraint_fixed_reg32(c));
            if (asm_pair_operand(c, is_64bit, asm_operand_size(in->asm_out_sizes, out_n, i, 4))) {
                fixed_mask |= x86_gpr_bit("%edx");
            }
        }
    }
    for (i = 0; i < in->asm_clobber_count; ++i) {
        clobber_mask |= x86_gpr_bit(in->asm_clobbers[i]);
    }

    for (i = 0; i < out_n; ++i) {
        const char *c = in->asm_out_constraints != NULL ? in->asm_out_constraints[i] : NULL;
        int vraw = in->asm_out_values != NULL ? in->asm_out_values[i] : -1;
        int is_indirect = CC_SSA_ASM_MEM_INDIRECT_P(vraw);
        int v = is_indirect ? CC_SSA_ASM_MEM_INDIRECT_DECODE(vraw) : vraw;
        int osz = asm_operand_size(in->asm_out_sizes, out_n, i, is_64bit ? 8 : 4);
        long off = slot_off(lay, v);
        const char *fixed = is_64bit ? asm_constraint_fixed_reg64(c) : asm_constraint_fixed_reg32(c);
        if (in->asm_out_names != NULL && in->asm_out_names[i] != NULL) {
            op_names[i] = in->asm_out_names[i];
        }
        if (cc_asm_constraint_is_memory_only(c)) {
            if (is_indirect) {
                /* the address lives in a register for the whole asm: no input may share it */
                const char *areg = asm_pick_reg(is_64bit, NULL, is_64bit ? 8 : 4, fixed_mask | clobber_mask | out_mask);
                char abuf[32];
                if (areg == NULL) {
                    goto impossible;
                }
                out_mask |= x86_gpr_bit(areg);
                out_live_mask |= x86_gpr_bit(areg);
                loads[load_count].reg = areg;
                loads[load_count].value = v;
                loads[load_count].is_address = 1;
                load_count++;
                snprintf(abuf, sizeof(abuf), "(%s)", areg);
                op_text[i] = dup_cstr(abuf);
            } else {
                op_text[i] = dup_printf("%ld(%s)", off, is_64bit ? "%rbp" : "%ebp");
            }
            if (op_text[i] == NULL) {
                goto oom;
            }
        } else {
            /* never another output's register, another operand's fixed one, or a clobbered one */
            const char *reg = fixed != NULL ? fixed : asm_pick_reg(is_64bit, c, osz, fixed_mask | clobber_mask | out_mask);
            const char *render_reg;
            int read = (c != NULL && strchr(c, '+') != NULL) || out_tied_read[i];

            if (reg == NULL) {
                goto impossible;
            }
            render_reg = reg_alias_for_size(reg, is_64bit, osz);
            out_regs[i] = reg;
            out_write_back[i] = 1;
            out_mask |= x86_gpr_bit(reg);
            if (asm_pair_operand(c, is_64bit, osz)) {
                /* its high word in %edx, written by the asm too */
                out_mask |= x86_gpr_bit("%edx");
                if (read) {
                    out_live_mask |= x86_gpr_bit("%edx");
                }
            }
            if (read || (c != NULL && strchr(c, '&') != NULL)) {
                out_live_mask |= x86_gpr_bit(reg);
            }
            op_text[i] = dup_cstr(render_reg);
            if (op_text[i] == NULL) {
                goto oom;
            }
            if (read) {
                int from = in->asm_in_values != NULL && out_tied_read[i] > 0 && (c == NULL || strchr(c, '+') == NULL)
                               ? in->asm_in_values[out_tied_read[i] - 1]
                               : -1;

                loads[load_count].reg = reg;
                /* a matching input's value ("0"(id)), not the output's: that is only written after */
                loads[load_count].off = from >= 0 && !CC_SSA_ASM_MEM_INDIRECT_P(from) ? slot_off(lay, from) : off;
                load_count++;
                if (asm_pair_operand(c, is_64bit, osz)) {
                    loads[load_count].reg = "%edx";
                    loads[load_count].off = loads[load_count - 1].off + 4;
                    load_count++;
                }
            }
        }
    }

    for (i = 0; i < in_n; ++i) {
        const char *c = in->asm_in_constraints != NULL ? in->asm_in_constraints[i] : NULL;
        int v = in->asm_in_values != NULL ? in->asm_in_values[i] : -1;
        size_t slot = out_n + i;
        int tied = cc_asm_constraint_matched_operand(c);
        if (in->asm_in_names != NULL && in->asm_in_names[i] != NULL) {
            op_names[slot] = in->asm_in_names[i];
        }
        if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
            /* a memory input given by its address (the object itself, as gcc passes it): in a register */
            const char *areg = asm_pick_reg(is_64bit, NULL, is_64bit ? 8 : 4,
                                            fixed_mask | clobber_mask | out_mask | out_live_mask | in_mask);
            char abuf[32];

            if (areg == NULL) {
                goto impossible;
            }
            in_mask |= x86_gpr_bit(areg);
            loads[load_count].reg = areg;
            loads[load_count].value = CC_SSA_ASM_MEM_INDIRECT_DECODE(v);
            loads[load_count].is_address = 1;
            load_count++;
            snprintf(abuf, sizeof(abuf), "(%s)", areg);
            op_text[slot] = dup_cstr(abuf);
            if (op_text[slot] == NULL) {
                goto oom;
            }
            continue;
        }
        if (tied >= 0) {
            if ((size_t)tied >= out_n || op_text[tied] == NULL) {
                set_diag(diag, "asm matching constraint references invalid output operand");
                goto fail;
            }
            op_text[slot] = dup_cstr(op_text[tied]);
            if (op_text[slot] == NULL) {
                goto oom;
            }
            continue;
        }
        if (asm_constraint_is_immediate(c)) {
            long imm = 0;
            char ibuf[64];
            int sym_ok = -1;
            int have_const = find_const_i64_for_value(f, v, instr_index, &imm) == 0;
            int alternative = cc_asm_constraint_allows_gpr(c) || cc_asm_constraint_allows_memory(c);

            if (have_const && (asm_immediate_fits(c, imm) || !alternative)) {
                snprintf(ibuf, sizeof(ibuf), "$%ld", imm);
                op_text[slot] = dup_cstr(ibuf);
                if (op_text[slot] == NULL) {
                    goto oom;
                }
                continue;
            }
            /*
             * A constant out of the immediate's range, or a value that is no
             * constant at all, takes the register or memory alternative below
             * when there is one.  resolve_asm_immediate_symbol() guesses a
             * value through loads and stores -- fit only for a constraint
             * that leaves no choice: it made set_bit()'s "Ir" (nr), a
             * parameter, the constant of an unrelated earlier store.
             */
            sym_ok = have_const || alternative
                         ? -1
                         : resolve_asm_immediate_symbol(f, v, instr_index, fn_index, 0, ibuf, sizeof(ibuf));
            if (sym_ok == 0) {
                op_text[slot] = dup_cstr(ibuf);
                if (op_text[slot] == NULL) {
                    goto oom;
                }
                continue;
            }
            if (!cc_asm_constraint_allows_gpr(c) && !cc_asm_constraint_allows_memory(c)) {
                /* Can't resolve to a constant. cc has no inliner, so a
                 * `static __always_inline` helper that takes its `i`-
                 * constrained input from a parameter (Linux's
                 * rip_rel_ptr is the canonical case) is unresolvable
                 * here. Emit a placeholder; the function body is
                 * either dead code (32-bit vDSO never calls
                 * rip_rel_ptr, which is 64-bit specific) or the link
                 * will fail in a more obvious way later. Beats
                 * blocking the entire compilation. */
                op_text[slot] = dup_cstr("$0");
                if (op_text[slot] == NULL) {
                    goto oom;
                }
                continue;
            }
            if (cc_asm_constraint_allows_memory(c) && !cc_asm_constraint_allows_gpr(c)) {
                long off = slot_off(lay, v);
                op_text[slot] = dup_printf("%ld(%s)", off, is_64bit ? "%rbp" : "%ebp");
                if (op_text[slot] == NULL) {
                    goto oom;
                }
                continue;
            }
        }
        if (cc_asm_constraint_is_memory_only(c)) {
            long off = slot_off(lay, v);
            op_text[slot] = dup_printf("%ld(%s)", off, is_64bit ? "%rbp" : "%ebp");
            if (op_text[slot] == NULL) {
                goto oom;
            }
            continue;
        }
        {
            const char *fixed = is_64bit ? asm_constraint_fixed_reg64(c) : asm_constraint_fixed_reg32(c);
            const char *reg = NULL;
            int isz = asm_operand_size(in->asm_in_sizes, in_n, i, is_64bit ? 8 : 4);
            const char *render_reg = NULL;
            if (fixed != NULL) {
                /* GCC accepts a fixed-class input that shares a register
                 * with an early-clobber output as long as the user wrote
                 * matching constraints; we don't fully model matching
                 * here, so trust the constraint and emit. The kernel's
                 * vDSO inline asm exercises this pattern. */
                reg = fixed;
            } else {
                /*
                 * Not another input's register, another operand's fixed one,
                 * a clobbered one, or an output's the asm reads or writes
                 * early.  An output's it only writes at the end may be
                 * shared, as gcc does, but only when nothing else is free.
                 */
                unsigned avoid = fixed_mask | clobber_mask | out_live_mask | in_mask;

                reg = asm_pick_reg(is_64bit, c, isz, avoid | out_mask);
                if (reg == NULL) {
                    reg = asm_pick_reg(is_64bit, c, isz, avoid);
                }
                if (reg == NULL) {
                    goto impossible;
                }
            }
            in_mask |= x86_gpr_bit(reg);
            render_reg = reg_alias_for_size(reg, is_64bit, isz);
            long off = slot_off(lay, v);
            op_text[slot] = dup_cstr(render_reg);
            if (op_text[slot] == NULL) {
                goto oom;
            }
            loads[load_count].reg = reg;
            loads[load_count].off = off;
            load_count++;
            if (asm_pair_operand(c, is_64bit, isz)) {
                /* and its high word in %edx */
                in_mask |= x86_gpr_bit("%edx");
                loads[load_count].reg = "%edx";
                loads[load_count].off = off + 4;
                load_count++;
            }
        }
    }

    /*
     * Save the callee-saved registers the asm uses -- an operand's, or a
     * clobbered one -- then load the operands.  The function's own code
     * keeps nothing in registers across an instruction, but its caller
     * does, and the ABI says these survive the call.  (%esp and %ebp are
     * the frame's, and not the asm's to change.)
     */
    saved_mask = (out_mask | in_mask | clobber_mask) & x86_callee_saved_mask(is_64bit);
    {
        int pushes = 0;
        int r;

        for (r = 0; r < 16; ++r) {
            pushes += (saved_mask & (1u << r)) != 0;
        }
        if (is_64bit && (pushes & 1) != 0) {
            fprintf(fp, "\tsubq $8, %%rsp\n"); /* keep %rsp 16-aligned for a call in the asm */
        }
        for (r = 0; r < 16; ++r) {
            if ((saved_mask & (1u << r)) != 0) {
                fprintf(fp, "\tpush%c %s\n", is_64bit ? 'q' : 'l', is_64bit ? x86_gprs[r].n64 : x86_gprs[r].n32);
            }
        }
    }
    for (i = 0; i < load_count; ++i) {
        if (loads[i].is_address) {
            emit_load_ptr_value_reg(fp, lay, loads[i].value, is_64bit, loads[i].reg);
        } else {
            emit_frame_load_reg(fp, is_64bit, loads[i].off, loads[i].reg);
        }
    }

    {
        size_t label_base = total;
        for (i = 0; i < out_n; ++i) {
            const char *c = in->asm_out_constraints != NULL ? in->asm_out_constraints[i] : NULL;
            if (c != NULL && strchr(c, '+') != NULL) {
                label_base++; /* a `+` output is also an implicit input */
            }
        }
        rendered = in->asm_is_basic
                       ? dup_cstr(in->sym != NULL ? in->sym : "") /* basic asm: the text as written */
                       : render_inline_asm_template(in->sym != NULL ? in->sym : "", op_text, op_names, total,
                                                    label_base, in->asm_goto_labels, in->asm_goto_names,
                                                    in->asm_goto_count, f->name, diag);
    }
    if (rendered == NULL) {
        if (diag != NULL && diag->message[0] == '\0') {
            char msg[384];
            snprintf(msg, sizeof(msg),
                     "inline asm template rendering failed (fn=%s instr=%zu out=%zu in=%zu tmpl=%.120s)",
                     f->name != NULL ? f->name : "<anon>", instr_index, out_n, in_n,
                     in->sym != NULL ? in->sym : "<null>");
            set_diag(diag, msg);
        }
        goto fail;
    }
    if (in->asm_volatile) {
        fprintf(fp, "\t# volatile asm\n");
    }
    for (i = 0; i < in->asm_clobber_count; ++i) {
        if (in->asm_clobbers[i] != NULL &&
            (strcmp(in->asm_clobbers[i], "memory") == 0 || strcmp(in->asm_clobbers[i], "cc") == 0)) {
            fprintf(fp, "\t# asm clobber %s\n", in->asm_clobbers[i]);
        }
    }
    {
        char *escaped = escape_strings_in_asm_template(rendered);
        if (escaped != NULL) {
            emit_asm_lines(fp, escaped);
            free(escaped);
        } else {
            emit_asm_lines(fp, rendered);
        }
    }

    for (i = 0; i < out_n; ++i) {
        /*
         * a flag output ("=@ccz"): the condition the asm leaves in the flags, set in the output's register
         * first, before the moves below (which leave the flags be).  Linux 7.3's try_cmpxchg() has no other
         * way out of its asm: with no setz, mutex_lock()'s fast path failed at random and the kernel tried
         * to sleep in early boot.
         */
        const char *c = in->asm_out_constraints != NULL ? in->asm_out_constraints[i] : NULL;

        if (c != NULL && strncmp(c, "=@cc", 4) == 0 && c[4] != '\0' && out_regs[i] != NULL) {
            /* (and the rest of the register clear: the output may be wider than the byte set) */
            fprintf(fp, "\tset%s %s\n", c + 4, reg_alias_for_size(out_regs[i], is_64bit, 1));
            fprintf(fp, "\tmovzbl %s, %s\n", reg_alias_for_size(out_regs[i], is_64bit, 1),
                    reg_alias_for_size(out_regs[i], is_64bit, 4));
        }
    }
    for (i = 0; i < out_n; ++i) {
        if (out_write_back[i] && out_regs[i] != NULL && in->asm_out_values != NULL) {
            int osz = asm_operand_size(in->asm_out_sizes, out_n, i, is_64bit ? 8 : 4);
            long off = slot_off(lay, in->asm_out_values[i]);
            emit_zero_extend_reg(fp, out_regs[i], is_64bit, osz);
            emit_frame_store_reg(fp, is_64bit, out_regs[i], off);
            if (asm_pair_operand(in->asm_out_constraints != NULL ? in->asm_out_constraints[i] : NULL, is_64bit, osz)) {
                emit_frame_store_reg(fp, is_64bit, "%edx", off + 4);
            }
        }
    }
    {
        int pushes = 0;
        int r;

        for (r = 15; r >= 0; --r) {
            if ((saved_mask & (1u << r)) != 0) {
                fprintf(fp, "\tpop%c %s\n", is_64bit ? 'q' : 'l', is_64bit ? x86_gprs[r].n64 : x86_gprs[r].n32);
                pushes++;
            }
        }
        if (is_64bit && (pushes & 1) != 0) {
            fprintf(fp, "\taddq $8, %%rsp\n");
        }
    }

    free(rendered);
    for (i = 0; i < total; ++i) {
        free(op_text[i]);
    }
    free(op_text);
    free(op_names);
    free(out_regs);
    free(out_write_back);
    free(out_tied_read);
    free(loads);
    return 0;

impossible: {
    char msg[192];
    snprintf(msg, sizeof(msg), "impossible register constraints in asm (fn=%s): no free register for an operand",
             f->name != NULL ? f->name : "<anon>");
    set_diag(diag, msg);
    goto fail;
}
oom:
    set_diag(diag, "out of memory preparing inline asm");
fail:
    if (diag != NULL && diag->message[0] == '\0') {
        char msg[192];
        snprintf(msg, sizeof(msg), "inline asm emission failed (fn=%s instr=%zu)",
                 f->name != NULL ? f->name : "<anon>", instr_index);
        set_diag(diag, msg);
    }
    free(rendered);
    for (i = 0; i < total; ++i) {
        free(op_text[i]);
    }
    free(op_text);
    free(op_names);
    free(out_regs);
    free(out_write_back);
    free(out_tied_read);
    free(loads);
    return -1;
}

typedef struct {
    FILE *fp;
    const slot_layout_t *lay;
    const int_reg_state_t *st;
} remat_store_ctx_t;

/** for_each_instr_use() callback: a constant made where it is read goes to its slot. */
static void remat_store_cb(int v, void *ctx) {
    const remat_store_ctx_t *c = (const remat_store_ctx_t *)ctx;

    if (c->st->remat[v] == 2) {
        fprintf(c->fp, "\tmovl $%s%+ld, %d(%%ebp)\n", c->st->remat_sym[v], (long)c->st->remat_imm[v],
                slot_off(c->lay, v));
    } else if (c->st->remat[v] && !is_stackalloc_value(c->lay, v)) {
        fprintf(c->fp, "\tmovl $%ld, %d(%%ebp)\n", (long)(int)(unsigned int)c->st->remat_imm[v], slot_off(c->lay, v));
    }
}

void int_regs_store_remat_uses(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, const int_reg_state_t *st,
                               const cc_ssa_instr_t *in) {
    remat_store_ctx_t c;

    if (st->remat == NULL) {
        return;
    }
    c.fp = fp;
    c.lay = lay;
    c.st = st;
    for_each_instr_use(f, in, remat_store_cb, &c);
}

/** The largest stack object an i386 frame places above its slots, near %ebp (build_slot_layout()). */
#define SMALL_STACK_OBJECT 64

typedef struct {
    long weight;
    int slot;
} slot_weight_t;

typedef struct {
    const slot_layout_t *lay;
    slot_weight_t *w;
} slot_count_ctx_t;

/** for_each_instr_use() callback: a use of a value is an access of its slot. */
static void slot_count_cb(int v, void *ctxp) {
    slot_count_ctx_t *c = (slot_count_ctx_t *)ctxp;

    if (v < c->lay->value_count && c->lay->slot_of[v] >= 0) {
        c->w[c->lay->slot_of[v]].weight++;
    }
}

/** qsort() order: the busier slot first, then the lower number. */
static int slot_weight_cmp(const void *a, const void *b) {
    const slot_weight_t *x = (const slot_weight_t *)a;
    const slot_weight_t *y = (const slot_weight_t *)b;

    if (x->weight != y->weight) {
        return x->weight > y->weight ? -1 : 1;
    }
    return x->slot - y->slot;
}

/**
 * Renumber the @p nslots slots of @p lay so the most used are nearest the frame pointer, where an 8-bit
 * displacement reaches them: what a value's definitions and uses count, a STACKALLOC's own aside (its
 * address is computed, not loaded).
 *
 * @return 0, or -1 when out of memory
 */
static int order_slots_by_use(const cc_ssa_function_t *f, slot_layout_t *lay, int nslots) {
    slot_weight_t *w;
    int *renum;
    slot_count_ctx_t c;
    size_t j;
    int i;

    if (nslots <= 1) {
        return 0;
    }
    w = (slot_weight_t *)calloc((size_t)nslots, sizeof(*w));
    renum = (int *)malloc((size_t)nslots * sizeof(*renum));
    if (w == NULL || renum == NULL) {
        free(w);
        free(renum);
        return -1;
    }
    for (i = 0; i < nslots; ++i) {
        w[i].slot = i;
    }
    c.lay = lay;
    c.w = w;
    for (j = 0; j < f->instr_count; ++j) {
        const cc_ssa_instr_t *in = &f->instrs[j];

        for_each_instr_use(f, in, slot_count_cb, &c);
        if (in->op != CC_SSA_STACKALLOC && in->dst >= 0 && in->dst < lay->value_count && lay->slot_of[in->dst] >= 0) {
            w[lay->slot_of[in->dst]].weight++;
        }
    }
    qsort(w, (size_t)nslots, sizeof(*w), slot_weight_cmp);
    for (i = 0; i < nslots; ++i) {
        renum[w[i].slot] = i;
    }
    for (i = 0; i < lay->value_count; ++i) {
        if (lay->slot_of[i] >= 0 && lay->slot_of[i] < nslots) {
            lay->slot_of[i] = renum[lay->slot_of[i]];
        }
    }
    free(w);
    free(renum);
    return 0;
}

int build_slot_layout(const cc_ssa_function_t *f, int slot_size, int reuse_always, const int *alias,
                      slot_layout_t *out, cc_diag_t *diag) {
    int i;
    int nvals;
    int raw_frame;
    int *def_at = NULL;
    int *first_use = NULL;
    int *last_use = NULL;
    int *def_block = NULL;
    int *use_block = NULL;
    int *instr_block = NULL;
    int *block_start = NULL;
    int *block_end = NULL;
    int *start_head = NULL;
    int *end_head = NULL;
    int *next_start = NULL;
    int *next_end = NULL;
    int *free_slots = NULL;
    unsigned char *pinned = NULL;
    unsigned char *cross_block = NULL;
    unsigned char *multi_def = NULL;
    int *span_lo = NULL; /* per value: its live range over the blocks (x86_value_spans()) */
    int *span_hi = NULL;
    int spans = 0;       /* whether they are known: a value live across blocks then shares a slot too */
    unsigned char *is_word = NULL; /* per value: it takes a 4-byte slot of the pool (word_of) */
    int *free_words = NULL;
    int free_word_count = 0;
    int next_word = 0;
    int free_count = 0;
    int next_slot = 0;
    int enable_reuse = 0;
    int block_count = 0;
    size_t j;

    memset(out, 0, sizeof(*out));
    out->slot_size = slot_size;

    if (f->value_count <= 0) {
        return 0;
    }

    nvals = f->value_count;
    /*
     * Reuse stack slots once functions accumulate many SSA values.
     * This keeps frame size bounded for normalization-heavy integer code.
     * i386 reuses them always: a smaller frame is reached with shorter
     * displacements, which Linux's 16-bit setup code needs to fit.
     */
    enable_reuse = (nvals >= 64 || reuse_always) ? 1 : 0;
    out->value_count = nvals;
    out->slot_of = (int *)malloc((size_t)nvals * sizeof(*out->slot_of));
    out->stackalloc_off = (int *)calloc((size_t)nvals, sizeof(*out->stackalloc_off));
    out->live_end = (int *)malloc((size_t)nvals * sizeof(*out->live_end));
    out->slot_end = (int *)malloc((size_t)nvals * sizeof(*out->slot_end));
    out->word_of = (int *)malloc((size_t)nvals * sizeof(*out->word_of));
    def_at =(int *)malloc((size_t)nvals * sizeof(*def_at));
    first_use = (int *)malloc((size_t)nvals * sizeof(*first_use));
    last_use = (int *)malloc((size_t)nvals * sizeof(*last_use));
    def_block = (int *)malloc((size_t)nvals * sizeof(*def_block));
    use_block = (int *)malloc((size_t)nvals * sizeof(*use_block));
    next_start = (int *)malloc((size_t)nvals * sizeof(*next_start));
    next_end = (int *)malloc((size_t)nvals * sizeof(*next_end));
    instr_block = (int *)malloc(f->instr_count * sizeof(*instr_block));
    block_start = (int *)malloc(f->instr_count * sizeof(*block_start));
    block_end = (int *)malloc(f->instr_count * sizeof(*block_end));
    start_head = (int *)malloc((f->instr_count + 1u) * sizeof(*start_head));
    end_head = (int *)malloc((f->instr_count + 1u) * sizeof(*end_head));
    free_slots = (int *)malloc((size_t)nvals * sizeof(*free_slots));
    pinned = (unsigned char *)calloc((size_t)nvals, sizeof(*pinned));
    cross_block = (unsigned char *)calloc((size_t)nvals, sizeof(*cross_block));
    multi_def = (unsigned char *)calloc((size_t)nvals, sizeof(*multi_def));
    if (out->slot_of == NULL || out->stackalloc_off == NULL || out->live_end == NULL || out->slot_end == NULL ||
        out->word_of == NULL || def_at == NULL || first_use == NULL || last_use == NULL ||
        def_block == NULL || use_block == NULL || next_start == NULL || next_end == NULL || instr_block == NULL ||
        block_start == NULL || block_end == NULL || start_head == NULL || end_head == NULL || free_slots == NULL ||
        pinned == NULL || cross_block == NULL || multi_def == NULL) {
        set_diag(diag, "out of memory building stack slot layout");
        free(def_at);
        free(first_use);
        free(last_use);
        free(def_block);
        free(use_block);
        free(next_start);
        free(next_end);
        free(instr_block);
        free(block_start);
        free(block_end);
        free(start_head);
        free(end_head);
        free(free_slots);
        free(pinned);
        free(cross_block);
        free(multi_def);
        free(span_lo);
        free(span_hi);
        free(is_word);
        free(free_words);
        slot_layout_free(out);
        return -1;
    }
    for (i = 0; i < nvals; ++i) {
        out->slot_of[i] = -1;
        out->live_end[i] = -1;
        out->slot_end[i] = -1;
        out->word_of[i] = -1;
        def_at[i] = -1;
        first_use[i] = -1;
        last_use[i] = -1;
        def_block[i] = -1;
        use_block[i] = -1;
        next_start[i] = -1;
        next_end[i] = -1;
    }
    for (j = 0; j <= f->instr_count; ++j) {
        start_head[j] = -1;
        end_head[j] = -1;
    }

    /* Build basic blocks from labels and terminators. */
    block_start[block_count++] = 0;
    for (j = 1; j < f->instr_count; ++j) {
        cc_ssa_opcode_t prev = f->instrs[j - 1].op;
        if (f->instrs[j].op == CC_SSA_LABEL || cc_ssa_opcode_is_terminator(prev)) {
            if (block_start[block_count - 1] != (int)j) {
                block_start[block_count++] = (int)j;
            }
        }
    }
    for (i = 0; i < block_count; ++i) {
        int s = block_start[i];
        int e = (i + 1 < block_count) ? (block_start[i + 1] - 1) : ((int)f->instr_count - 1);
        int k;
        block_end[i] = e;
        for (k = s; k <= e; ++k) {
            instr_block[k] = i;
        }
    }

    for (j = 0; j < f->instr_count; ++j) {
        slot_use_ctx_t uctx;
        slot_def_ctx_t dctx;
        slot_block_ctx_t bctx;
        const cc_ssa_instr_t *in = &f->instrs[j];
        int b = instr_block[j];

        uctx.nvals = nvals;
        uctx.at = (int)j;
        uctx.first_use = first_use;
        uctx.last_use = last_use;
        uctx.alias = alias;
        dctx.nvals = nvals;
        dctx.at = (int)j;
        dctx.def_at = def_at;
        dctx.multi_def = multi_def;
        bctx.nvals = nvals;
        bctx.block = b;
        bctx.def_block = def_block;
        bctx.use_block = use_block;
        bctx.cross_block = cross_block;
        bctx.alias = alias;

        for_each_instr_use(f, in, slot_mark_use_cb, &uctx);
        for_each_instr_def(f, in, slot_mark_def_cb, &dctx);
        for_each_instr_use(f, in, slot_mark_use_block_cb, &bctx);

        if (in->op == CC_SSA_PARAM && in->dst >= 0 && in->dst < nvals) {
            pinned[in->dst] = 1;
            cross_block[in->dst] = 1;
        }
        if (in->op == CC_SSA_ADDR && in->lhs >= 0 && in->lhs < nvals) {
            /*
             * Once a local's address escapes, indirect loads/stores can outlive
             * the value's direct SSA uses. Keep that storage stable instead of
             * reusing it for another stack-backed SSA value.
             */
            pinned[in->lhs] = 1;
            cross_block[in->lhs] = 1;
        }

        /* Track block-locality for safe reuse. */
        if (in->dst >= 0 && in->dst < nvals) {
            if (def_block[in->dst] < 0) {
                def_block[in->dst] = b;
            } else if (def_block[in->dst] != b) {
                cross_block[in->dst] = 1;
            }
        }
        if (in->op == CC_SSA_ASM && in->asm_out_values != NULL) {
            for (i = 0; i < (int)in->asm_out_count; ++i) {
                int v = in->asm_out_values[i];
                if (CC_SSA_ASM_MEM_INDIRECT_P(v)) {
                    v = CC_SSA_ASM_MEM_INDIRECT_DECODE(v);
                }
                if (v < 0 || v >= nvals) {
                    continue;
                }
                if (def_block[v] < 0) {
                    def_block[v] = b;
                } else if (def_block[v] != b) {
                    cross_block[v] = 1;
                }
            }
        }
    }
    for (i = 0; i < nvals; ++i) {
        if (multi_def[i]) {
            cross_block[i] = 1;
        }
        if (def_at[i] < 0) {
            cross_block[i] = 1;
        } else if (first_use[i] >= 0 && first_use[i] < def_at[i]) {
            /* Conservatively pin values that appear live before their defining instruction. */
            cross_block[i] = 1;
        }
    }

    /* a value defined once and used in its own block only is dead past its last use */
    for (i = 0; i < nvals; ++i) {
        if (!pinned[i] && !cross_block[i] && def_at[i] >= 0) {
            out->live_end[i] = last_use[i] > def_at[i] ? last_use[i] : def_at[i];
            out->slot_end[i] = out->live_end[i];
        }
    }
    if (!enable_reuse) {
        for (i = 0; i < nvals; ++i) {
            out->slot_of[i] = i;
        }
        out->slot_count = nvals;
        raw_frame = out->slot_count * slot_size;
        goto build_stackalloc_offsets;
    }
    /*
     * A value live across blocks shares a slot with others whose live ranges do not meet its own, over the
     * blocks: one slot each took Linux's siphash_1u32() to a 2 KiB frame.  Not when the blocks cannot be
     * followed (a label's address taken, an asm goto) or the function is too big for the sets.
     */
    span_lo = (int *)malloc((size_t)nvals * sizeof(*span_lo));
    span_hi = (int *)malloc((size_t)nvals * sizeof(*span_hi));
    if (span_lo != NULL && span_hi != NULL) {
        int rc = x86_value_spans(f, alias, span_lo, span_hi);

        spans = rc == 0;
        if (rc < 0) {
            free(span_lo);
            free(span_hi);
            span_lo = span_hi = NULL;
        }
    }
    if (span_lo == NULL || span_hi == NULL) {
        set_diag(diag, "out of memory building stack slot layout");
        free(def_at);
        free(first_use);
        free(last_use);
        free(def_block);
        free(use_block);
        free(next_start);
        free(next_end);
        free(instr_block);
        free(block_start);
        free(block_end);
        free(start_head);
        free(end_head);
        free(free_slots);
        free(pinned);
        free(cross_block);
        free(multi_def);
        free(span_lo);
        free(span_hi);
        free(is_word);
        free(free_words);
        slot_layout_free(out);
        return -1;
    }
    /*
     * i386 slots of 8 bytes, which a function with any value of 8 bytes has: the words among the values take
     * 4-byte slots of their own pool instead, right below the slots, as twice as many fit -- Linux's
     * do_sys_poll() has 8-byte times and was 4 bytes over 1 KiB.  A value nothing names keeps a slot as before,
     * which i386_peephole_dead_slots() then drops.
     */
    if (reuse_always && slot_size == 8) {
        is_word = (unsigned char *)calloc((size_t)nvals, 1);
        free_words = (int *)malloc((size_t)nvals * sizeof(*free_words));
        if (is_word == NULL || free_words == NULL) {
            free(is_word);
            free(free_words);
            is_word = NULL;
            free_words = NULL;
        } else {
            for (i = 0; i < nvals; ++i) {
                is_word[i] = cc_ssa_value_class(f, i) == CC_VAL_I64 && cc_ssa_value_size(f, i) <= 4 &&
                             (def_at[i] >= 0 || first_use[i] >= 0 || (spans && span_hi[i] >= 0));
            }
        }
    }
    for (i = 0; i < nvals; ++i) {
        int s;
        int e;
        if (pinned[i] || (cross_block[i] && (!spans || span_hi[i] < 0))) {
            if (is_word != NULL && is_word[i]) {
                out->word_of[i] = next_word++;
            } else {
                out->slot_of[i] = next_slot++;
            }
            continue;
        }
        if (cross_block[i]) {
            s = span_lo[i];
            e = span_hi[i];
            /* written back after its range, it would overwrite the slot's next value */
            out->slot_end[i] = e;
            if ((size_t)e > f->instr_count) {
                e = (int)f->instr_count;
            }
            next_start[i] = start_head[s];
            start_head[s] = i;
            next_end[i] = end_head[e];
            end_head[e] = i;
            continue;
        }
        s = def_at[i];
        if (s < 0) {
            s = 0;
        }
        def_at[i] = s;
        if (last_use[i] < def_at[i]) {
            last_use[i] = def_at[i];
        }
        e = last_use[i];
        if ((size_t)s > f->instr_count) {
            s = (int)f->instr_count;
        }
        if ((size_t)e > f->instr_count) {
            e = (int)f->instr_count;
        }
        next_start[i] = start_head[s];
        start_head[s] = i;
        next_end[i] = end_head[e];
        end_head[e] = i;
    }

    for (j = 0; j <= f->instr_count; ++j) {
        int v;
        for (v = start_head[j]; v >= 0; v = next_start[v]) {
            if (is_word != NULL && is_word[v]) {
                out->word_of[v] = allocate_slot(free_words, &free_word_count, &next_word);
            } else {
                out->slot_of[v] = allocate_slot(free_slots, &free_count, &next_slot);
            }
        }
        for (v = end_head[j]; v >= 0; v = next_end[v]) {
            int slot = out->slot_of[v];
            if (pinned[v] || (cross_block[v] && !spans)) {
                continue;
            }
            if (out->word_of[v] >= 0 && free_word_count < nvals) {
                free_words[free_word_count++] = out->word_of[v];
            } else if (slot >= 0 && free_count < nvals) {
                free_slots[free_count++] = slot;
            }
        }
    }
    for (i = 0; i < nvals; ++i) {
        if (out->slot_of[i] < 0 && out->word_of[i] < 0) {
            out->slot_of[i] = allocate_slot(free_slots, &free_count, &next_slot);
        }
    }
    if (reuse_always && order_slots_by_use(f, out, next_slot) != 0) {
        set_diag(diag, "out of memory building stack slot layout");
        free(def_at);
        free(first_use);
        free(last_use);
        free(def_block);
        free(use_block);
        free(next_start);
        free(next_end);
        free(instr_block);
        free(block_start);
        free(block_end);
        free(start_head);
        free(end_head);
        free(free_slots);
        free(pinned);
        free(cross_block);
        free(multi_def);
        free(span_lo);
        free(span_hi);
        free(is_word);
        free(free_words);
        slot_layout_free(out);
        return -1;
    }

    out->slot_count = next_slot;
    out->word_count = next_word;
    raw_frame = out->slot_count * slot_size + 4 * next_word;
build_stackalloc_offsets:
    if (reuse_always) {
        /*
         * i386: the stack objects of SMALL_STACK_OBJECT bytes or fewer -- a local whose address is taken, a
         * small structure, read and written often -- first, right under %ebp at a short displacement; the
         * slots start below them (bias).  One of 16 bytes or more on a 16-byte boundary, as below.
         */
        int small = 0;

        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];
            int bytes = in->imm > 0 ? (int)in->imm : 1;

            if (in->op != CC_SSA_STACKALLOC || in->dst < 0 || in->dst >= nvals || bytes > SMALL_STACK_OBJECT) {
                continue;
            }
            small += (bytes + 3) & ~3;
            while (bytes >= 16 && small % 16 != 8) {
                small += 4;
            }
            out->stackalloc_off[in->dst] = -small;
        }
        out->bias = small;
        if (cc_backend_checked_frame_add(&raw_frame, small, diag, "stack allocation layout") != 0) {
            slot_layout_free(out);
            return -1;
        }
    }
    for (j = 0; j < f->instr_count; ++j) {
        const cc_ssa_instr_t *in = &f->instrs[j];
        int bytes;

        if (in->op != CC_SSA_STACKALLOC || in->dst < 0 || in->dst >= nvals) {
            continue;
        }
        if (reuse_always && (in->imm > 0 ? in->imm : 1) <= SMALL_STACK_OBJECT) {
            /* placed above */
            continue;
        }
        bytes = in->imm > 0 ? (int)in->imm : 1;
        bytes = ((bytes + slot_size - 1) / slot_size) * slot_size;
        if (cc_backend_checked_frame_add(&raw_frame, bytes, diag, "stack allocation layout") != 0) {
            slot_layout_free(out);
            return -1;
        }
        if (in->imm >= 16) {
            /*
             * 16 bytes or more (a vector, a 16-aligned structure, anything
             * gcc-built code may use movaps on) on a 16-byte boundary: the
             * frame pointer is on one on x86-64, 8 past one on i386.
             */
            int residue = cc_target_pointer_size() == 4 ? 8 : 0;

            /* by 4: an i386 frame of word slots is a multiple of 4 only */
            while (raw_frame % 16 != residue) {
                raw_frame += 4;
            }
        }
        out->stackalloc_off[in->dst] = -raw_frame;
    }
    free(def_at);
    free(first_use);
    free(last_use);
    free(def_block);
    free(use_block);
    free(next_start);
    free(next_end);
    free(instr_block);
    free(block_start);
    free(block_end);
    free(start_head);
    free(end_head);
    free(free_slots);
    free(pinned);
    free(cross_block);
    free(multi_def);
    free(span_lo);
    free(span_hi);
    free(is_word);
    free(free_words);
    out->frame_bytes = raw_frame;
    return 0;
}

int call_arg_is_ldouble_abi(const cc_ssa_instr_t *in, size_t arg_index) {
    if (in == NULL || in->call_arg_abi == NULL || arg_index >= in->arg_count) {
        return 0;
    }
    return in->call_arg_abi[arg_index] == CC_CALL_ARG_ABI_LDOUBLE ? 1 : 0;
}

int call_arg_is_agg_mem_abi(const cc_ssa_instr_t *in, size_t arg_index) {
    if (in == NULL || in->call_arg_abi == NULL || arg_index >= in->arg_count) {
        return 0;
    }
    return CC_ABI_KIND(in->call_arg_abi[arg_index]) == CC_CALL_ARG_ABI_AGG_MEM ? 1 : 0;
}

int call_arg_is_agg_reg_abi(const cc_ssa_instr_t *in, size_t arg_index) {
    if (in == NULL || in->call_arg_abi == NULL || arg_index >= in->arg_count) {
        return 0;
    }
    return CC_ABI_KIND(in->call_arg_abi[arg_index]) == CC_CALL_ARG_ABI_AGG_REG ? 1 : 0;
}

long call_arg_size_bytes(const cc_ssa_instr_t *in, size_t arg_index, long fallback) {
    if (fallback <= 0) {
        fallback = 4;
    }
    if (in == NULL || in->call_arg_sizes == NULL || arg_index >= in->arg_count || in->call_arg_sizes[arg_index] == 0) {
        return fallback;
    }
    return in->call_arg_sizes[arg_index];
}

long x86_64_param_size_bytes(const cc_ssa_function_t *f, int param_index, long fallback) {
    size_t i;
    if (fallback <= 0) {
        fallback = 8;
    }
    if (f == NULL || param_index < 0) {
        return fallback;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        if (in->op == CC_SSA_PARAM && in->param_index == param_index) {
            return in->imm > 0 ? in->imm : fallback;
        }
    }
    return fallback;
}

size_t align_up_size(size_t v, size_t align) {
    if (align == 0) {
        return v;
    }
    return (v + (align - 1)) & ~(align - 1);
}

/**
 * Store %st0, truncated, as a 64-bit integer at @p out -- unsigned if
 * @p is_unsigned: fistpll converts to a signed one, so a value of 2^63 or
 * more is converted less 2^63 and given its top bit back.  @p k (4 bytes
 * at it, base register @p base) holds the float 2^63 meanwhile.  Clobbers
 * %eax.
 */
void emit_x87_store_i64(FILE *fp, const char *base, int out, int k, int is_unsigned) {
    unsigned long lid;

    if (!is_unsigned) {
        fprintf(fp, "\tfistpll %d(%s)\n", out, base);
        return;
    }
    lid = next_aux_label_id();
    fprintf(fp, "\tmovl $0x5f000000, %d(%s)\n", k, base);
    fprintf(fp, "\tflds %d(%s)\n", k, base);
    fprintf(fp, "\tfcomp %%st(1)\n");
    fprintf(fp, "\tfnstsw %%ax\n");
    fprintf(fp, "\tsahf\n");
    fprintf(fp, "\tjbe .L__cc_f2u64_big_%lu\n", lid);
    fprintf(fp, "\tfistpll %d(%s)\n", out, base);
    fprintf(fp, "\tjmp .L__cc_f2u64_done_%lu\n", lid);
    fprintf(fp, ".L__cc_f2u64_big_%lu:\n", lid);
    fprintf(fp, "\tfsubs %d(%s)\n", k, base);
    fprintf(fp, "\tfistpll %d(%s)\n", out, base);
    fprintf(fp, "\txorl $0x80000000, %d(%s)\n", out + 4, base);
    fprintf(fp, ".L__cc_f2u64_done_%lu:\n", lid);
}

int cc_emit_gas(const cc_ssa_module_t *m, const char *path, const char *src_path,
                int emit_debug, const cc_target_desc_t *tgt, int pic, cc_diag_t *diag) {
    FILE *fp;

    if (diag != NULL) {
        diag->path[0] = '\0';
        diag->line = 0;
        diag->col = 0;
        diag->error_count = 0;
        diag->message[0] = '\0';
    }

    fp = fopen(path, "w");
    if (fp == NULL) {
        set_diag(diag, "failed to open assembly output");
        return -1;
    }

    if (emit_debug && src_path != NULL) {
        fprintf(fp, ".file 1 \"%s\"\n", src_path);
    }
    if (emit_debug && cc_options_current() != NULL && !cc_options_current()->unwind_tables) {
        /*
         * -fno-asynchronous-unwind-tables: the frame descriptions are for a debugger only, in .debug_frame,
         * not loaded with the code in .eh_frame (Linux's 16-bit setup code has no room for them)
         */
        fprintf(fp, "\t.cfi_sections .debug_frame\n");
    }
    if (emit_globals(fp, m, tgt->pointer_size, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            set_diag(diag, "global emission failed");
        }
        fclose(fp);
        return -1;
    }
    emit_compiler_stamp(fp);

    if (tgt->id == CC_TARGET_I386) {
        /*
         * -m16: the same 32-bit code, assembled with operand- and
         * address-size prefixes to run in real mode -- Linux's realmode
         * trampoline and boot setup are built so.
         */
        fprintf(fp, cc_options_current() != NULL && cc_options_current()->code16 ? ".code16gcc\n" : ".code32\n");
        emit_toplevel_asm(fp, m);
        if (emit_i386(fp, m, src_path, emit_debug, diag) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                set_diag(diag, "i386 emission failed");
            }
            fclose(fp);
            return -1;
        }
    } else {
        emit_toplevel_asm(fp, m);
        if (emit_x86_64(fp, m, src_path, emit_debug, pic, diag) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                set_diag(diag, "x86_64 emission failed");
            }
            fclose(fp);
            return -1;
        }
    }

    /* a nested function's trampoline runs on the stack */
    fprintf(fp, "\n.section .note.GNU-stack,\"%s\",@progbits\n", m->needs_exec_stack ? "x" : "");

    if (fclose(fp) != 0) {
        set_diag(diag, "failed to finalize assembly output");
        return -1;
    }

    return 0;
}
