#ifndef _ELFOBJ_H_
#define _ELFOBJ_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct elfobj elfobj_t;
typedef struct elf_section elf_section_t;
typedef struct elf_symbol elf_symbol_t;
typedef struct elf_reloc elf_reloc_t;
typedef struct elf_segment elf_segment_t;
typedef struct elf_link_plan elf_link_plan_t;

#define ELFOBJ_API_VERSION 1

typedef enum {
    ELF_OK = 0,
    ELF_ERR_IO,
    ELF_ERR_FORMAT,
    ELF_ERR_RELOC,
    ELF_ERR_OOM,
    ELF_ERR_BOUNDS,
    ELF_ERR_UNSUPPORTED,
    ELF_ERR_STATE,
    ELF_ERR_NOTFOUND
} elf_err_t;

typedef enum {
    ELFOBJ_CLASS_NONE = 0,
    ELFOBJ_CLASS_32 = 1,
    ELFOBJ_CLASS_64 = 2
} elfobj_class_t;

typedef enum {
    ELFOBJ_ENDIAN_NONE = 0,
    ELFOBJ_ENDIAN_LE = 1,
    ELFOBJ_ENDIAN_BE = 2
} elfobj_endian_t;

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
#ifndef EM_X86_64
#define EM_X86_64 62
#endif

#ifndef SHT_NULL
#define SHT_NULL 0
#define SHT_PROGBITS 1
#define SHT_SYMTAB 2
#define SHT_STRTAB 3
#define SHT_RELA 4
#define SHT_DYNAMIC 6
#define SHT_NOTE 7
#define SHT_NOBITS 8
#define SHT_REL 9
#define SHT_DYNSYM 11
#endif

#ifndef SHF_WRITE
#define SHF_WRITE 0x1
#define SHF_ALLOC 0x2
#define SHF_EXECINSTR 0x4
#define SHF_MERGE 0x10
#define SHF_STRINGS 0x20
#define SHF_GROUP 0x200
#define SHF_TLS 0x400
#endif
#ifndef SHF_COMPRESSED
#define SHF_COMPRESSED 0x800
#endif

#define ELFOBJ_OPEN_NOCOPY 0x1u
#define ELFOBJ_OPEN_USE_MMAP 0x2u
#define ELFOBJ_OPEN_LAZY_PARSE 0x4u

#ifndef PT_NULL
#define PT_NULL 0
#define PT_LOAD 1
#define PT_DYNAMIC 2
#define PT_INTERP 3
#define PT_NOTE 4
#define PT_PHDR 6
#define PT_TLS 7
#endif

#ifndef DT_NULL
#define DT_NULL 0
#define DT_NEEDED 1
#define DT_STRTAB 5
#define DT_SYMTAB 6
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

#ifndef SHN_UNDEF
#define SHN_UNDEF 0
#define SHN_ABS 0xfff1
#define SHN_COMMON 0xfff2
#endif

#ifndef R_386_NONE
#define R_386_NONE 0
#define R_386_32 1
#define R_386_PC32 2
#define R_386_GOT32 3
#define R_386_PLT32 4
#define R_386_RELATIVE 8
#define R_386_GOTOFF 9
#define R_386_GOTPC 10
#define R_386_TLS_TPOFF 14
#define R_386_TLS_IE 15
#define R_386_TLS_GOTIE 16
#define R_386_TLS_LE 17
#define R_386_TLS_GD 18
#define R_386_TLS_LDM 19
#define R_386_TLS_LDO_32 32
#endif

#ifndef R_X86_64_NONE
#define R_X86_64_NONE 0
#define R_X86_64_64 1
#define R_X86_64_PC32 2
#define R_X86_64_GOT32 3
#define R_X86_64_PLT32 4
#define R_X86_64_GOTPCREL 9
#define R_X86_64_32 10
#define R_X86_64_32S 11
#define R_X86_64_TLSGD 19
#define R_X86_64_GOTTPOFF 22
#define R_X86_64_TPOFF32 23
#endif

typedef struct {
    uint32_t machine;
    uint8_t use_rela;
    uint8_t reserved[3];
} elfobj_reloc_ctx_t;

struct elf_reloc_backend {
    uint32_t machine;
    int (*apply_reloc)(const elfobj_reloc_ctx_t *ctx,
                       uint32_t type,
                       uint64_t place,
                       uint64_t sym_value,
                       int64_t addend,
                       uint64_t *out_value);
    int (*reloc_size)(uint32_t type);
    int (*is_pc_relative)(uint32_t type);
};

typedef struct {
    int (*before_apply)(const elf_reloc_t *reloc, void *user);
    void (*after_apply)(const elf_reloc_t *reloc, uint64_t relocated_value, void *user);
    void (*incremental_note)(const char *key, uint64_t value, void *user);
} elf_reloc_hooks_t;

typedef enum {
    ELF_LINK_MERGE_APPEND = 0,
    ELF_LINK_MERGE_REPLACE = 1,
    ELF_LINK_MERGE_SKIP = 2
} elf_link_merge_action_t;

typedef elf_link_merge_action_t (*elf_link_section_merge_hook_t)(
    const char *section_name, const elf_section_t *existing, const elf_section_t *incoming, void *user);
typedef int (*elf_link_archive_hook_t)(const char *archive_path, const char *member_name, void *user);
typedef int (*elf_link_gc_hook_t)(const elf_section_t *section, void *user);
typedef void (*elf_link_incremental_hook_t)(const char *key, const char *value, void *user);
typedef int (*elf_link_version_hook_t)(const char *symbol_name, const char *version_name, void *user);

typedef struct {
    const char *symbol_name;
    const char *section_name;
    const char *input_name;
    uint64_t value;
    size_t input_index;
} elf_link_map_entry_t;

typedef enum {
    ELF_VALIDATE_PERMISSIVE = 0,
    ELF_VALIDATE_STRICT = 1
} elf_validate_mode_t;

typedef enum {
    ELF_DIAG_INFO = 0,
    ELF_DIAG_WARNING = 1,
    ELF_DIAG_ERROR = 2
} elf_diag_level_t;

typedef struct {
    elf_diag_level_t level;
    elf_err_t code;
    uint64_t index;
    const char *message;
} elf_diag_entry_t;

typedef struct {
    elf_validate_mode_t mode;
    size_t max_errors;
} elf_validate_options_t;

elf_err_t elf_open(const char *path, elfobj_t **out);
elf_err_t elf_open_memory(const void *buf, size_t size, elfobj_t **out);
elf_err_t elf_open_with_options(const char *path, uint32_t flags, elfobj_t **out);
elf_err_t elf_open_memory_with_options(const void *buf, size_t size, uint32_t flags,
                                       elfobj_t **out);
elf_err_t elf_open_memory_nocopy(const void *buf, size_t size, elfobj_t **out);
elf_err_t elf_write_file(elfobj_t *obj, const char *path);
void elf_close(elfobj_t *obj);
int elf_uses_mmap(const elfobj_t *obj);
int elf_is_lazy_parse_enabled(const elfobj_t *obj);

elf_section_t *elf_add_section(elfobj_t *obj, const char *name, uint32_t type, uint64_t flags);
elf_err_t elf_section_set_data(elf_section_t *section, const void *data, size_t size);
elf_err_t elf_section_set_align(elf_section_t *section, uint64_t align);
elf_err_t elf_section_set_type(elf_section_t *section, uint32_t type);
elf_err_t elf_section_set_flags(elf_section_t *section, uint64_t flags);
elf_err_t elf_section_set_group(elf_section_t *section, uint32_t group, int comdat);
elf_err_t elf_section_set_merge(elf_section_t *section, uint64_t entsize, int strings);
elf_err_t elf_section_set_tls(elf_section_t *section, int enable);
elf_err_t elf_section_set_note_info(elf_section_t *section, uint32_t note_type, const char *note_name);
elf_err_t elf_remove_section(elfobj_t *obj, elf_section_t *section);
elf_err_t elf_reorder_section(elfobj_t *obj, elf_section_t *section, size_t new_index);
elf_section_t *elf_find_section(elfobj_t *obj, const char *name);

elf_symbol_t *elf_add_symbol(elfobj_t *obj, const char *name, uint64_t value,
                              uint64_t size, uint8_t bind, uint8_t type);
elf_err_t elf_symbol_define(elf_symbol_t *symbol, elf_section_t *section, uint64_t value);
elf_symbol_t *elf_find_symbol(elfobj_t *obj, const char *name);
elf_symbol_t *elf_symbol_at(elfobj_t *obj, size_t index);
elf_err_t elf_symbol_set_binding(elf_symbol_t *symbol, uint8_t bind);
elf_err_t elf_symbol_set_type(elf_symbol_t *symbol, uint8_t type);
elf_err_t elf_symbol_set_visibility(elf_symbol_t *symbol, uint8_t visibility);
elf_err_t elf_symbol_set_version(elf_symbol_t *symbol, uint16_t version_index);
uint16_t elf_symbol_version(const elf_symbol_t *symbol);
elf_err_t elf_symbol_set_shndx(elf_symbol_t *symbol, uint16_t shndx);
int elf_symbol_is_duplicate_global(const elfobj_t *obj, const char *name, uint8_t bind);
elf_err_t elf_symbols_sort_deterministic(elfobj_t *obj, size_t *first_global_out);
elf_symbol_t *elf_symbol_lookup_sysv(elfobj_t *obj, const char *name);
elf_symbol_t *elf_symbol_lookup_gnu(elfobj_t *obj, const char *name);

elf_err_t elf_add_relocation(elf_section_t *section, uint64_t offset, elf_symbol_t *symbol,
                             uint32_t type, int64_t addend);
size_t elf_section_reloc_count(const elf_section_t *section);
elf_reloc_t *elf_section_reloc_at(elf_section_t *section, size_t index);
elf_reloc_t *elf_reloc_at(elfobj_t *obj, size_t index);
uint64_t elf_reloc_offset(const elf_reloc_t *reloc);
uint32_t elf_reloc_type(const elf_reloc_t *reloc);
int64_t elf_reloc_addend(const elf_reloc_t *reloc);
int elf_reloc_has_addend(const elf_reloc_t *reloc);
elf_symbol_t *elf_reloc_symbol(const elf_reloc_t *reloc);
elf_section_t *elf_reloc_section(const elf_reloc_t *reloc);
elf_err_t elf_set_reloc_hooks(elfobj_t *obj, const elf_reloc_hooks_t *hooks, void *user);
elf_err_t elf_apply_relocation(const elf_reloc_t *reloc, uint64_t place, uint64_t sym_value,
                               uint64_t *out_value);
elf_err_t elf_apply_relocation_value(const elfobj_t *obj, uint32_t type, uint64_t place,
                                     uint64_t sym_value, int64_t addend, uint64_t *out_value);
int elf_reloc_size_for_machine(uint16_t machine, uint32_t type);
int elf_reloc_is_pc_relative_for_machine(uint16_t machine, uint32_t type);
int elf_reloc_is_tls_for_machine(uint16_t machine, uint32_t type);

elf_err_t elf_link(elfobj_t **inputs, size_t count, elfobj_t **output);
elf_link_plan_t *elf_link_plan_create(void);
void elf_link_plan_destroy(elf_link_plan_t *plan);
elf_err_t elf_link_plan_add_input(elf_link_plan_t *plan, elfobj_t *obj, const char *name);
size_t elf_link_plan_input_count(const elf_link_plan_t *plan);
elf_err_t elf_link_plan_set_section_merge_hook(elf_link_plan_t *plan,
                                               elf_link_section_merge_hook_t hook,
                                               void *user);
elf_err_t elf_link_plan_set_archive_hook(elf_link_plan_t *plan, elf_link_archive_hook_t hook,
                                         void *user);
elf_err_t elf_link_plan_set_gc_hook(elf_link_plan_t *plan, elf_link_gc_hook_t hook, void *user);
elf_err_t elf_link_plan_set_incremental_hook(elf_link_plan_t *plan,
                                              elf_link_incremental_hook_t hook, void *user);
elf_err_t elf_link_plan_set_version_hook(elf_link_plan_t *plan, elf_link_version_hook_t hook,
                                         void *user);
elf_err_t elf_link_plan_consider_archive_member(elf_link_plan_t *plan, const char *archive_path,
                                                 const char *member_name, int *should_extract_out);
elf_err_t elf_link_plan_note_incremental(elf_link_plan_t *plan, const char *key,
                                         const char *value);
elf_err_t elf_link_plan_link(elf_link_plan_t *plan, elfobj_t **output);
size_t elf_link_plan_map_count(const elf_link_plan_t *plan);
int elf_link_plan_map_entry(const elf_link_plan_t *plan, size_t index,
                            elf_link_map_entry_t *out_entry);
elf_err_t elf_link_load_objects(const char **paths, size_t count, elfobj_t ***out_objs,
                                size_t *out_count);
void elf_link_unload_objects(elfobj_t **objs, size_t count);
elf_symbol_t *elf_link_resolve_symbol(elfobj_t **inputs, size_t count, const char *name,
                                      size_t *input_index_out);
elf_section_t *elf_link_add_got_section(elfobj_t *obj, size_t entries);
elf_section_t *elf_link_add_plt_section(elfobj_t *obj, size_t entries);
elf_err_t elf_link_add_dynamic_entry(elfobj_t *obj, int64_t tag, uint64_t value);
elf_err_t elf_validate_ex(elfobj_t *obj, const elf_validate_options_t *options, char **diagnostics);
elf_err_t elf_validate(elfobj_t *obj, char **diagnostics);
elf_err_t elf_set_validation_mode(elfobj_t *obj, elf_validate_mode_t mode);
elf_validate_mode_t elf_get_validation_mode(const elfobj_t *obj);
size_t elf_diag_count(const elfobj_t *obj);
int elf_diag_entry(const elfobj_t *obj, size_t index, elf_diag_entry_t *out);

const char *elf_errstr(elf_err_t err);
elf_err_t elf_last_error(const elfobj_t *obj);
const char *elf_last_diagnostics(const elfobj_t *obj);
elf_err_t elf_register_reloc_backend(const struct elf_reloc_backend *backend);

elfobj_t *elf_create(uint16_t type, uint16_t machine, elfobj_class_t cls, elfobj_endian_t endian);
elf_err_t elf_finalize(elfobj_t *obj);
elf_err_t elf_set_type(elfobj_t *obj, uint16_t type);
uint16_t elf_type(const elfobj_t *obj);
uint16_t elf_machine(const elfobj_t *obj);
elfobj_class_t elf_class(const elfobj_t *obj);
elfobj_endian_t elf_endian(const elfobj_t *obj);

size_t elf_section_count(const elfobj_t *obj);
size_t elf_symbol_count(const elfobj_t *obj);
size_t elf_reloc_count(const elfobj_t *obj);
uint16_t elf_program_header_count(const elfobj_t *obj);
size_t elf_segment_count(const elfobj_t *obj);

const char *elf_section_name(const elf_section_t *section);
uint32_t elf_section_type(const elf_section_t *section);
uint64_t elf_section_flags(const elf_section_t *section);
uint64_t elf_section_size(const elf_section_t *section);
const void *elf_section_data(const elf_section_t *section, size_t *size_out);

const char *elf_symbol_name(const elf_symbol_t *symbol);
uint64_t elf_symbol_value(const elf_symbol_t *symbol);
uint64_t elf_symbol_size(const elf_symbol_t *symbol);

elf_segment_t *elf_add_segment(elfobj_t *obj, uint32_t type, uint32_t flags, uint64_t align);
elf_segment_t *elf_add_load_segment(elfobj_t *obj, uint32_t flags, uint64_t align);
elf_segment_t *elf_add_dynamic_segment(elfobj_t *obj, uint64_t align);
elf_segment_t *elf_add_tls_segment(elfobj_t *obj, uint64_t align);
elf_segment_t *elf_add_interp_segment(elfobj_t *obj, const char *interp_path);
elf_err_t elf_segment_add_section(elf_segment_t *segment, elf_section_t *section);
uint32_t elf_segment_type(const elf_segment_t *segment);
uint32_t elf_segment_flags(const elf_segment_t *segment);
uint64_t elf_segment_align(const elf_segment_t *segment);
size_t elf_segment_section_count(const elf_segment_t *segment);
int elf_segment_contains_section(const elf_segment_t *segment, const elf_section_t *section);

uint32_t elf_hash_sysv(const char *name);
uint32_t elf_hash_gnu(const char *name);

int elf_section_is_debug(const elf_section_t *section);
int elf_section_is_cfi(const elf_section_t *section);
int elf_section_is_split_dwarf(const elf_section_t *section);
int elf_section_is_compressed_debug(const elf_section_t *section);
elf_err_t elf_debug_set_compression_hint(elf_section_t *section, uint32_t ch_type,
                                         uint64_t uncompressed_size, uint64_t addralign);
int elf_debug_get_compression_hint(const elf_section_t *section, uint32_t *ch_type_out,
                                   uint64_t *uncompressed_size_out, uint64_t *addralign_out);
elf_err_t elf_eh_frame_stats(const elf_section_t *section, size_t *cie_count_out,
                             size_t *fde_count_out);
elf_err_t elf_debug_validate(elfobj_t *obj, char **diagnostics);
elf_err_t elf_debug_sort_sections(elfobj_t *obj);

#ifdef __cplusplus
}
#endif

#endif
