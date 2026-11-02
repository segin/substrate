#ifndef _ELFOBJ_PRIVATE_H_
#define _ELFOBJ_PRIVATE_H_

#include <elfobj.h>
#include <exec/formats/elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EI_DATA
#define EI_DATA 5
#endif
#ifndef EI_VERSION
#define EI_VERSION 6
#endif
#ifndef EI_ABIVERSION
#define EI_ABIVERSION 8
#endif
#ifndef ELFDATA2LSB
#define ELFDATA2LSB 1
#endif
#ifndef ELFDATA2MSB
#define ELFDATA2MSB 2
#endif
#ifndef EV_CURRENT
#define EV_CURRENT 1
#endif
#ifndef ET_NONE
#define ET_NONE 0
#define ET_REL 1
#define ET_EXEC 2
#define ET_DYN 3
#define ET_CORE 4
#endif
#ifndef EM_386
#define EM_386 3
#endif
#ifndef SHN_UNDEF
#define SHN_UNDEF 0
#endif
#ifndef STB_LOCAL
#define STB_LOCAL 0
#define STB_GLOBAL 1
#define STB_WEAK 2
#endif
#ifndef STT_NOTYPE
#define STT_NOTYPE 0
#define STT_OBJECT 1
#define STT_FUNC 2
#define STT_SECTION 3
#define STT_FILE 4
#define STT_TLS 6
#endif

typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} elf_diag_t;

struct elf_addr_index_ent {
    uint64_t addr;
    uint64_t end;
    struct elf_section *sec;
};

typedef struct {
    elf_diag_level_t level;
    elf_err_t code;
    uint64_t index;
    char *message;
} elf_diag_item_t;

struct elf_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
};

typedef struct {
    char *data;
    size_t size;
    size_t cap;
} elf_strtab_t;

struct elf_reloc {
    struct elf_section *section;
    uint64_t offset;
    struct elf_symbol *symbol;
    uint32_t type;
    int64_t addend;
    uint8_t has_addend;
};

struct elf_section {
    struct elfobj *obj;
    char *name;
    uint32_t sh_name;
    uint32_t type;
    uint64_t flags;
    uint64_t addr;
    uint64_t lma;       /* where it is loaded, when has_lma: a linker script's AT() */
    int has_lma;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint64_t entsize;
    uint32_t group_index;
    uint8_t comdat;
    char *group_signature;
    struct elf_section *link_order; /* SHF_LINK_ORDER: the section it is ordered by, made sh_link when written */
    struct elf_symbol *section_symbol;  /* a link's: its STT_SECTION symbol, once a relocation names it */
    uint32_t note_type;
    char *note_name;
    uint8_t *data;
    size_t data_size;
    size_t data_cap;            /* allocated, where data was grown by the
                                 * merge; 0 where it is exactly data_size */
    uint8_t owns_data;
    uint8_t placeholder;        /* one of the empty sections numbered as the reserved indices are */
    uint8_t has_compression_hint;
    uint32_t compression_type;
    uint64_t compression_size;
    uint64_t compression_addralign;
    size_t index;
    struct elf_reloc **relocs;
    size_t reloc_count;
    size_t reloc_cap;
};

struct elf_segment {
    struct elfobj *obj;
    uint32_t type;
    uint32_t flags;
    uint64_t align;
    size_t *section_indices;
    size_t section_count;
    size_t section_cap;
    char *interp_path;
};

struct elf_symbol {
    struct elfobj *obj;
    char *name;
    uint64_t value;
    uint64_t size;
    uint8_t bind;
    uint8_t type;
    uint8_t other;
    uint32_t shndx;             /* past SHN_HIRESERVE for a section that is (ELF_SHNDX_RESERVED) */
    uint16_t ver_index;
    char *version_name;
    uint8_t version_default;
    uint8_t in_merge_section;   /* a link took it from an SHF_MERGE input section */
    size_t index;
    struct elf_symbol *name_next;       /* the next in its bucket of the name index */
    uint32_t name_hash;
};

struct elfobj {
    uint8_t *image;
    size_t image_size;
    uint8_t owns_image;
    uint8_t mmapped_image;
    uint8_t readonly;
    uint8_t finalized;
    uint8_t no_load_headers;   /* the ELF and program headers are in no PT_LOAD (elf_set_load_headers) */
    uint8_t force_load_headers; /* the first PT_LOAD takes them, from a page lower if need be (2 there) */
    uint64_t max_page_size;    /* a PT_LOAD's alignment, and its offset's modulus (elf_set_max_page_size); 0: 4 KiB */
    uint8_t dirty;
    uint8_t lazy_parse;
    uint8_t symrel_loaded;

    elfobj_class_t cls;
    elfobj_endian_t endian;
    uint16_t type;
    uint16_t machine;
    uint8_t osabi;
    uint8_t abiversion;
    uint64_t entry;
    uint32_t flags;

    struct elf_section **sections;
    size_t section_count;
    size_t section_cap;

    struct elf_symbol **symbols;
    size_t symbol_count;
    size_t symbol_cap;
    /* The symbols by name: made when a name is first looked up, added to
     * as symbols are, and dropped when symbols are taken away.  It holds
     * symbols[0 .. name_indexed). */
    struct elf_symbol **name_buckets;
    size_t name_nbuckets;               /* a power of two, or 0 for none */
    size_t name_indexed;

    struct elf_reloc **relocs;
    size_t reloc_count;
    size_t reloc_cap;

    uint32_t shstrndx;
    uint16_t phnum;
    struct elf_phdr *phdrs;
    size_t phdr_count;
    size_t phdr_cap;
    uint8_t has_notes;
    uint8_t has_dynamic;
    uint8_t has_versioning;

    struct elf_segment **segments;
    size_t segment_count;
    size_t segment_cap;

    elf_reloc_hooks_t reloc_hooks;
    void *reloc_hook_user;

    elf_validate_mode_t validate_mode;
    size_t validate_max_errors;

    /* Lazily-built, addr-sorted index of SHF_ALLOC sections, used to resolve
     * runtime relocation targets in O(log n) instead of an O(n) scan per
     * relocation (which is O(relocs * sections) overall - a parse-time DoS on
     * an object with many sections and many runtime relocations). */
    struct elf_addr_index_ent *runtime_addr_index;
    size_t runtime_addr_index_count;
    uint8_t runtime_addr_index_built;

    elf_err_t last_err;
    elf_diag_t diag;
    elf_diag_item_t *diag_items;
    size_t diag_item_count;
    size_t diag_item_cap;
};

struct elf_link_input {
    elfobj_t *obj;
    char *name;
};

struct elf_link_map_entry_rec {
    char *symbol_name;
    char *section_name;
    char *input_name;
    uint64_t value;
    size_t input_index;
};

/* The sections of one rank in one output section: [start, end) of it. */
struct elf_link_rank_extent {
    char *output_name;
    int rank;
    uint64_t start;
    uint64_t end;
};

struct elf_link_plan {
    struct elf_link_input *inputs;
    size_t input_count;
    size_t input_cap;

    elf_link_section_merge_hook_t section_merge_hook;
    void *section_merge_user;
    elf_link_section_name_hook_t section_name_hook;
    void *section_name_user;
    elf_link_section_rank_hook_t rank_hook;
    elf_link_section_gap_hook_t gap_hook;
    void *rank_user;
    struct elf_link_rank_extent *extents; /* with rank hooks: where each rank's sections went */
    size_t extent_count;
    size_t extent_cap;
    elf_link_archive_hook_t archive_hook;
    void *archive_user;
    elf_link_gc_hook_t gc_hook;
    void *gc_user;
    elf_link_incremental_hook_t incremental_hook;
    void *incremental_user;
    elf_link_version_hook_t version_hook;
    void *version_user;

    struct elf_link_map_entry_rec *map_entries;
    size_t map_count;
    size_t map_cap;

    int merge_sections;                 /* SHF_MERGE sections are merged (elf_link_plan_set_merge_sections) */
    struct elf_merge_state *merge;      /* for one link: elf_merge.c */
    int x86_note_merged;                /* an x86 link: .note.gnu.property is the inputs' merged */
    uint8_t *x86_note;                  /* and this is it, NULL if no property is left */
    size_t x86_note_size;
    uint64_t x86_note_align;
    int x86_note_placed;
};

/**
 * Whether the first of @p obj's symbols is the null symbol (nameless, local, of no type, value, size or
 * section), which the symbol table's entry 0 is; the writer makes one up when it is not.
 */
int elf__first_symbol_is_null(const elfobj_t *obj);

/**
 * The first global or weak symbol of @p obj named @p name, passing over local ones: a link's output has a local
 * of each input's own, and an input's `static int f` is not the global f another input defines or refers to.
 *
 * @return the symbol, or NULL if there is none
 */
struct elf_symbol *elf__find_global_symbol(elfobj_t *obj, const char *name);

/* elf_merge.c: SHF_MERGE sections merged, as GNU ld merges them */
/** Whether input section @p src can be merged: SHF_MERGE, whole entries, no relocations of its own. */
int elf__merge_eligible(const struct elf_section *src);
/** Take section @p sec of input @p input, going to output section @p out_name, into its merge group. */
elf_err_t elf__merge_add(elf_link_plan_t *plan, size_t input, size_t sec, const char *out_name);
/** Lay out every merge group's block, once every section is in. */
elf_err_t elf__merge_finish(elf_link_plan_t *plan);
/**
 * If section @p sec of input @p input is merged: 1, with its group's block (@p data, @p size, @p align),
 * where to record the block's place (@p base), and whether it was placed already (@p placed), which it is
 * from then on.  Otherwise 0.
 */
int elf__merge_block(elf_link_plan_t *plan, size_t input, size_t sec, const uint8_t **data, size_t *size,
                     uint64_t *align, uint64_t **base, int *placed);
/** If section @p sec of input @p input is merged: 1, with *@p out_off where its offset @p in_off went. */
int elf__merge_offset(const elf_link_plan_t *plan, size_t input, size_t sec, uint64_t in_off,
                      uint64_t *out_off);
/**
 * The .note.gnu.property of an x86 link: the inputs' properties merged as GNU ld merges them (an AND
 * property gone if one input lacks it, an OR one ORed), in one note.  *@p data is NULL if none is left;
 * *@p align is its section's alignment: the class's, or the strictest input note's.
 */
elf_err_t elf__x86_property_note(const elf_link_plan_t *plan, uint8_t **data, size_t *size, uint64_t *align);
/** Free what the merge of one link kept. */
void elf__merge_free(elf_link_plan_t *plan);

elfobj_t *elf__alloc_obj(void);
void elf__diag_clear(elfobj_t *obj);
elf_err_t elf__diag_append(elfobj_t *obj, elf_diag_level_t level, elf_err_t code,
                           uint64_t index, const char *msg);
elf_err_t elf__append_diag(elfobj_t *obj, const char *msg);
elf_err_t elf__append_diag_fmt(elfobj_t *obj, const char *prefix, uint64_t value);
void elf__set_err(elfobj_t *obj, elf_err_t err, const char *msg);
void *elf__calloc(size_t n, size_t sz);
void *elf__reallocarray(void *ptr, size_t n, size_t sz);
char *elf__strdup(const char *s);
/* elf_inflate.c: a zlib stream into exactly out_len bytes; 0 or -1. */
int elf__zlib_inflate(const uint8_t *in, size_t in_len, uint8_t *out,
                      size_t out_len);
#define ELF__COMPRESS_ZLIB  1U          /* Elf_Chdr.ch_type */
int elf__bounds_ok(size_t off, size_t len, size_t total);
int elf__bounds_ok_u64(uint64_t off, uint64_t len, size_t total);
int elf__u64_add(uint64_t a, uint64_t b, uint64_t *out);
int elf__u64_mul(uint64_t a, uint64_t b, uint64_t *out);
elf_err_t elf__write_file_atomic(const char *path, const void *buf, size_t size);
uint16_t elf__rd16(const uint8_t *p, elfobj_endian_t e);
uint32_t elf__rd32(const uint8_t *p, elfobj_endian_t e);
uint64_t elf__rd64(const uint8_t *p, elfobj_endian_t e);
void elf__wr16(uint8_t *p, elfobj_endian_t e, uint16_t v);
void elf__wr32(uint8_t *p, elfobj_endian_t e, uint32_t v);
void elf__wr64(uint8_t *p, elfobj_endian_t e, uint64_t v);

elf_err_t elf__push_section(elfobj_t *obj, struct elf_section *sec);
elf_err_t elf__push_symbol(elfobj_t *obj, struct elf_symbol *sym);
void elf__symbol_index_drop(elfobj_t *obj);
elf_err_t elf__push_reloc(elfobj_t *obj, struct elf_reloc *rel);
elf_err_t elf__section_push_reloc(struct elf_section *section, struct elf_reloc *rel);
elf_err_t elf__push_phdr(elfobj_t *obj, const struct elf_phdr *phdr);
elf_err_t elf__push_segment(elfobj_t *obj, struct elf_segment *seg);

elf_err_t elf__strtab_init(elf_strtab_t *tab);
void elf__strtab_free(elf_strtab_t *tab);
uint32_t elf__strtab_add(elf_strtab_t *tab, const char *s);

elf_err_t elf__layout(elfobj_t *obj);
elf_err_t elf__write_to_buffer(elfobj_t *obj, uint8_t **out_buf, size_t *out_sz);
elf_err_t elf__ensure_symbols_relocs(elfobj_t *obj);

#endif
