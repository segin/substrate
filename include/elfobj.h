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

#ifndef R_386_NONE
#define R_386_NONE 0
#define R_386_32 1
#define R_386_PC32 2
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

elf_err_t elf_open(const char *path, elfobj_t **out);
elf_err_t elf_open_memory(const void *buf, size_t size, elfobj_t **out);
elf_err_t elf_write_file(elfobj_t *obj, const char *path);
void elf_close(elfobj_t *obj);

elf_section_t *elf_add_section(elfobj_t *obj, const char *name, uint32_t type, uint64_t flags);
elf_err_t elf_section_set_data(elf_section_t *section, const void *data, size_t size);
elf_err_t elf_section_set_align(elf_section_t *section, uint64_t align);
elf_section_t *elf_find_section(elfobj_t *obj, const char *name);

elf_symbol_t *elf_add_symbol(elfobj_t *obj, const char *name, uint64_t value,
                              uint64_t size, uint8_t bind, uint8_t type);
elf_err_t elf_symbol_define(elf_symbol_t *symbol, elf_section_t *section, uint64_t value);
elf_symbol_t *elf_find_symbol(elfobj_t *obj, const char *name);

elf_err_t elf_add_relocation(elf_section_t *section, uint64_t offset, elf_symbol_t *symbol,
                             uint32_t type, int64_t addend);

elf_err_t elf_link(elfobj_t **inputs, size_t count, elfobj_t **output);
elf_err_t elf_validate(elfobj_t *obj, char **diagnostics);

const char *elf_errstr(elf_err_t err);
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

const char *elf_section_name(const elf_section_t *section);
uint32_t elf_section_type(const elf_section_t *section);
uint64_t elf_section_flags(const elf_section_t *section);
uint64_t elf_section_size(const elf_section_t *section);
const void *elf_section_data(const elf_section_t *section, size_t *size_out);

const char *elf_symbol_name(const elf_symbol_t *symbol);
uint64_t elf_symbol_value(const elf_symbol_t *symbol);
uint64_t elf_symbol_size(const elf_symbol_t *symbol);

uint32_t elf_hash_sysv(const char *name);
uint32_t elf_hash_gnu(const char *name);

#ifdef __cplusplus
}
#endif

#endif
