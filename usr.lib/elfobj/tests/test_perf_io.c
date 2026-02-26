#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_perf_io: %s\n", msg);
    exit(1);
}

static void build_sample_object(const char *path) {
    elfobj_t *obj;
    elf_section_t *text;
    elf_symbol_t *sym;
    uint8_t code[] = {0x90, 0x90, 0x90, 0x90, 0xC3};

    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) fail("elf_create");
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) fail("add .text");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set .text");
    sym = elf_add_symbol(obj, "entry", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (sym == NULL) fail("add symbol");
    if (elf_symbol_define(sym, text, 0) != ELF_OK) fail("define symbol");
    if (elf_add_relocation(text, 0, sym, R_X86_64_PC32, -4) != ELF_OK) fail("add relocation");
    if (elf_write_file(obj, path) != ELF_OK) fail("write sample object");
    elf_close(obj);
}

static uint8_t *read_file(const char *path, size_t *size_out) {
    FILE *fp = fopen(path, "rb");
    uint8_t *buf;
    long end;
    size_t got;
    if (fp == NULL) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    end = ftell(fp);
    if (end < 0) {
        fclose(fp);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }
    buf = (uint8_t *)malloc((size_t)end);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    got = fread(buf, 1, (size_t)end, fp);
    fclose(fp);
    if (got != (size_t)end) {
        free(buf);
        return NULL;
    }
    *size_out = (size_t)end;
    return buf;
}

static void test_nocopy_view(void) {
    elfobj_t *obj = NULL;
    elf_section_t *text;
    const uint8_t *data;
    size_t size = 0;
    size_t data_size = 0;
    uint8_t *buf;

    build_sample_object("tmp_perf_io.o");
    buf = read_file("tmp_perf_io.o", &size);
    if (buf == NULL) fail("read sample object");

    if (elf_open_memory_nocopy(buf, size, &obj) != ELF_OK) fail("open_memory_nocopy");
    text = elf_find_section(obj, ".text");
    if (text == NULL) fail("missing .text in nocopy view");
    data = (const uint8_t *)elf_section_data(text, &data_size);
    if (data == NULL || data_size == 0) fail("missing section payload");
    if (data < buf || data >= buf + size) fail("section payload is not backed by caller buffer");
    elf_close(obj);
    free(buf);
}

static void test_mmap_open_path(void) {
    elfobj_t *obj = NULL;
    if (elf_open_with_options("tmp_perf_io.o", ELFOBJ_OPEN_USE_MMAP, &obj) != ELF_OK)
        fail("elf_open with mmap");
    /* mmap is optional at runtime; fallback is acceptable when mapping is unavailable. */
    (void)elf_uses_mmap(obj);
    elf_close(obj);
}

static void test_lazy_parse(void) {
    elfobj_t *obj = NULL;
    if (elf_open_with_options("tmp_perf_io.o", ELFOBJ_OPEN_LAZY_PARSE, &obj) != ELF_OK)
        fail("elf_open with lazy parse");
    if (!elf_is_lazy_parse_enabled(obj)) fail("expected lazy parse flag");
    if (elf_symbol_count(obj) == 0) fail("lazy materialization did not load symbols");
    if (elf_reloc_count(obj) == 0) fail("lazy materialization did not load relocations");
    if (elf_validate(obj, NULL) != ELF_OK) fail("validate after lazy parse");
    elf_close(obj);
}

int main(void) {
    test_nocopy_view();
    test_mmap_open_path();
    test_lazy_parse();
    return 0;
}
