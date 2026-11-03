/*
 * elf_merge.c -- the merging of SHF_MERGE sections in a link, as GNU ld
 * (bfd's merge.c) does it.
 *
 * A section that is SHF_MERGE is a table of entries of sh_entsize bytes,
 * or with SHF_STRINGS of strings of characters of that size, which may
 * be shared: the link keeps one copy of each, and with strings one that
 * ends another is not kept at all but found at the end of that one (tail
 * merging).  The sections of one output section alike in entry size,
 * strings or not, and alignment are one group, laid out as one block
 * where the first of them would have gone; each input section then has a
 * table from its offsets to the block's, which symbol values and the
 * targets of relocations go through.
 */

#include <elf_private.h>

struct merge_entry {
    const uint8_t *data;
    size_t len;                 /* with its terminator, for a string */
    uint64_t align;
    uint64_t off;               /* in the group's block */
    struct merge_entry *suffix_of;      /* the entry it ends, with tail merging */
    struct merge_entry *next;           /* in its hash bucket */
    uint32_t hash;
};

struct merge_ref {
    uint64_t in_off;            /* where it starts in the input section */
    uint64_t len;
    struct merge_entry *entry;
};

struct merge_group {
    char *out_name;
    uint64_t entsize;
    int strings;
    uint64_t sec_align;         /* the alignment of its sections, which tells it from others */
    uint64_t align;             /* the block's */
    struct merge_entry **entries;       /* in the order first met */
    size_t entry_count;
    size_t entry_cap;
    struct merge_entry **buckets;
    size_t nbuckets;
    uint8_t *blob;
    size_t blob_size;
    uint64_t base;              /* where the block went in its output section */
    int placed;
};

struct merge_sec {
    size_t group;               /* SIZE_MAX: not merged */
    struct merge_ref *refs;
    size_t ref_count;
};

struct elf_merge_state {
    struct merge_group *groups;
    size_t group_count;
    size_t group_cap;
    struct merge_sec **secs;    /* [input][section] */
    size_t *sec_counts;
    size_t input_count;
};

static uint32_t piece_hash(const uint8_t *p, size_t n) {
    uint32_t h = 2166136261u;   /* FNV-1a */
    size_t i;

    for (i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 16777619u;
    }
    return h;
}

static elf_err_t ensure_state(elf_link_plan_t *plan) {
    struct elf_merge_state *st;
    size_t i;

    if (plan->merge != NULL) {
        return ELF_OK;
    }
    st = (struct elf_merge_state *)elf__calloc(1, sizeof(*st));
    if (st == NULL) {
        return ELF_ERR_OOM;
    }
    st->input_count = plan->input_count;
    st->secs = (struct merge_sec **)elf__calloc(plan->input_count + 1, sizeof(st->secs[0]));
    st->sec_counts = (size_t *)elf__calloc(plan->input_count + 1, sizeof(st->sec_counts[0]));
    plan->merge = st;
    if (st->secs == NULL || st->sec_counts == NULL) {
        return ELF_ERR_OOM;
    }
    for (i = 0; i < plan->input_count; ++i) {
        size_t n = plan->inputs[i].obj != NULL ? plan->inputs[i].obj->section_count : 0;
        size_t j;

        st->secs[i] = (struct merge_sec *)elf__calloc(n + 1, sizeof(st->secs[i][0]));
        if (st->secs[i] == NULL) {
            return ELF_ERR_OOM;
        }
        st->sec_counts[i] = n;
        for (j = 0; j < n; ++j) {
            st->secs[i][j].group = SIZE_MAX;
        }
    }
    return ELF_OK;
}

/* The group of output section @p out_name these are of, made if it is not there. */
static elf_err_t find_group(struct elf_merge_state *st, const char *out_name, uint64_t entsize, int strings,
                            uint64_t sec_align, size_t *out_index) {
    size_t i;
    struct merge_group *g;

    for (i = 0; i < st->group_count; ++i) {
        g = &st->groups[i];
        if (g->entsize == entsize && g->strings == strings && g->sec_align == sec_align &&
            strcmp(g->out_name, out_name) == 0) {
            *out_index = i;
            return ELF_OK;
        }
    }
    if (st->group_count == st->group_cap) {
        size_t cap = st->group_cap == 0 ? 8 : st->group_cap * 2;
        void *next = elf__reallocarray(st->groups, cap, sizeof(st->groups[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        st->groups = (struct merge_group *)next;
        st->group_cap = cap;
    }
    g = &st->groups[st->group_count];
    memset(g, 0, sizeof(*g));
    g->out_name = elf__strdup(out_name);
    g->nbuckets = 1024;
    g->buckets = (struct merge_entry **)elf__calloc(g->nbuckets, sizeof(g->buckets[0]));
    if (g->out_name == NULL || g->buckets == NULL) {
        free(g->out_name);
        free(g->buckets);
        return ELF_ERR_OOM;
    }
    g->entsize = entsize;
    g->strings = strings;
    g->sec_align = sec_align;
    g->align = sec_align;
    *out_index = st->group_count++;
    return ELF_OK;
}

static elf_err_t grow_buckets(struct merge_group *g) {
    size_t n = g->nbuckets * 2;
    struct merge_entry **b = (struct merge_entry **)elf__calloc(n, sizeof(b[0]));
    size_t i;

    if (b == NULL) {
        return ELF_ERR_OOM;
    }
    for (i = 0; i < g->entry_count; ++i) {
        struct merge_entry *e = g->entries[i];
        e->next = b[e->hash & (n - 1)];
        b[e->hash & (n - 1)] = e;
    }
    free(g->buckets);
    g->buckets = b;
    g->nbuckets = n;
    return ELF_OK;
}

/* The entry of @p g that holds these bytes, made if there is none; its alignment at least @p align. */
static elf_err_t add_piece(struct merge_group *g, const uint8_t *p, size_t len, uint64_t align,
                           struct merge_entry **out_entry) {
    uint32_t h = piece_hash(p, len);
    struct merge_entry *e;

    for (e = g->buckets[h & (g->nbuckets - 1)]; e != NULL; e = e->next) {
        if (e->hash == h && e->len == len && memcmp(e->data, p, len) == 0) {
            if (align > e->align) {
                e->align = align;
            }
            *out_entry = e;
            return ELF_OK;
        }
    }
    if (g->entry_count == g->entry_cap) {
        size_t cap = g->entry_cap == 0 ? 64 : g->entry_cap * 2;
        void *next = elf__reallocarray(g->entries, cap, sizeof(g->entries[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        g->entries = (struct merge_entry **)next;
        g->entry_cap = cap;
    }
    if (g->entry_count >= g->nbuckets && grow_buckets(g) != ELF_OK) {
        return ELF_ERR_OOM;
    }
    e = (struct merge_entry *)elf__calloc(1, sizeof(*e));
    if (e == NULL) {
        return ELF_ERR_OOM;
    }
    e->data = p;
    e->len = len;
    e->align = align;
    e->hash = h;
    e->next = g->buckets[h & (g->nbuckets - 1)];
    g->buckets[h & (g->nbuckets - 1)] = e;
    g->entries[g->entry_count++] = e;
    *out_entry = e;
    return ELF_OK;
}

/* Whether the @p entsize bytes at @p p are all zero: the end of a string. */
static int is_nul_char(const uint8_t *p, uint64_t entsize) {
    uint64_t k;

    for (k = 0; k < entsize; ++k) {
        if (p[k] != 0) {
            return 0;
        }
    }
    return 1;
}

int elf__merge_eligible(const struct elf_section *src) {
    uint64_t align;
    int strings;

    if (src == NULL || (src->flags & SHF_MERGE) == 0 || src->entsize == 0 || src->type != SHT_PROGBITS ||
        (src->flags & (SHF_COMPRESSED | SHF_LINK_ORDER | SHF_GROUP)) != 0 || src->data == NULL ||
        src->data_size == 0 || src->data_size % src->entsize != 0 || src->reloc_count != 0) {
        return 0;
    }
    align = src->addralign != 0 ? src->addralign : 1;
    strings = (src->flags & SHF_STRINGS) != 0;
    /* as bfd: entries that would not each fall on the section's alignment are not shared */
    if (src->entsize < align && (!strings || (src->entsize & (src->entsize - 1)) != 0)) {
        return 0;
    }
    if (strings && !is_nul_char(src->data + src->data_size - src->entsize, src->entsize)) {
        return 0;
    }
    return 1;
}

elf_err_t elf__merge_add(elf_link_plan_t *plan, size_t input, size_t sec, const char *out_name) {
    const struct elf_section *src = plan->inputs[input].obj->sections[sec];
    struct elf_merge_state *st;
    struct merge_group *g;
    struct merge_sec *ms;
    uint64_t align = src->addralign != 0 ? src->addralign : 1;
    uint64_t es = src->entsize;
    int strings = (src->flags & SHF_STRINGS) != 0;
    size_t gi;
    uint64_t p;
    elf_err_t err;

    err = ensure_state(plan);
    if (err != ELF_OK) {
        return err;
    }
    st = plan->merge;
    if (input >= st->input_count || sec >= st->sec_counts[input]) {
        return ELF_ERR_STATE;
    }
    err = find_group(st, out_name, es, strings, align, &gi);
    if (err != ELF_OK) {
        return err;
    }
    g = &st->groups[gi];
    ms = &st->secs[input][sec];
    for (p = 0; p < src->data_size;) {
        uint64_t len = es;
        uint64_t palign = align;
        struct merge_entry *e;
        struct merge_ref *refs;

        if (strings) {
            while (p + len <= src->data_size && !is_nul_char(src->data + p + len - es, es)) {
                len += es;
            }
            /* a string keeps the alignment its place gave it, as far as the section's (bfd) */
            if (p != 0) {
                uint64_t low = p & (~p + 1);
                if (low < palign) {
                    palign = low;
                }
            }
            if (palign < es) {
                palign = es;
            }
        }
        err = add_piece(g, src->data + p, (size_t)len, palign, &e);
        if (err != ELF_OK) {
            return err;
        }
        if ((ms->ref_count & (ms->ref_count + 1)) == 0) {      /* 0, 1, 3, 7, ...: grow */
            refs = (struct merge_ref *)elf__reallocarray(ms->refs, (ms->ref_count + 1) * 2, sizeof(*refs));
            if (refs == NULL) {
                return ELF_ERR_OOM;
            }
            ms->refs = refs;
        }
        ms->refs[ms->ref_count].in_off = p;
        ms->refs[ms->ref_count].len = len;
        ms->refs[ms->ref_count].entry = e;
        ms->ref_count++;
        p += len;
    }
    ms->group = gi;
    return ELF_OK;
}

/* qsort: by the bytes read from the end, a string that ends another before it */
static int rev_cmp(const void *a, const void *b) {
    const struct merge_entry *x = *(const struct merge_entry *const *)a;
    const struct merge_entry *y = *(const struct merge_entry *const *)b;
    size_t i = x->len;
    size_t j = y->len;

    while (i > 0 && j > 0) {
        --i;
        --j;
        if (x->data[i] != y->data[j]) {
            return x->data[i] < y->data[j] ? -1 : 1;
        }
    }
    if (x->len != y->len) {
        return x->len < y->len ? -1 : 1;
    }
    return 0;
}

/* Lay out one group's block: tail merging for strings, then each entry in the order first met. */
static elf_err_t lay_out_group(struct merge_group *g) {
    uint64_t cur = 0;
    size_t i;

    if (g->strings && g->entry_count > 1) {
        struct merge_entry **sorted = (struct merge_entry **)elf__calloc(g->entry_count, sizeof(sorted[0]));
        struct merge_entry *e;

        if (sorted == NULL) {
            return ELF_ERR_OOM;
        }
        memcpy(sorted, g->entries, g->entry_count * sizeof(sorted[0]));
        qsort(sorted, g->entry_count, sizeof(sorted[0]), rev_cmp);
        e = sorted[g->entry_count - 1];
        for (i = g->entry_count - 1; i-- > 0;) {
            struct merge_entry *c = sorted[i];
            uint64_t delta = e->len - c->len;

            if (c->len <= e->len && memcmp(e->data + delta, c->data, c->len) == 0 &&
                c->align <= e->align && delta % c->align == 0) {
                c->suffix_of = e;
            } else {
                e = c;
            }
        }
        free(sorted);
    }
    for (i = 0; i < g->entry_count; ++i) {
        struct merge_entry *e = g->entries[i];

        if (e->suffix_of != NULL) {
            continue;
        }
        /* the block is one allocation: it must fit in size_t at every step, or the
         * offsets would outrun a block cut to the low bits of its size */
        if (e->align - 1 > (uint64_t)SIZE_MAX - cur) {
            return ELF_ERR_OOM;
        }
        cur = (cur + e->align - 1) & ~(e->align - 1);
        if ((uint64_t)e->len > (uint64_t)SIZE_MAX - cur) {
            return ELF_ERR_OOM;
        }
        e->off = cur;
        cur += e->len;
        if (e->align > g->align) {
            g->align = e->align;
        }
    }
    g->blob = (uint8_t *)elf__calloc(cur != 0 ? (size_t)cur : 1, 1);
    if (g->blob == NULL) {
        return ELF_ERR_OOM;
    }
    g->blob_size = (size_t)cur;
    for (i = 0; i < g->entry_count; ++i) {
        struct merge_entry *e = g->entries[i];

        if (e->suffix_of != NULL) {
            e->off = e->suffix_of->off + (e->suffix_of->len - e->len);
        } else {
            memcpy(g->blob + e->off, e->data, e->len);
        }
    }
    return ELF_OK;
}

elf_err_t elf__merge_finish(elf_link_plan_t *plan) {
    size_t i;

    if (plan->merge == NULL) {
        return ELF_OK;
    }
    for (i = 0; i < plan->merge->group_count; ++i) {
        elf_err_t err = lay_out_group(&plan->merge->groups[i]);
        if (err != ELF_OK) {
            return err;
        }
    }
    return ELF_OK;
}

/* The merge record of section @p sec of input @p input, if it is merged. */
static struct merge_sec *merged_sec(const elf_link_plan_t *plan, size_t input, size_t sec) {
    const struct elf_merge_state *st = plan->merge;

    if (st == NULL || input >= st->input_count || sec >= st->sec_counts[input] ||
        st->secs[input][sec].group == SIZE_MAX) {
        return NULL;
    }
    return &st->secs[input][sec];
}

int elf__merge_block(elf_link_plan_t *plan, size_t input, size_t sec, const uint8_t **data, size_t *size,
                     uint64_t *align, uint64_t **base, int *placed) {
    struct merge_sec *ms = merged_sec(plan, input, sec);
    struct merge_group *g;

    if (ms == NULL) {
        return 0;
    }
    g = &plan->merge->groups[ms->group];
    *data = g->blob;
    *size = g->blob_size;
    *align = g->align;
    *base = &g->base;
    *placed = g->placed;
    g->placed = 1;
    return 1;
}

int elf__merge_offset(const elf_link_plan_t *plan, size_t input, size_t sec, uint64_t in_off, uint64_t *out_off) {
    struct merge_sec *ms = merged_sec(plan, input, sec);
    const struct merge_group *g;
    size_t lo, hi;

    if (ms == NULL || ms->ref_count == 0) {
        return 0;
    }
    g = &plan->merge->groups[ms->group];
    /* the entry it is in; just past the last, the end of the last */
    lo = 0;
    hi = ms->ref_count;
    while (hi - lo > 1) {
        size_t mid = lo + (hi - lo) / 2;
        if (ms->refs[mid].in_off <= in_off) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    *out_off = g->base + ms->refs[lo].entry->off + (in_off - ms->refs[lo].in_off);
    return 1;
}

/* ---- x86 GNU property notes, merged as bfd's _bfd_x86_elf_merge_gnu_properties() ---------------------- */

#define MERGE_MAX_PROPS 64

struct x86_prop {
    uint32_t type;
    uint32_t value;
};

/*
 * How a property of @p type is merged: 'a' AND (gone if an input lacks it); 'o' OR (an input that lacks it
 * changes nothing; gone if it comes out 0); 'm' OR_AND (ORed while every input has it, 0 or not; gone if
 * one lacks it); 0 another kind.
 */
static int x86_prop_kind(uint32_t type) {
    if ((type >= 0xc0000002u && type <= 0xc0007fffu) || (type >= 0xb0000000u && type <= 0xb0007fffu)) {
        return 'a';     /* GNU_PROPERTY_X86_UINT32_AND_LO..HI, GNU_PROPERTY_UINT32_AND_LO..HI */
    }
    if ((type >= 0xc0008000u && type <= 0xc000ffffu) || (type >= 0xb0008000u && type <= 0xb000ffffu)) {
        return 'o';     /* GNU_PROPERTY_X86_UINT32_OR_LO..HI, GNU_PROPERTY_UINT32_OR_LO..HI */
    }
    if (type >= 0xc0010000u && type <= 0xc0017fffu) {
        return 'm';     /* GNU_PROPERTY_X86_UINT32_OR_AND_LO..HI: the ISA and features used */
    }
    return 0;
}

/*
 * The 4-byte properties of the NT_GNU_PROPERTY_TYPE_0 notes of @p obj's
 * .note.gnu.property: how many (into @p props), or -1 if it has none.
 */
static int read_x86_props(const elfobj_t *obj, struct x86_prop *props) {
    const struct elf_section *sec = NULL;
    size_t pad = obj->cls == ELFOBJ_CLASS_64 ? 8 : 4;
    size_t off = 0;
    size_t i;
    int n = 0;

    for (i = 0; i < obj->section_count; ++i) {
        const struct elf_section *s = obj->sections[i];
        if (s != NULL && s->type == SHT_NOTE && s->name != NULL && strcmp(s->name, ".note.gnu.property") == 0) {
            sec = s;
            break;
        }
    }
    if (sec == NULL || sec->data == NULL) {
        return -1;
    }
    /*
     * Every size is tested against what is left (off <= data_size, p <= end) before it is added, so
     * a size near 4 GiB cannot wrap a 32-bit size_t and move the walk on by nothing, or back.
     */
    while (sec->data_size - off >= 12) {
        uint32_t namesz = elf__rd32(sec->data + off, obj->endian);
        uint32_t descsz = elf__rd32(sec->data + off + 4, obj->endian);
        uint32_t ntype = elf__rd32(sec->data + off + 8, obj->endian);
        size_t left = sec->data_size - off - 12;
        size_t desc, end, p;

        if (namesz > left || (((size_t)namesz + 3) & ~(size_t)3) > left) {
            break;
        }
        desc = off + 12 + (((size_t)namesz + 3) & ~(size_t)3);
        if (descsz > sec->data_size - desc) {
            break;
        }
        end = desc + descsz;
        if (ntype == 5 && namesz == 4 && memcmp(sec->data + off + 12, "GNU", 4) == 0) {
            for (p = desc; end - p >= 8;) {
                uint32_t ptype = elf__rd32(sec->data + p, obj->endian);
                uint32_t psz = elf__rd32(sec->data + p + 4, obj->endian);
                size_t step;

                if (psz > end - p - 8) {
                    break;      /* runs past the note: bfd calls it corrupt */
                }
                if (psz == 4) {
                    uint32_t v = elf__rd32(sec->data + p + 8, obj->endian);
                    int k;

                    /* one type twice in an object (GNU as adds a note of its own): ORed, as bfd reads it */
                    for (k = 0; k < n && props[k].type != ptype; ++k) {
                    }
                    if (k < n) {
                        props[k].value |= v;
                    } else if (n < MERGE_MAX_PROPS) {
                        props[n].type = ptype;
                        props[n].value = v;
                        n++;
                    }
                }
                step = ((size_t)psz + pad - 1) & ~(pad - 1);
                if (step > end - p - 8) {
                    break;      /* the last property, its padding cut off */
                }
                p += 8 + step;
            }
        }
        if ((((size_t)descsz + pad - 1) & ~(pad - 1)) > sec->data_size - desc) {
            break;
        }
        off = desc + (((size_t)descsz + pad - 1) & ~(pad - 1));
    }
    return n;
}

static int find_prop(const struct x86_prop *props, int n, uint32_t type) {
    int i;

    for (i = 0; i < n; ++i) {
        if (props[i].type == type) {
            return i;
        }
    }
    return -1;
}

static int prop_cmp(const void *a, const void *b) {
    uint32_t x = ((const struct x86_prop *)a)->type;
    uint32_t y = ((const struct x86_prop *)b)->type;

    return x < y ? -1 : x > y ? 1 : 0;
}

elf_err_t elf__x86_property_note(const elf_link_plan_t *plan, uint8_t **data, size_t *size, uint64_t *align) {
    struct x86_prop acc[MERGE_MAX_PROPS];
    struct x86_prop in[MERGE_MAX_PROPS];
    int nacc = -1;
    size_t base = 0;
    size_t i;
    int k;
    size_t pad, descsz, off;
    const elfobj_t *first = NULL;
    uint8_t *buf;

    *data = NULL;
    *size = 0;
    /* from the first input that has a note, every other merged into it, notes or none */
    for (i = 0; i < plan->input_count && nacc < 0; ++i) {
        if (plan->inputs[i].obj != NULL && (nacc = read_x86_props(plan->inputs[i].obj, acc)) >= 0) {
            base = i;
            first = plan->inputs[i].obj;
        }
    }
    if (nacc < 0) {
        return ELF_OK;
    }
    for (i = 0; i < plan->input_count; ++i) {
        int nin;

        /* (an input the linker made itself, its name in <>, as --build-id's note, is none of the program's) */
        if (i == base || plan->inputs[i].obj == NULL ||
            (plan->inputs[i].name != NULL && plan->inputs[i].name[0] == '<')) {
            continue;
        }
        nin = read_x86_props(plan->inputs[i].obj, in);
        if (nin < 0) {
            nin = 0;
        }
        /* what the merged has: kept, combined or gone */
        for (k = 0; k < nacc;) {
            int j = find_prop(in, nin, acc[k].type);
            int kind = x86_prop_kind(acc[k].type);
            int keep = 1;

            if (kind == 'a') {
                keep = j >= 0 && (acc[k].value &= in[j].value) != 0;
            } else if (kind == 'm') {
                keep = j >= 0;
                if (keep) {
                    acc[k].value |= in[j].value;
                }
            } else if (kind == 'o') {
                if (j >= 0) {
                    acc[k].value |= in[j].value;
                }
                keep = acc[k].value != 0;
            } else {
                keep = j >= 0;
            }
            if (keep) {
                k++;
            } else {
                acc[k] = acc[--nacc];
            }
        }
        /* what only the input has: an OR property comes in */
        for (k = 0; k < nin; ++k) {
            if (x86_prop_kind(in[k].type) == 'o' && in[k].value != 0 && find_prop(acc, nacc, in[k].type) < 0 &&
                nacc < MERGE_MAX_PROPS) {
                acc[nacc++] = in[k];
            }
        }
    }
    if (nacc == 0) {
        return ELF_OK;
    }
    qsort(acc, (size_t)nacc, sizeof(acc[0]), prop_cmp);
    pad = first->cls == ELFOBJ_CLASS_64 ? 8 : 4;
    descsz = (size_t)nacc * (8 + pad);
    buf = (uint8_t *)elf__calloc(16 + descsz, 1);
    if (buf == NULL) {
        return ELF_ERR_OOM;
    }
    elf__wr32(buf, first->endian, 4);
    elf__wr32(buf + 4, first->endian, (uint32_t)descsz);
    elf__wr32(buf + 8, first->endian, 5);   /* NT_GNU_PROPERTY_TYPE_0 */
    memcpy(buf + 12, "GNU", 4);
    off = 16;
    for (k = 0; k < nacc; ++k) {
        elf__wr32(buf + off, first->endian, acc[k].type);
        elf__wr32(buf + off + 4, first->endian, 4);
        elf__wr32(buf + off + 8, first->endian, acc[k].value);
        off += 8 + pad;
    }
    *data = buf;
    *size = 16 + descsz;
    /*
     * Its alignment: the class's, or the strictest of the input notes it
     * stands for where that is more, as GNU ld's output section is aligned
     * (objcopy's x32 objects keep the 64-bit notes' 8).
     */
    *align = pad;
    for (i = 0; i < plan->input_count; ++i) {
        const elfobj_t *o = plan->inputs[i].obj;
        size_t j;

        for (j = 0; o != NULL && j < o->section_count; ++j) {
            const struct elf_section *s = o->sections[j];

            if (s != NULL && s->type == SHT_NOTE && s->name != NULL &&
                strcmp(s->name, ".note.gnu.property") == 0 && s->addralign > *align) {
                *align = s->addralign;
            }
        }
    }
    return ELF_OK;
}

void elf__merge_free(elf_link_plan_t *plan) {
    struct elf_merge_state *st = plan->merge;
    size_t i, j;

    free(plan->x86_note);
    plan->x86_note = NULL;
    plan->x86_note_size = 0;
    plan->x86_note_placed = 0;
    if (st == NULL) {
        return;
    }
    for (i = 0; i < st->group_count; ++i) {
        struct merge_group *g = &st->groups[i];
        for (j = 0; j < g->entry_count; ++j) {
            free(g->entries[j]);
        }
        free(g->entries);
        free(g->buckets);
        free(g->blob);
        free(g->out_name);
    }
    free(st->groups);
    for (i = 0; i < st->input_count; ++i) {
        if (st->secs[i] != NULL) {
            for (j = 0; j < st->sec_counts[i]; ++j) {
                free(st->secs[i][j].refs);
            }
            free(st->secs[i]);
        }
    }
    free(st->secs);
    free(st->sec_counts);
    free(st);
    plan->merge = NULL;
}
