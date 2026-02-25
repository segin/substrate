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
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint32_t info;
    uint64_t addralign;
    uint64_t entsize;
    uint32_t group_index;
    uint8_t comdat;
    uint32_t note_type;
    char *note_name;
    uint8_t *data;
    size_t data_size;
    uint8_t owns_data;
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
    uint16_t shndx;
    uint16_t ver_index;
    size_t index;
};

struct elfobj {
    uint8_t *image;
    size_t image_size;
    uint8_t owns_image;
    uint8_t readonly;
    uint8_t finalized;
    uint8_t dirty;

    elfobj_class_t cls;
    elfobj_endian_t endian;
    uint16_t type;
    uint16_t machine;
    uint64_t entry;
    uint32_t flags;

    struct elf_section **sections;
    size_t section_count;
    size_t section_cap;

    struct elf_symbol **symbols;
    size_t symbol_count;
    size_t symbol_cap;

    struct elf_reloc **relocs;
    size_t reloc_count;
    size_t reloc_cap;

    uint16_t shstrndx;
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

    elf_err_t last_err;
    elf_diag_t diag;
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

struct elf_link_plan {
    struct elf_link_input *inputs;
    size_t input_count;
    size_t input_cap;

    elf_link_section_merge_hook_t section_merge_hook;
    void *section_merge_user;
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
};

elfobj_t *elf__alloc_obj(void);
elf_err_t elf__append_diag(elfobj_t *obj, const char *msg);
elf_err_t elf__append_diag_fmt(elfobj_t *obj, const char *prefix, uint64_t value);
void elf__set_err(elfobj_t *obj, elf_err_t err, const char *msg);
void *elf__calloc(size_t n, size_t sz);
void *elf__reallocarray(void *ptr, size_t n, size_t sz);
char *elf__strdup(const char *s);
int elf__bounds_ok(size_t off, size_t len, size_t total);
int elf__u64_add(uint64_t a, uint64_t b, uint64_t *out);
int elf__u64_mul(uint64_t a, uint64_t b, uint64_t *out);
uint16_t elf__rd16(const uint8_t *p, elfobj_endian_t e);
uint32_t elf__rd32(const uint8_t *p, elfobj_endian_t e);
uint64_t elf__rd64(const uint8_t *p, elfobj_endian_t e);
void elf__wr16(uint8_t *p, elfobj_endian_t e, uint16_t v);
void elf__wr32(uint8_t *p, elfobj_endian_t e, uint32_t v);
void elf__wr64(uint8_t *p, elfobj_endian_t e, uint64_t v);

elf_err_t elf__push_section(elfobj_t *obj, struct elf_section *sec);
elf_err_t elf__push_symbol(elfobj_t *obj, struct elf_symbol *sym);
elf_err_t elf__push_reloc(elfobj_t *obj, struct elf_reloc *rel);
elf_err_t elf__section_push_reloc(struct elf_section *section, struct elf_reloc *rel);
elf_err_t elf__push_phdr(elfobj_t *obj, const struct elf_phdr *phdr);
elf_err_t elf__push_segment(elfobj_t *obj, struct elf_segment *seg);

elf_err_t elf__strtab_init(elf_strtab_t *tab);
void elf__strtab_free(elf_strtab_t *tab);
uint32_t elf__strtab_add(elf_strtab_t *tab, const char *s);

elf_err_t elf__layout(elfobj_t *obj);
elf_err_t elf__write_to_buffer(elfobj_t *obj, uint8_t **out_buf, size_t *out_sz);

#endif
