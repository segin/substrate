/*
 * An object read from a file or a buffer, edited, and written twice must
 * come out the same both times, and both times as edited: the image it was
 * read from stops describing it once it has been changed.
 */
#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_write_twice: %s\n", msg);
    exit(1);
}

/**
 * Read a whole file into a newly allocated buffer.
 *
 * @param path  the file to read
 * @param size  set to the number of bytes read
 * @return the bytes, which the caller frees; exits the test on failure
 */
static unsigned char *slurp(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long len;

    if (f == NULL) fail("open output");
    if (fseek(f, 0, SEEK_END) != 0) fail("seek output");
    len = ftell(f);
    if (len <= 0) fail("empty output");
    if (fseek(f, 0, SEEK_SET) != 0) fail("rewind output");
    buf = malloc((size_t)len);
    if (buf == NULL) fail("malloc");
    if (fread(buf, 1, (size_t)len, f) != (size_t)len) fail("read output");
    fclose(f);
    *size = (size_t)len;
    return buf;
}

/**
 * Check that a written file holds the edits check_twice made: the new ELF
 * header flags and the new .text bytes.
 *
 * @param path  the file to check
 * @param code  the .text bytes it must hold
 * @param n     their count
 */
static void check_edited(const char *path, const unsigned char *code, size_t n) {
    elfobj_t *obj;
    elf_section_t *text;
    const void *data;
    size_t size = 0;

    if (elf_open(path, &obj) != ELF_OK) fail("reopen output");
    if (elf_flags(obj) != 0x1234u) fail("output lost the edited header flags");
    text = elf_find_section(obj, ".text");
    if (text == NULL) fail("output lost .text");
    data = elf_section_data(text, &size);
    if (size != n || memcmp(data, code, n) != 0) fail("output lost the edited .text");
    elf_close(obj);
}

/**
 * Edit an object that was read in, write it to two files, and require both
 * to be byte for byte the same and to hold the edits.
 *
 * @param obj  the object, freshly read; closed here
 * @param tag  names the case in the output file names
 */
static void check_twice(elfobj_t *obj, const char *tag) {
    static const unsigned char newcode[] = {0xCC, 0xCC, 0x90, 0xC3};
    char p1[64];
    char p2[64];
    unsigned char *a;
    unsigned char *b;
    size_t na;
    size_t nb;
    elf_section_t *text;

    snprintf(p1, sizeof(p1), "tmp_write_twice_%s_1.o", tag);
    snprintf(p2, sizeof(p2), "tmp_write_twice_%s_2.o", tag);

    if (elf_set_flags(obj, 0x1234u) != ELF_OK) fail("elf_set_flags");
    text = elf_find_section(obj, ".text");
    if (text == NULL) fail("missing .text");
    if (elf_section_set_data(text, newcode, sizeof(newcode)) != ELF_OK) fail("elf_section_set_data");

    if (elf_write_file(obj, p1) != ELF_OK) fail("first elf_write_file");
    if (elf_write_file(obj, p2) != ELF_OK) fail("second elf_write_file");
    elf_close(obj);

    a = slurp(p1, &na);
    b = slurp(p2, &nb);
    if (na != nb || memcmp(a, b, na) != 0) fail("second write differs from the first");
    free(a);
    free(b);
    check_edited(p1, newcode, sizeof(newcode));
    check_edited(p2, newcode, sizeof(newcode));
    remove(p1);
    remove(p2);
}

int main(void) {
    static const unsigned char code[] = {0x90, 0x90, 0x90, 0xC3};
    elfobj_t *obj;
    elf_section_t *text;
    unsigned char *img;
    size_t nimg;

    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) fail("elf_create");
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) fail("elf_add_section");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("elf_section_set_data");
    if (elf_add_symbol(obj, "func", 0, sizeof(code), STB_GLOBAL, STT_FUNC) == NULL) fail("elf_add_symbol");
    if (elf_write_file(obj, "tmp_write_twice_src.o") != ELF_OK) fail("write source");
    elf_close(obj);

    if (elf_open("tmp_write_twice_src.o", &obj) != ELF_OK) fail("elf_open");
    check_twice(obj, "file");

    img = slurp("tmp_write_twice_src.o", &nimg);
    if (elf_open_memory(img, nimg, &obj) != ELF_OK) fail("elf_open_memory");
    check_twice(obj, "mem");
    free(img);

    remove("tmp_write_twice_src.o");
    return 0;
}
