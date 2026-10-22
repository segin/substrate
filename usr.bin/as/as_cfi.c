#include "as_cfi.h"

#include "as_expr.h"
#include "as_x86_regs.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* Call frame information from the `.cfi_*` directives.  See as_cfi.h. */

enum {
    DW_CFA_nop = 0x00,
    DW_CFA_advance_loc1 = 0x02,
    DW_CFA_advance_loc2 = 0x03,
    DW_CFA_advance_loc4 = 0x04,
    DW_CFA_offset_extended = 0x05,
    DW_CFA_restore_extended = 0x06,
    DW_CFA_undefined = 0x07,
    DW_CFA_same_value = 0x08,
    DW_CFA_register = 0x09,
    DW_CFA_remember_state = 0x0a,
    DW_CFA_restore_state = 0x0b,
    DW_CFA_def_cfa = 0x0c,
    DW_CFA_def_cfa_register = 0x0d,
    DW_CFA_def_cfa_offset = 0x0e,
    DW_CFA_offset_extended_sf = 0x11,
    DW_CFA_def_cfa_sf = 0x12,
    DW_CFA_def_cfa_offset_sf = 0x13,
    DW_CFA_GNU_window_save = 0x2d,
    DW_CFA_advance_loc = 0x40,
    DW_CFA_offset = 0x80,
    DW_CFA_restore = 0xc0,
};

/* How a pointer is written: the low four bits its form, 0x10 that it is
 * less its own address, 0x80 that it is the address of the pointer. */
enum {
    DW_EH_PE_absptr = 0x00,
    DW_EH_PE_udata2 = 0x02,
    DW_EH_PE_udata4 = 0x03,
    DW_EH_PE_udata8 = 0x04,
    DW_EH_PE_sdata2 = 0x0a,
    DW_EH_PE_sdata4 = 0x0b,
    DW_EH_PE_sdata8 = 0x0c,
    DW_EH_PE_pcrel = 0x10,
    DW_EH_PE_omit = 0xff,
};

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} buf_t;

static int buf_put(buf_t *b, const void *p, size_t n) {
    if (b->len + n > b->cap) {
        size_t cap = b->cap != 0 ? b->cap * 2 : 128;
        uint8_t *grown;

        while (cap < b->len + n) {
            cap *= 2;
        }
        grown = (uint8_t *)realloc(b->data, cap);
        if (grown == NULL) {
            return -1;
        }
        b->data = grown;
        b->cap = cap;
    }
    if (n != 0) {
        memcpy(b->data + b->len, p, n);
    }
    b->len += n;
    return 0;
}

static int buf_u8(buf_t *b, unsigned v) {
    uint8_t c = (uint8_t)v;

    return buf_put(b, &c, 1);
}

static int buf_le(buf_t *b, uint64_t v, unsigned width) {
    uint8_t bytes[8];
    unsigned i;

    for (i = 0; i < width; ++i) {
        bytes[i] = (uint8_t)(v >> (8 * i));
    }
    return buf_put(b, bytes, width);
}

static int buf_uleb(buf_t *b, uint64_t v) {
    do {
        unsigned c = (unsigned)(v & 0x7fu);

        v >>= 7;
        if (buf_u8(b, c | (v != 0 ? 0x80u : 0u)) != 0) {
            return -1;
        }
    } while (v != 0);
    return 0;
}

static int buf_sleb(buf_t *b, int64_t v) {
    for (;;) {
        unsigned c = (unsigned)(v & 0x7f);
        int done;

        v >>= 7; /* (an arithmetic shift, of what is signed) */
        done = (v == 0 && (c & 0x40u) == 0) || (v == -1 && (c & 0x40u) != 0);
        if (buf_u8(b, c | (done ? 0u : 0x80u)) != 0) {
            return -1;
        }
        if (done) {
            return 0;
        }
    }
}

/* What is common to the frames that share it. */
typedef struct {
    /* What the frame's rules are before the procedure's first
     * instruction: the instructions of its frame that stand before any
     * that moves on.  (They are the CIE's, and frames that begin alike
     * share it.) */
    uint8_t *initial;
    size_t initial_len;
    int signal;         /* `.cfi_signal_frame` */
    unsigned return_column;
    unsigned personality_enc; /* DW_EH_PE_omit for none */
    const char *personality;
    unsigned lsda_enc;        /* DW_EH_PE_omit for none */
    size_t offset;            /* where it is in the section */
} cie_t;

typedef struct {
    const as_cfi_event_t *events;
    size_t count;
    int is_64;
    int debug_frame;
    as_cfi_frames_t *out;
    buf_t sec;
    cie_t *cies;
    size_t cie_count;
    size_t last_fde; /* where the last frame written begins */
    int have_fde;
    char *errbuf;
    size_t errbuf_sz;
} cfi_t;

static int fail(cfi_t *c, const as_cfi_event_t *ev, const char *fmt, ...) {
    char why[160];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(why, sizeof(why), fmt, ap);
    va_end(ap);
    if (c->errbuf != NULL && c->errbuf_sz != 0) {
        snprintf(c->errbuf, c->errbuf_sz, "%s:%u: %s", ev != NULL && ev->file != NULL ? ev->file : "<input>",
                 ev != NULL ? ev->line : 0u, why);
    }
    return -1;
}

static int add_fixup(cfi_t *c, size_t offset, unsigned width, int pcrel, const char *symbol, int64_t addend) {
    as_cfi_fixup_t *grown =
        (as_cfi_fixup_t *)realloc(c->out->fixups, (c->out->fixup_count + 1) * sizeof(c->out->fixups[0]));

    if (grown == NULL) {
        return -1;
    }
    c->out->fixups = grown;
    grown[c->out->fixup_count].offset = offset;
    grown[c->out->fixup_count].width = width;
    grown[c->out->fixup_count].pcrel = pcrel;
    grown[c->out->fixup_count].symbol = symbol;
    grown[c->out->fixup_count].addend = addend;
    c->out->fixup_count++;
    return 0;
}

static const char *arg_of(const as_directive_t *d, size_t i) {
    const char *a = i < d->arg_count && d->args[i] != NULL ? d->args[i] : "";

    while (isspace((unsigned char)*a)) {
        a++;
    }
    return a;
}

/*
 * A number of a directive, written as a constant expression -- Linux's
 * vDSO has `.cfi_def_cfa esp, IA32_SIGFRAME_sigcontext - 4`, a number
 * from asm-offsets less 4: 0 and the number, or -1.
 */
static int number_of(const as_directive_t *d, size_t i, long long *out) {
    const char *a = arg_of(d, i);

    if (*a == '\0') {
        return -1;
    }
    return as_expr_eval_string(a, NULL, NULL, out) == AS_EXPR_EVAL_OK ? 0 : -1;
}

/*
 * The number DWARF has for a register: of the System V psABI for
 * x86-64, and of GCC's i386 numbering, which is not the order the
 * registers have in an instruction.  -1 for one it has none for.
 */
static int dwarf_register(const as_x86_reg_info_t *r, int is_64) {
    static const int general64[16] = {0, 2, 1, 3, 7, 6, 4, 5, 8, 9, 10, 11, 12, 13, 14, 15};
    static const int general32[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    /* %es %cs %ss %ds %fs %gs, by their number in an instruction. */
    static const int segment64[6] = {50, 51, 52, 53, 54, 55};
    static const int segment32[6] = {40, 41, 42, 43, 44, 45};

    switch (r->cls) {
    case AS_X86_REGCLASS_GENERAL:
        if (is_64) {
            return r->width == 64 && r->number < 16 ? general64[r->number] : -1;
        }
        return r->width == 32 && r->number < 8 ? general32[r->number] : -1;
    case AS_X86_REGCLASS_IP:
        return r->width == (is_64 ? 64 : 32) ? (is_64 ? 16 : 8) : -1;
    case AS_X86_REGCLASS_XMM:
        if (is_64) {
            return r->number < 16 ? 17 + r->number : 67 + (r->number - 16);
        }
        return r->number < 8 ? 21 + r->number : -1;
    case AS_X86_REGCLASS_X87:
        return (is_64 ? 33 : 11) + r->number;
    case AS_X86_REGCLASS_MMX:
        return (is_64 ? 41 : 29) + r->number;
    case AS_X86_REGCLASS_MASK:
        return (is_64 ? 118 : 93) + r->number;
    case AS_X86_REGCLASS_SEGMENT:
        return r->number < 6 ? (is_64 ? segment64[r->number] : segment32[r->number]) : -1;
    default:
        return -1;
    }
}

/* A register of a directive, by name or by number. */
static int register_of(cfi_t *c, const as_cfi_event_t *ev, size_t i, unsigned *out) {
    const char *a = arg_of(ev->d, i);
    const as_x86_reg_info_t *r;
    long long n;
    int dw;

    if (number_of(ev->d, i, &n) == 0) {
        if (n < 0 || n > 0xffff) {
            return fail(c, ev, "%s: %lld is no register's number", ev->d->name, n);
        }
        *out = (unsigned)n;
        return 0;
    }
    if (strcasecmp(a, "%rflags") == 0 || strcasecmp(a, "rflags") == 0) {
        *out = 49;
        return 0;
    }
    if (strcasecmp(a, "%eflags") == 0 || strcasecmp(a, "eflags") == 0) {
        *out = c->is_64 ? 49u : 9u;
        return 0;
    }
    if (strncasecmp(a, "%st(", 4) == 0 && isdigit((unsigned char)a[4]) && a[5] == ')') {
        *out = (unsigned)((c->is_64 ? 33 : 11) + (a[4] - '0'));
        return 0;
    }
    r = as_x86_reg_find(a);
    dw = r != NULL ? dwarf_register(r, c->is_64) : -1;
    if (dw < 0) {
        return fail(c, ev, "%s: '%s' is not a register call frame information has a number for", ev->d->name, a);
    }
    *out = (unsigned)dw;
    return 0;
}

/* The bytes of a pointer in an encoding; 0 for one there is no writing. */
static unsigned encoded_size(unsigned enc, int is_64) {
    switch (enc & 0x0fu) {
    case DW_EH_PE_absptr:
        return is_64 ? 8u : 4u;
    case DW_EH_PE_udata2:
    case DW_EH_PE_sdata2:
        return 2;
    case DW_EH_PE_udata4:
    case DW_EH_PE_sdata4:
        return 4;
    case DW_EH_PE_udata8:
    case DW_EH_PE_sdata8:
        return 8;
    default:
        return 0;
    }
}

/* A pointer to a symbol, in an encoding: its bytes, which a relocation
 * fills. */
static int put_pointer(cfi_t *c, buf_t *b, size_t base, unsigned enc, const char *symbol) {
    unsigned width = encoded_size(enc, c->is_64);

    if (add_fixup(c, base + b->len, width, (enc & 0x70u) == DW_EH_PE_pcrel, symbol, 0) != 0) {
        return -1;
    }
    return buf_le(b, 0, width);
}

static int pad(buf_t *b, size_t from, unsigned align) {
    while ((b->len - from) % align != 0) {
        if (buf_u8(b, DW_CFA_nop) != 0) {
            return -1;
        }
    }
    return 0;
}

/* The CIE that is this one, or a new one written at the section's end:
 * where it is, and how many of the frame's first bytes of instructions
 * it holds. */
static int cie_for(cfi_t *c, const cie_t *want, size_t *offset_out, size_t *initial_out) {
    /* In .eh_frame every entry is a number of 4 bytes, in .debug_frame
     * of an address's. */
    unsigned align = c->debug_frame && c->is_64 ? 8u : 4u;
    buf_t *s = &c->sec;
    cie_t *grown;
    size_t start;
    size_t body;
    size_t i;

    for (i = 0; i < c->cie_count; ++i) {
        const cie_t *have = &c->cies[i];

        /* (A frame that begins as the CIE does and goes on has the
         * CIE, and says the rest itself.) */
        if (have->initial_len <= want->initial_len &&
            (have->initial_len == 0 || memcmp(have->initial, want->initial, have->initial_len) == 0) &&
            have->signal == want->signal &&
            have->return_column == want->return_column && have->personality_enc == want->personality_enc &&
            have->lsda_enc == want->lsda_enc &&
            ((have->personality == NULL && want->personality == NULL) ||
             (have->personality != NULL && want->personality != NULL &&
              strcmp(have->personality, want->personality) == 0))) {
            *offset_out = have->offset;
            *initial_out = have->initial_len;
            return 0;
        }
    }
    start = s->len;
    if (buf_le(s, 0, 4) != 0 || buf_le(s, c->debug_frame ? 0xffffffffu : 0u, 4) != 0 || buf_u8(s, 1) != 0) {
        return -1;
    }
    body = start + 4;
    if (!c->debug_frame) {
        /* z: the length of what follows; P, L, R: how the personality,
         * the tables of the frames and their addresses are written; S:
         * a signal's frame. */
        if (buf_u8(s, 'z') != 0 ||
            (want->personality_enc != DW_EH_PE_omit && buf_u8(s, 'P') != 0) ||
            (want->lsda_enc != DW_EH_PE_omit && buf_u8(s, 'L') != 0) || buf_u8(s, 'R') != 0 ||
            (want->signal && buf_u8(s, 'S') != 0)) {
            return -1;
        }
    } else if (want->signal && buf_u8(s, 'S') != 0) {
        return -1;
    }
    if (buf_u8(s, 0) != 0 || buf_uleb(s, 1) != 0 || buf_sleb(s, c->is_64 ? -8 : -4) != 0 ||
        buf_u8(s, want->return_column) != 0) {
        return -1;
    }
    if (!c->debug_frame) {
        size_t aug_len = 1;

        if (want->personality_enc != DW_EH_PE_omit) {
            aug_len += 1 + encoded_size(want->personality_enc, c->is_64);
        }
        if (want->lsda_enc != DW_EH_PE_omit) {
            aug_len += 1;
        }
        if (buf_uleb(s, aug_len) != 0) {
            return -1;
        }
        if (want->personality_enc != DW_EH_PE_omit &&
            (buf_u8(s, want->personality_enc) != 0 ||
             put_pointer(c, s, 0, want->personality_enc, want->personality) != 0)) {
            return -1;
        }
        if ((want->lsda_enc != DW_EH_PE_omit && buf_u8(s, want->lsda_enc) != 0) ||
            buf_u8(s, DW_EH_PE_pcrel | DW_EH_PE_sdata4) != 0) {
            return -1;
        }
    }
    if (buf_put(s, want->initial, want->initial_len) != 0 || pad(s, start, align) != 0) {
        return -1;
    }
    {
        uint32_t len = (uint32_t)(s->len - body);
        unsigned k;

        for (k = 0; k < 4; ++k) {
            s->data[start + k] = (uint8_t)(len >> (8 * k));
        }
    }
    grown = (cie_t *)realloc(c->cies, (c->cie_count + 1) * sizeof(c->cies[0]));
    if (grown == NULL) {
        return -1;
    }
    c->cies = grown;
    grown[c->cie_count] = *want;
    grown[c->cie_count].initial = NULL;
    if (want->initial_len != 0) {
        grown[c->cie_count].initial = (uint8_t *)malloc(want->initial_len);
        if (grown[c->cie_count].initial == NULL) {
            return -1;
        }
        memcpy(grown[c->cie_count].initial, want->initial, want->initial_len);
    }
    grown[c->cie_count].offset = start;
    c->cie_count++;
    *offset_out = start;
    *initial_out = want->initial_len;
    return 0;
}

/* The directive's name after `.cfi_`. */
static const char *what(const as_cfi_event_t *ev) {
    return ev->d->name + 5;
}

/* An offset from the frame's address, to a register's saved value. */
static int put_offset(cfi_t *c, buf_t *b, unsigned reg, long long off) {
    long long data_align = c->is_64 ? -8 : -4;
    long long factored = off / data_align;

    if (off % data_align != 0) {
        return -2;
    }
    if (factored < 0) {
        return buf_u8(b, DW_CFA_offset_extended_sf) != 0 || buf_uleb(b, reg) != 0 || buf_sleb(b, factored) != 0 ? -1
                                                                                                                 : 0;
    }
    if (reg < 64u) {
        return buf_u8(b, DW_CFA_offset | reg) != 0 || buf_uleb(b, (uint64_t)factored) != 0 ? -1 : 0;
    }
    return buf_u8(b, DW_CFA_offset_extended) != 0 || buf_uleb(b, reg) != 0 || buf_uleb(b, (uint64_t)factored) != 0
               ? -1
               : 0;
}

/* The frame of one procedure: events [first, last], the first a
 * `.cfi_startproc` and the last its `.cfi_endproc`. */
static int one_frame(cfi_t *c, size_t first, size_t last) {
    unsigned align = c->debug_frame && c->is_64 ? 8u : 4u;
    const as_cfi_event_t *start = &c->events[first];
    const as_cfi_event_t *end = &c->events[last];
    int simple = strcasecmp(arg_of(start->d, 0), "simple") == 0;
    buf_t *s = &c->sec;
    buf_t ins;
    cie_t cie;
    const char *lsda = NULL;
    long long cfa_offset;
    long long saved_offset[32];
    size_t saved = 0;
    uint64_t at = start->offset;
    size_t cie_offset = 0;
    /* Where the instructions that are the CIE's end: at the first that
     * moves on, or that cannot be said once for several frames. */
    size_t lead = (size_t)-1;
    size_t fde;
    size_t body;
    size_t i;
    int rc = -1;

    memset(&ins, 0, sizeof(ins));
    memset(&cie, 0, sizeof(cie));
    cie.return_column = c->is_64 ? 16u : 8u;
    cie.personality_enc = DW_EH_PE_omit;
    cie.lsda_enc = DW_EH_PE_omit;
    cfa_offset = simple ? 0 : (c->is_64 ? 8 : 4);
    /* On entry the frame's address is the stack pointer and the word
     * it points at, and the return address is in that word -- unless
     * the procedure is `simple`, and says for itself. */
    if (!simple && (buf_u8(&ins, DW_CFA_def_cfa) != 0 || buf_uleb(&ins, c->is_64 ? 7u : 4u) != 0 ||
                    buf_uleb(&ins, c->is_64 ? 8u : 4u) != 0 ||
                    buf_u8(&ins, DW_CFA_offset | (c->is_64 ? 16u : 8u)) != 0 || buf_uleb(&ins, 1) != 0)) {
        goto oom;
    }

    for (i = first + 1; i < last; ++i) {
        const as_cfi_event_t *ev = &c->events[i];
        const char *name = what(ev);
        unsigned reg = 0;
        unsigned reg2 = 0;
        long long n = 0;
        uint64_t delta;

        if (strcmp(ev->section, start->section) != 0) {
            fail(c, ev, "%s is in another section than its .cfi_startproc", ev->d->name);
            goto out;
        }
        /* What is of the whole frame, and of no place in it. */
        if (strcmp(name, "personality") == 0 || strcmp(name, "lsda") == 0) {
            int is_lsda = name[0] == 'l';

            if (number_of(ev->d, 0, &n) != 0 || n < 0 || n > 0xff ||
                (n != DW_EH_PE_omit && (encoded_size((unsigned)n, c->is_64) == 0 || ev->d->arg_count < 2))) {
                fail(c, ev, "%s: how the pointer is written, and what to", ev->d->name);
                goto out;
            }
            if (is_lsda) {
                cie.lsda_enc = (unsigned)n;
                lsda = n != DW_EH_PE_omit ? arg_of(ev->d, 1) : NULL;
            } else {
                cie.personality_enc = (unsigned)n;
                cie.personality = n != DW_EH_PE_omit ? arg_of(ev->d, 1) : NULL;
            }
            continue;
        }
        if (strcmp(name, "signal_frame") == 0) {
            cie.signal = 1;
            continue;
        }
        if (strcmp(name, "return_column") == 0) {
            if (register_of(c, ev, 0, &reg) != 0) {
                goto out;
            }
            cie.return_column = reg;
            continue;
        }
        if (strcmp(name, "sections") == 0) {
            continue;
        }
        if (strcmp(name, "startproc") == 0) {
            fail(c, ev, ".cfi_startproc in a procedure that has not ended");
            goto out;
        }

        /* How far on this is from the last that said anything. */
        if (ev->offset < at) {
            fail(c, ev, "%s is before the directive before it", ev->d->name);
            goto out;
        }
        delta = ev->offset - at;
        at = ev->offset;
        if (lead == (size_t)-1 &&
            (delta != 0 || strcmp(name, "remember_state") == 0 || strcmp(name, "escape") == 0)) {
            lead = ins.len;
        }
        if (delta != 0) {
            int bad;

            if (delta < 0x40u) {
                bad = buf_u8(&ins, DW_CFA_advance_loc | (unsigned)delta);
            } else if (delta < 0x100u) {
                bad = buf_u8(&ins, DW_CFA_advance_loc1) != 0 || buf_le(&ins, delta, 1) != 0;
            } else if (delta < 0x10000u) {
                bad = buf_u8(&ins, DW_CFA_advance_loc2) != 0 || buf_le(&ins, delta, 2) != 0;
            } else {
                bad = buf_u8(&ins, DW_CFA_advance_loc4) != 0 || buf_le(&ins, delta, 4) != 0;
            }
            if (bad) {
                goto oom;
            }
        }

        if (strcmp(name, "def_cfa") == 0) {
            if (register_of(c, ev, 0, &reg) != 0) {
                goto out;
            }
            if (number_of(ev->d, 1, &n) != 0) {
                fail(c, ev, ".cfi_def_cfa: a register and an offset");
                goto out;
            }
            cfa_offset = n;
            if (n < 0) {
                if (buf_u8(&ins, DW_CFA_def_cfa_sf) != 0 || buf_uleb(&ins, reg) != 0 ||
                    buf_sleb(&ins, n / (c->is_64 ? -8 : -4)) != 0) {
                    goto oom;
                }
            } else if (buf_u8(&ins, DW_CFA_def_cfa) != 0 || buf_uleb(&ins, reg) != 0 ||
                       buf_uleb(&ins, (uint64_t)n) != 0) {
                goto oom;
            }
        } else if (strcmp(name, "def_cfa_register") == 0) {
            if (register_of(c, ev, 0, &reg) != 0) {
                goto out;
            }
            if (buf_u8(&ins, DW_CFA_def_cfa_register) != 0 || buf_uleb(&ins, reg) != 0) {
                goto oom;
            }
        } else if (strcmp(name, "def_cfa_offset") == 0 || strcmp(name, "adjust_cfa_offset") == 0) {
            if (number_of(ev->d, 0, &n) != 0) {
                fail(c, ev, "%s: an offset", ev->d->name);
                goto out;
            }
            cfa_offset = name[0] == 'a' ? cfa_offset + n : n;
            if (cfa_offset < 0) {
                if (buf_u8(&ins, DW_CFA_def_cfa_offset_sf) != 0 ||
                    buf_sleb(&ins, cfa_offset / (c->is_64 ? -8 : -4)) != 0) {
                    goto oom;
                }
            } else if (buf_u8(&ins, DW_CFA_def_cfa_offset) != 0 || buf_uleb(&ins, (uint64_t)cfa_offset) != 0) {
                goto oom;
            }
        } else if (strcmp(name, "offset") == 0 || strcmp(name, "rel_offset") == 0) {
            int prc;

            if (register_of(c, ev, 0, &reg) != 0) {
                goto out;
            }
            if (number_of(ev->d, 1, &n) != 0) {
                fail(c, ev, "%s: a register and an offset", ev->d->name);
                goto out;
            }
            /* (Relative to the register the frame is found by, which
             * is the frame's address less its offset.) */
            if (name[0] == 'r') {
                n -= cfa_offset;
            }
            prc = put_offset(c, &ins, reg, n);
            if (prc == -2) {
                fail(c, ev, "%s: %lld is not a number of whole words", ev->d->name, n);
                goto out;
            }
            if (prc != 0) {
                goto oom;
            }
        } else if (strcmp(name, "val_offset") == 0) {
            long long data_align = c->is_64 ? -8 : -4;

            if (register_of(c, ev, 0, &reg) != 0) {
                goto out;
            }
            if (number_of(ev->d, 1, &n) != 0 || n % data_align != 0) {
                fail(c, ev, ".cfi_val_offset: a register and an offset of whole words");
                goto out;
            }
            /* The register's value is the frame's address and this,
             * not what is in memory there. */
            if (n / data_align < 0
                    ? (buf_u8(&ins, 0x15) != 0 || buf_uleb(&ins, reg) != 0 || buf_sleb(&ins, n / data_align) != 0)
                    : (buf_u8(&ins, 0x14) != 0 || buf_uleb(&ins, reg) != 0 ||
                       buf_uleb(&ins, (uint64_t)(n / data_align)) != 0)) {
                goto oom;
            }
        } else if (strcmp(name, "register") == 0) {
            if (register_of(c, ev, 0, &reg) != 0 || register_of(c, ev, 1, &reg2) != 0) {
                goto out;
            }
            if (buf_u8(&ins, DW_CFA_register) != 0 || buf_uleb(&ins, reg) != 0 || buf_uleb(&ins, reg2) != 0) {
                goto oom;
            }
        } else if (strcmp(name, "restore") == 0) {
            size_t k;

            /* (One register or several.) */
            for (k = 0; k < ev->d->arg_count; ++k) {
                if (register_of(c, ev, k, &reg) != 0) {
                    goto out;
                }
                if (reg < 64u ? buf_u8(&ins, DW_CFA_restore | reg) != 0
                              : (buf_u8(&ins, DW_CFA_restore_extended) != 0 || buf_uleb(&ins, reg) != 0)) {
                    goto oom;
                }
            }
        } else if (strcmp(name, "undefined") == 0 || strcmp(name, "same_value") == 0) {
            if (register_of(c, ev, 0, &reg) != 0) {
                goto out;
            }
            if (buf_u8(&ins, name[0] == 'u' ? DW_CFA_undefined : DW_CFA_same_value) != 0 ||
                buf_uleb(&ins, reg) != 0) {
                goto oom;
            }
        } else if (strcmp(name, "remember_state") == 0) {
            if (saved == sizeof(saved_offset) / sizeof(saved_offset[0])) {
                fail(c, ev, ".cfi_remember_state: too many remembered");
                goto out;
            }
            saved_offset[saved++] = cfa_offset;
            if (buf_u8(&ins, DW_CFA_remember_state) != 0) {
                goto oom;
            }
        } else if (strcmp(name, "restore_state") == 0) {
            if (saved == 0) {
                fail(c, ev, ".cfi_restore_state with nothing remembered");
                goto out;
            }
            cfa_offset = saved_offset[--saved];
            if (buf_u8(&ins, DW_CFA_restore_state) != 0) {
                goto oom;
            }
        } else if (strcmp(name, "escape") == 0) {
            size_t k;

            for (k = 0; k < ev->d->arg_count; ++k) {
                if (number_of(ev->d, k, &n) != 0 || n < -128 || n > 255) {
                    fail(c, ev, ".cfi_escape: bytes");
                    goto out;
                }
                if (buf_u8(&ins, (unsigned)n & 0xffu) != 0) {
                    goto oom;
                }
            }
        } else if (strcmp(name, "window_save") == 0) {
            if (buf_u8(&ins, DW_CFA_GNU_window_save) != 0) {
                goto oom;
            }
        } else {
            fail(c, ev, "%s is not a directive of call frame information this assembler has", ev->d->name);
            goto out;
        }
    }
    if (strcmp(end->section, start->section) != 0 || end->offset < start->offset) {
        fail(c, end, ".cfi_endproc is not after its .cfi_startproc in the section");
        goto out;
    }
    if (c->debug_frame) {
        /* .debug_frame has no personality and no tables. */
        cie.personality_enc = DW_EH_PE_omit;
        cie.personality = NULL;
        cie.lsda_enc = DW_EH_PE_omit;
        lsda = NULL;
    }
    if (lead == (size_t)-1) {
        lead = ins.len;
    }
    cie.initial = ins.data;
    cie.initial_len = lead;
    if (cie_for(c, &cie, &cie_offset, &lead) != 0) {
        goto oom;
    }

    fde = s->len;
    body = fde + 4;
    if (buf_le(s, 0, 4) != 0) {
        goto oom;
    }
    if (c->debug_frame) {
        unsigned ptr = c->is_64 ? 8u : 4u;

        /* The CIE's place in the section, the procedure's address and
         * its length. */
        if (add_fixup(c, s->len, 4, 0, ".debug_frame", (int64_t)cie_offset) != 0 || buf_le(s, 0, 4) != 0 ||
            add_fixup(c, s->len, ptr, 0, start->section, (int64_t)start->offset) != 0 || buf_le(s, 0, ptr) != 0 ||
            buf_le(s, end->offset - start->offset, ptr) != 0) {
            goto oom;
        }
    } else {
        /* How far back the CIE is; the procedure's address less this
         * field's; its length; and the pointer to its table. */
        if (buf_le(s, (uint64_t)(s->len - cie_offset), 4) != 0 ||
            add_fixup(c, s->len, 4, 1, start->section, (int64_t)start->offset) != 0 || buf_le(s, 0, 4) != 0 ||
            buf_le(s, end->offset - start->offset, 4) != 0 ||
            buf_uleb(s, lsda != NULL ? encoded_size(cie.lsda_enc, c->is_64) : 0u) != 0 ||
            (lsda != NULL && put_pointer(c, s, 0, cie.lsda_enc, lsda) != 0)) {
            goto oom;
        }
    }
    if (buf_put(s, ins.data + lead, ins.len - lead) != 0 || pad(s, fde, align) != 0) {
        goto oom;
    }
    {
        uint32_t len = (uint32_t)(s->len - body);
        unsigned k;

        for (k = 0; k < 4; ++k) {
            s->data[fde + k] = (uint8_t)(len >> (8 * k));
        }
    }
    c->last_fde = fde;
    c->have_fde = 1;
    rc = 0;
    goto out;

oom:
    if (c->errbuf != NULL && c->errbuf_sz != 0 && c->errbuf[0] == '\0') {
        snprintf(c->errbuf, c->errbuf_sz, "out of memory");
    }
out:
    free(ins.data);
    return rc;
}

void as_cfi_sections(const as_cfi_event_t *events, size_t count, int *eh_frame, int *debug_frame) {
    int said = 0;
    size_t i;
    size_t k;

    *eh_frame = 0;
    *debug_frame = 0;
    for (i = 0; i < count; ++i) {
        const as_directive_t *d = events[i].d;

        if (strcmp(d->name, ".cfi_sections") != 0) {
            continue;
        }
        said = 1;
        for (k = 0; k < d->arg_count; ++k) {
            const char *a = arg_of(d, k);

            if (strcmp(a, ".eh_frame") == 0) {
                *eh_frame = 1;
            } else if (strcmp(a, ".debug_frame") == 0) {
                *debug_frame = 1;
            }
        }
    }
    if (!said) {
        *eh_frame = 1;
    }
}

int as_cfi_build(const as_cfi_event_t *events, size_t count, int is_64, int debug_frame, as_cfi_frames_t *out,
                 char *errbuf, size_t errbuf_sz) {
    cfi_t c;
    size_t i;
    size_t open = (size_t)-1;
    int rc = 0;

    memset(&c, 0, sizeof(c));
    memset(out, 0, sizeof(*out));
    if (errbuf != NULL && errbuf_sz != 0) {
        errbuf[0] = '\0';
    }
    c.events = events;
    c.count = count;
    c.is_64 = is_64;
    c.debug_frame = debug_frame;
    c.out = out;
    c.errbuf = errbuf;
    c.errbuf_sz = errbuf_sz;

    for (i = 0; i < count && rc == 0; ++i) {
        const char *name = what(&events[i]);

        if (strcmp(name, "sections") == 0) {
            continue;
        }
        if (strcmp(name, "startproc") == 0) {
            if (open != (size_t)-1) {
                rc = fail(&c, &events[i], ".cfi_startproc in a procedure that has not ended");
            }
            open = i;
        } else if (strcmp(name, "endproc") == 0) {
            if (open == (size_t)-1) {
                rc = fail(&c, &events[i], ".cfi_endproc without a .cfi_startproc");
            } else {
                rc = one_frame(&c, open, i);
                open = (size_t)-1;
            }
        } else if (open == (size_t)-1) {
            rc = fail(&c, &events[i], "%s outside a procedure: there is no .cfi_startproc before it",
                      events[i].d->name);
        }
    }
    if (rc == 0 && open != (size_t)-1) {
        rc = fail(&c, &events[open], ".cfi_startproc with no .cfi_endproc");
    }
    /* The last frame is filled out to the section's alignment, which in
     * 64-bit code is more than a frame's. */
    if (rc == 0 && c.have_fde && is_64 && !debug_frame) {
        if (pad(&c.sec, 0, 8) != 0) {
            rc = -1;
            if (errbuf != NULL && errbuf_sz != 0) {
                snprintf(errbuf, errbuf_sz, "out of memory");
            }
        } else {
            uint32_t len = (uint32_t)(c.sec.len - c.last_fde - 4);
            unsigned k;

            for (k = 0; k < 4; ++k) {
                c.sec.data[c.last_fde + k] = (uint8_t)(len >> (8 * k));
            }
        }
    }
    for (i = 0; i < c.cie_count; ++i) {
        free(c.cies[i].initial);
    }
    free(c.cies);
    if (rc != 0) {
        free(c.sec.data);
        free(out->fixups);
        memset(out, 0, sizeof(*out));
        return -1;
    }
    out->bytes = c.sec.data;
    out->len = c.sec.len;
    return 0;
}

void as_cfi_frames_free(as_cfi_frames_t *frames) {
    if (frames == NULL) {
        return;
    }
    free(frames->bytes);
    free(frames->fixups);
    memset(frames, 0, sizeof(*frames));
}
