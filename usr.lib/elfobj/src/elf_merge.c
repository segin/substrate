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
        cur = (cur + e->align - 1) & ~(e->align - 1);
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

void elf__merge_free(elf_link_plan_t *plan) {
    struct elf_merge_state *st = plan->merge;
    size_t i, j;

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
