#include "as_dwline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The line table of `.debug_line`.  See as_dwline.h. */

enum {
    DW_LNS_copy = 1,
    DW_LNS_advance_pc = 2,
    DW_LNS_advance_line = 3,
    DW_LNS_set_file = 4,
    DW_LNS_set_column = 5,
    DW_LNS_negate_stmt = 6,
    DW_LNS_set_basic_block = 7,
    DW_LNS_set_prologue_end = 10,
    DW_LNS_set_epilogue_begin = 11,
    DW_LNS_set_isa = 12,
    DW_LNE_end_sequence = 1,
    DW_LNE_set_address = 2,
    DW_LNE_set_discriminator = 4,
    DW_LNCT_path = 1,
    DW_LNCT_directory_index = 2,
    DW_FORM_string = 0x08,
    DW_FORM_udata = 0x0f,
    OPCODE_BASE = 13,
};

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} buf_t;

static int put(buf_t *b, const void *p, size_t n) {
    if (b->len + n > b->cap) {
        size_t cap = b->cap != 0 ? b->cap * 2 : 256;
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

static int u8(buf_t *b, unsigned v) {
    uint8_t c = (uint8_t)v;

    return put(b, &c, 1);
}

static int le(buf_t *b, uint64_t v, unsigned width) {
    uint8_t bytes[8];
    unsigned i;

    for (i = 0; i < width; ++i) {
        bytes[i] = (uint8_t)(v >> (8 * i));
    }
    return put(b, bytes, width);
}

static int uleb(buf_t *b, uint64_t v) {
    do {
        unsigned c = (unsigned)(v & 0x7fu);

        v >>= 7;
        if (u8(b, c | (v != 0 ? 0x80u : 0u)) != 0) {
            return -1;
        }
    } while (v != 0);
    return 0;
}

static int sleb(buf_t *b, int64_t v) {
    for (;;) {
        unsigned c = (unsigned)(v & 0x7f);
        int done;

        v >>= 7;
        done = (v == 0 && (c & 0x40u) == 0) || (v == -1 && (c & 0x40u) != 0);
        if (u8(b, c | (done ? 0u : 0x80u)) != 0) {
            return -1;
        }
        if (done) {
            return 0;
        }
    }
}

static int str(buf_t *b, const char *s, size_t n) {
    return put(b, s, n) != 0 || u8(b, 0) != 0 ? -1 : 0;
}

/* The directories of the table: each a stretch of some file's name, or
 * what a `.file` gave for one. */
typedef struct {
    const char *text;
    size_t len;
} dir_t;

typedef struct {
    dir_t *items;
    size_t count;
} dirs_t;

/* The number of a directory, which is added if it is new.  -1 for no
 * memory. */
static long dir_number(dirs_t *dirs, const char *text, size_t len, size_t first) {
    dir_t *grown;
    size_t i;

    for (i = first; i < dirs->count; ++i) {
        if (dirs->items[i].len == len && memcmp(dirs->items[i].text, text, len) == 0) {
            return (long)i;
        }
    }
    grown = (dir_t *)realloc(dirs->items, (dirs->count + 1) * sizeof(dirs->items[0]));
    if (grown == NULL) {
        return -1;
    }
    dirs->items = grown;
    grown[dirs->count].text = text;
    grown[dirs->count].len = len;
    return (long)dirs->count++;
}

typedef struct {
    const char *base; /* the name less its directories */
    unsigned dir;
} entry_t;

static int add_fixup(as_dwline_table_t *out, size_t offset, unsigned width, const char *section, int64_t addend) {
    as_dwline_fixup_t *grown =
        (as_dwline_fixup_t *)realloc(out->fixups, (out->fixup_count + 1) * sizeof(out->fixups[0]));

    if (grown == NULL) {
        return -1;
    }
    out->fixups = grown;
    grown[out->fixup_count].offset = offset;
    grown[out->fixup_count].width = width;
    grown[out->fixup_count].section = section;
    grown[out->fixup_count].addend = addend;
    out->fixup_count++;
    return 0;
}

/* The rows of one section, as a sequence: the address set, then for
 * each row what has changed since the one before and a copy, then the
 * section's end. */
static int sequence(buf_t *b, as_dwline_table_t *out, const as_dwline_row_t *rows, size_t row_count,
                    const char *section, uint64_t end, unsigned ptr) {
    /* (Each sequence begins with the registers as the header has them.) */
    unsigned isa_now = 0;
    unsigned *isa_io = &isa_now;
    uint64_t address = 0;
    unsigned file = 1;
    long long line = 1;
    unsigned column = 0;
    unsigned is_stmt = 1;
    int begun = 0;
    int rows_before = 0;
    size_t i;

    for (i = 0; i < row_count; ++i) {
        const as_dwline_row_t *r = &rows[i];

        if (strcmp(r->section, section) != 0) {
            continue;
        }
        if (!begun) {
            if (u8(b, 0) != 0 || uleb(b, 1u + ptr) != 0 || u8(b, DW_LNE_set_address) != 0 ||
                add_fixup(out, b->len, ptr, section, (int64_t)r->offset) != 0 || le(b, 0, ptr) != 0) {
                return -1;
            }
            address = r->offset;
            begun = 1;
        }
        if (r->offset != address) {
            if (u8(b, DW_LNS_advance_pc) != 0 || uleb(b, r->offset - address) != 0) {
                return -1;
            }
            address = r->offset;
        } else if ((r->flags & AS_DWLINE_VIEW_RESET) != 0 && rows_before) {
            /* The address said again, at which a reader counts its
             * views anew -- which is how GNU as says it. */
            if (u8(b, 0) != 0 || uleb(b, 1u + ptr) != 0 || u8(b, DW_LNE_set_address) != 0 ||
                add_fixup(out, b->len, ptr, section, (int64_t)r->offset) != 0 || le(b, 0, ptr) != 0) {
                return -1;
            }
        }
        rows_before = 1;
        if (r->file != file) {
            if (u8(b, DW_LNS_set_file) != 0 || uleb(b, r->file) != 0) {
                return -1;
            }
            file = r->file;
        }
        if (r->column != column) {
            if (u8(b, DW_LNS_set_column) != 0 || uleb(b, r->column) != 0) {
                return -1;
            }
            column = r->column;
        }
        if ((long long)r->line != line) {
            if (u8(b, DW_LNS_advance_line) != 0 || sleb(b, (long long)r->line - line) != 0) {
                return -1;
            }
            line = (long long)r->line;
        }
        if (((r->flags & AS_DWLINE_IS_STMT) != 0) != (is_stmt != 0)) {
            if (u8(b, DW_LNS_negate_stmt) != 0) {
                return -1;
            }
            is_stmt = !is_stmt;
        }
        if (r->isa != *isa_io) {
            if (u8(b, DW_LNS_set_isa) != 0 || uleb(b, r->isa) != 0) {
                return -1;
            }
            *isa_io = r->isa;
        }
        if (((r->flags & AS_DWLINE_BASIC_BLOCK) != 0 && u8(b, DW_LNS_set_basic_block) != 0) ||
            ((r->flags & AS_DWLINE_PROLOGUE_END) != 0 && u8(b, DW_LNS_set_prologue_end) != 0) ||
            ((r->flags & AS_DWLINE_EPILOGUE_BEGIN) != 0 && u8(b, DW_LNS_set_epilogue_begin) != 0)) {
            return -1;
        }
        if (r->discriminator != 0) {
            buf_t d;

            memset(&d, 0, sizeof(d));
            if (uleb(&d, r->discriminator) != 0 || u8(b, 0) != 0 || uleb(b, 1u + d.len) != 0 ||
                u8(b, DW_LNE_set_discriminator) != 0 || put(b, d.data, d.len) != 0) {
                free(d.data);
                return -1;
            }
            free(d.data);
        }
        if (u8(b, DW_LNS_copy) != 0) {
            return -1;
        }
    }
    if (!begun) {
        return 0;
    }
    if (end > address && (u8(b, DW_LNS_advance_pc) != 0 || uleb(b, end - address) != 0)) {
        return -1;
    }
    return u8(b, 0) != 0 || uleb(b, 1) != 0 || u8(b, DW_LNE_end_sequence) != 0 ? -1 : 0;
}

int as_dwline_build(const as_dwline_file_t *files, size_t file_count, const as_dwline_row_t *rows, size_t row_count,
                    const as_dwline_end_t *ends, size_t end_count, int is_64, as_dwline_table_t *out, char *errbuf,
                    size_t errbuf_sz) {
    /* How many operands each standard opcode has. */
    static const uint8_t lengths[OPCODE_BASE - 1] = {0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1};
    unsigned ptr = is_64 ? 8u : 4u;
    const as_dwline_file_t *zero = NULL;
    entry_t *entries = NULL;
    dirs_t dirs;
    unsigned highest = 0;
    size_t header_length_at;
    size_t i;
    size_t k;
    buf_t b;
    int v5;
    int rc = -1;

    memset(&b, 0, sizeof(b));
    memset(&dirs, 0, sizeof(dirs));
    memset(out, 0, sizeof(*out));
    if (errbuf != NULL && errbuf_sz != 0) {
        errbuf[0] = '\0';
    }
    for (i = 0; i < file_count; ++i) {
        if (files[i].number == 0) {
            zero = &files[i];
        }
        if (files[i].number > highest) {
            highest = files[i].number;
        }
    }
    v5 = zero != NULL;
    for (i = 0; i < row_count; ++i) {
        for (k = 0; k < file_count; ++k) {
            if (files[k].number == rows[i].file) {
                break;
            }
        }
        if (k == file_count) {
            if (errbuf != NULL && errbuf_sz != 0) {
                snprintf(errbuf, errbuf_sz, ".loc: file number %u has no .file", rows[i].file);
            }
            goto out;
        }
    }

    /*
     * The directories: in DWARF 5 the first is where the compiler ran,
     * which file 0 says; before it there is no such entry, and a file
     * with no directory in its name has the number 0, which is none.
     */
    if (v5) {
        const char *d = zero->dir != NULL ? zero->dir : ".";

        if (dir_number(&dirs, d, strlen(d), 0) < 0) {
            goto oom;
        }
    } else if (dir_number(&dirs, "", 0, 0) < 0) {
        goto oom;
    }
    entries = (entry_t *)calloc((size_t)highest + 1, sizeof(entries[0]));
    if (entries == NULL) {
        goto oom;
    }
    for (i = 0; i < file_count; ++i) {
        const as_dwline_file_t *f = &files[i];
        const char *slash = strrchr(f->name, '/');
        entry_t *e = &entries[f->number];
        long n = 0;

        e->base = slash != NULL ? slash + 1 : f->name;
        if (slash != NULL) {
            /* (All of the name before its last part; `/x` is of `/`.) */
            n = dir_number(&dirs, f->name, slash == f->name ? 1 : (size_t)(slash - f->name), 1);
        } else if (f->dir != NULL && !(v5 && f->number == 0) &&
                   !(v5 && strcmp(f->dir, dirs.items[0].text) == 0 && strlen(f->dir) == dirs.items[0].len)) {
            n = dir_number(&dirs, f->dir, strlen(f->dir), 1);
        }
        if (n < 0) {
            goto oom;
        }
        e->dir = (unsigned)n;
    }

    /* The header. */
    if (le(&b, 0, 4) != 0 || le(&b, v5 ? 5u : 3u, 2) != 0 || (v5 && (u8(&b, ptr) != 0 || u8(&b, 0) != 0))) {
        goto oom;
    }
    header_length_at = b.len;
    if (le(&b, 0, 4) != 0 || u8(&b, 1) != 0 || (v5 && u8(&b, 1) != 0) || u8(&b, 1) != 0 ||
        u8(&b, (unsigned)(-5) & 0xffu) != 0 || u8(&b, 14) != 0 || u8(&b, OPCODE_BASE) != 0 ||
        put(&b, lengths, sizeof(lengths)) != 0) {
        goto oom;
    }
    if (v5) {
        if (u8(&b, 1) != 0 || uleb(&b, DW_LNCT_path) != 0 || uleb(&b, DW_FORM_string) != 0 ||
            uleb(&b, dirs.count) != 0) {
            goto oom;
        }
        for (i = 0; i < dirs.count; ++i) {
            if (str(&b, dirs.items[i].text, dirs.items[i].len) != 0) {
                goto oom;
            }
        }
        if (u8(&b, 2) != 0 || uleb(&b, DW_LNCT_path) != 0 || uleb(&b, DW_FORM_string) != 0 ||
            uleb(&b, DW_LNCT_directory_index) != 0 || uleb(&b, DW_FORM_udata) != 0 ||
            uleb(&b, (uint64_t)highest + 1) != 0) {
            goto oom;
        }
        for (i = 0; i <= highest; ++i) {
            const char *name = entries[i].base != NULL ? entries[i].base : "";

            if (str(&b, name, strlen(name)) != 0 || uleb(&b, entries[i].dir) != 0) {
                goto oom;
            }
        }
    } else {
        for (i = 1; i < dirs.count; ++i) {
            if (str(&b, dirs.items[i].text, dirs.items[i].len) != 0) {
                goto oom;
            }
        }
        if (u8(&b, 0) != 0) {
            goto oom;
        }
        for (i = 1; i <= highest; ++i) {
            /* (A number no .file gave is a name that says so: an empty
             * one would end the table.) */
            const char *name = entries[i].base != NULL ? entries[i].base : "<unused>";

            if (str(&b, name, strlen(name)) != 0 || uleb(&b, entries[i].dir) != 0 || uleb(&b, 0) != 0 ||
                uleb(&b, 0) != 0) {
                goto oom;
            }
        }
        if (u8(&b, 0) != 0) {
            goto oom;
        }
    }
    {
        uint32_t n = (uint32_t)(b.len - header_length_at - 4);

        for (k = 0; k < 4; ++k) {
            b.data[header_length_at + k] = (uint8_t)(n >> (8 * k));
        }
    }

    /* A sequence for each section, in the order their first rows come. */
    for (i = 0; i < row_count; ++i) {
        uint64_t end = 0;
        size_t before;

        for (before = 0; before < i; ++before) {
            if (strcmp(rows[before].section, rows[i].section) == 0) {
                break;
            }
        }
        if (before < i) {
            continue;
        }
        for (k = 0; k < end_count; ++k) {
            if (strcmp(ends[k].section, rows[i].section) == 0) {
                end = ends[k].size;
            }
        }
        if (sequence(&b, out, rows, row_count, rows[i].section, end, ptr) != 0) {
            goto oom;
        }
    }
    {
        uint32_t n = (uint32_t)(b.len - 4);

        for (k = 0; k < 4; ++k) {
            b.data[k] = (uint8_t)(n >> (8 * k));
        }
    }
    out->bytes = b.data;
    out->len = b.len;
    b.data = NULL;
    rc = 0;
    goto out;

oom:
    if (errbuf != NULL && errbuf_sz != 0 && errbuf[0] == '\0') {
        snprintf(errbuf, errbuf_sz, "out of memory");
    }
out:
    free(b.data);
    free(entries);
    free(dirs.items);
    if (rc != 0) {
        free(out->fixups);
        memset(out, 0, sizeof(*out));
    }
    return rc;
}

void as_dwline_table_free(as_dwline_table_t *table) {
    if (table == NULL) {
        return;
    }
    free(table->bytes);
    free(table->fixups);
    memset(table, 0, sizeof(*table));
}
