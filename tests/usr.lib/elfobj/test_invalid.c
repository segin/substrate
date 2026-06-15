#include <elf.h>
#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf_private.h"

static void fail(const char *msg) {
    fprintf(stderr, "test_invalid: %s\n", msg);
    exit(1);
}

static void build_minimal_rel(uint8_t **out_buf, size_t *out_sz) {
    elfobj_t *obj;
    elf_section_t *text;
    uint8_t code[] = {0x90, 0xC3};

    if (out_buf == NULL || out_sz == NULL) {
        fail("build_minimal_rel requires output storage");
    }

    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        fail("failed to create minimal relocatable object");
    }

    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) {
        elf_close(obj);
        fail("failed to add .text to minimal relocatable object");
    }
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) {
        elf_close(obj);
        fail("failed to set .text payload");
    }
    if (elf__write_to_buffer(obj, out_buf, out_sz) != ELF_OK) {
        elf_close(obj);
        fail("failed to serialize minimal relocatable object");
    }

    elf_close(obj);
}

static Elf64_Shdr *find_section64(uint8_t *buf, size_t sz, const char *name) {
    Elf64_Ehdr *eh;
    Elf64_Shdr *shdrs;
    uint64_t shoff;
    uint16_t shnum;
    uint16_t shentsize;
    uint16_t shstrndx;
    uint64_t shstr_off;
    uint64_t shstr_size;
    const char *shstrtab;
    size_t i;

    if (buf == NULL || name == NULL || sz < sizeof(Elf64_Ehdr)) {
        return NULL;
    }

    eh = (Elf64_Ehdr *)buf;
    if (memcmp(eh->e_ident, ELFMAG, SELFMAG) != 0 ||
        eh->e_ident[EI_CLASS] != ELFCLASS64) {
        return NULL;
    }

    shoff = elf__rd64((const uint8_t *)&eh->e_shoff, ELFOBJ_ENDIAN_LE);
    shnum = elf__rd16((const uint8_t *)&eh->e_shnum, ELFOBJ_ENDIAN_LE);
    shentsize = elf__rd16((const uint8_t *)&eh->e_shentsize, ELFOBJ_ENDIAN_LE);
    shstrndx = elf__rd16((const uint8_t *)&eh->e_shstrndx, ELFOBJ_ENDIAN_LE);
    if (shentsize != sizeof(Elf64_Shdr) || shstrndx >= shnum) {
        return NULL;
    }
    if (shoff > sz || shnum > (sz - shoff) / sizeof(Elf64_Shdr)) {
        return NULL;
    }

    shdrs = (Elf64_Shdr *)(buf + shoff);
    shstr_off = elf__rd64((const uint8_t *)&shdrs[shstrndx].sh_offset, ELFOBJ_ENDIAN_LE);
    shstr_size = elf__rd64((const uint8_t *)&shdrs[shstrndx].sh_size, ELFOBJ_ENDIAN_LE);
    if (shstr_off > sz || shstr_size > sz - shstr_off) {
        return NULL;
    }

    shstrtab = (const char *)(buf + shstr_off);
    for (i = 0; i < shnum; ++i) {
        uint32_t name_off = elf__rd32((const uint8_t *)&shdrs[i].sh_name, ELFOBJ_ENDIAN_LE);

        if (name_off >= shstr_size) {
            continue;
        }
        if (strcmp(shstrtab + name_off, name) == 0) {
            return &shdrs[i];
        }
    }

    return NULL;
}

static void test_unterminated_shstrtab_rejected(void) {
    uint8_t *buf = NULL;
    size_t sz = 0;
    Elf64_Shdr *shstr;
    uint64_t shstr_off;
    uint64_t shstr_size;
    elfobj_t *obj = NULL;
    elf_err_t err;

    build_minimal_rel(&buf, &sz);

    shstr = find_section64(buf, sz, ".shstrtab");
    if (shstr == NULL) {
        free(buf);
        fail("failed to find .shstrtab in minimal relocatable object");
    }

    shstr_off = elf__rd64((const uint8_t *)&shstr->sh_offset, ELFOBJ_ENDIAN_LE);
    shstr_size = elf__rd64((const uint8_t *)&shstr->sh_size, ELFOBJ_ENDIAN_LE);
    if (shstr_size == 0 || shstr_off > sz || shstr_size > sz - shstr_off) {
        free(buf);
        fail("invalid .shstrtab bounds in minimal relocatable object");
    }

    buf[shstr_off + shstr_size - 1] = 'X';
    err = elf_open_memory(buf, sz, &obj);
    if (err != ELF_ERR_FORMAT) {
        if (obj != NULL) {
            elf_close(obj);
        }
        free(buf);
        fail("expected ELF_ERR_FORMAT for unterminated .shstrtab");
    }

    free(buf);
}

static void test_invalid_section_align_rejected(void) {
    uint8_t *buf = NULL;
    size_t sz = 0;
    Elf64_Shdr *text;
    elfobj_t *obj = NULL;
    elf_err_t err;

    build_minimal_rel(&buf, &sz);

    text = find_section64(buf, sz, ".text");
    if (text == NULL) {
        free(buf);
        fail("failed to find .text in minimal relocatable object");
    }

    elf__wr64((uint8_t *)&text->sh_addralign, ELFOBJ_ENDIAN_LE, 3);
    err = elf_open_memory(buf, sz, &obj);
    if (err != ELF_ERR_FORMAT) {
        if (obj != NULL) {
            elf_close(obj);
        }
        free(buf);
        fail("expected ELF_ERR_FORMAT for invalid section alignment");
    }

    free(buf);
}

int main(void) {
    unsigned char bad[8] = {0x7f, 'E', 'L', 'F', 0xff, 0, 0, 0};
    unsigned char bad_phdr[64] = {0};
    elfobj_t *obj = NULL;
    if (elf_open_memory(bad, sizeof(bad), &obj) == ELF_OK) {
        fprintf(stderr, "expected failure for malformed ELF\n");
        elf_close(obj);
        return 1;
    }

    bad_phdr[0] = 0x7f;
    bad_phdr[1] = 'E';
    bad_phdr[2] = 'L';
    bad_phdr[3] = 'F';
    bad_phdr[4] = 2;
    bad_phdr[5] = 1;
    bad_phdr[6] = 1;
    bad_phdr[16] = ET_EXEC & 0xff;
    bad_phdr[18] = 0x3e;
    bad_phdr[20] = 1;
    bad_phdr[32] = 60;
    bad_phdr[52] = 64;
    bad_phdr[54] = 56;
    bad_phdr[56] = 1;

    if (elf_open_memory(bad_phdr, sizeof(bad_phdr), &obj) == ELF_OK) {
        fprintf(stderr, "expected failure for truncated program headers\n");
        elf_close(obj);
        return 1;
    }

    test_unterminated_shstrtab_rejected();
    test_invalid_section_align_rejected();

    return 0;
}
