#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_section_segment: %s\n", msg);
    exit(1);
}

int main(void) {
    elfobj_t *obj;
    elfobj_t *group_obj;
    elfobj_t *roundtrip;
    elf_section_t *text;
    elf_section_t *group_text;
    elf_section_t *group_sec;
    elf_section_t *data;
    elf_section_t *tdata;
    elf_section_t *note;
    elf_section_t *dyn;
    elf_segment_t *load;
    elf_segment_t *tls;
    elf_segment_t *interp;
    uint8_t text_bytes[] = {0x90, 0xC3};
    uint8_t data_bytes[] = {'a', 'b', 'c', 0};
    char *diag = NULL;
    char tmp_path[] = "tmp_group_XXXXXX";
    int fd;

    obj = elf_create(ET_DYN, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) fail("elf_create");

    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    data = elf_add_section(obj, ".rodata.str", SHT_PROGBITS, SHF_ALLOC);
    tdata = elf_add_section(obj, ".tdata", SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
    note = elf_add_section(obj, ".note.test", SHT_NOTE, SHF_ALLOC);
    dyn = elf_add_section(obj, ".dynamic", SHT_DYNAMIC, SHF_ALLOC | SHF_WRITE);
    if (!text || !data || !tdata || !note || !dyn) fail("add sections");

    if (elf_section_set_data(text, text_bytes, sizeof(text_bytes)) != ELF_OK) fail("text data");
    if (elf_section_set_data(data, data_bytes, sizeof(data_bytes)) != ELF_OK) fail("data data");
    if (elf_section_set_data(dyn, data_bytes, 2) != ELF_OK) fail("dyn data");
    if (elf_section_set_type(data, SHT_PROGBITS) != ELF_OK) fail("set type");
    if (elf_section_set_flags(data, SHF_ALLOC) != ELF_OK) fail("set flags");
    /* test elf_section_set_align coverage */
    if (elf_section_set_align(NULL, 16) != ELF_ERR_STATE) fail("set align NULL");
    if (elf_section_set_align(text, 0) != ELF_OK) fail("set align 0");
    if (elf_section_align(text) != 1) fail("align 0 did not result in align 1");
    if (elf_section_set_align(text, 16) != ELF_OK) fail("set align 16");
    if (elf_section_align(text) != 16) fail("align 16 did not result in align 16");

    if (elf_section_set_merge(data, 1, 1) != ELF_OK) fail("set merge");
    if (elf_section_set_tls(tdata, 1) != ELF_OK) fail("set tls");
    if (elf_section_set_note_info(note, 7, "TESTNOTE") != ELF_OK) fail("set note");
    if (elf_section_set_name(data, ".rodata.str1.1") != ELF_OK) fail("set name");
    if (elf_section_set_name(NULL, ".rodata") != ELF_ERR_STATE) fail("set name (NULL section)");
    if (elf_section_set_name(data, NULL) != ELF_ERR_STATE) fail("set name (NULL name)");

    if (elf_reorder_section(obj, note, 1) != ELF_OK) fail("reorder section");
    if (elf_remove_section(obj, data) != ELF_OK) fail("remove section");

    load = elf_add_load_segment(obj, 0x5, 0x1000);
    tls = elf_add_tls_segment(obj, 8);
    interp = elf_add_interp_segment(obj, "/lib64/ld-linux-x86-64.so.2");
    if (!load || !tls || !interp) fail("add segments");

    if (elf_segment_add_section(load, text) != ELF_OK) fail("assign load text");
    if (elf_segment_add_section(load, dyn) != ELF_OK) fail("assign load dynamic");
    if (elf_segment_add_section(tls, tdata) != ELF_OK) fail("assign tls");
    if (elf_segment_type(load) != PT_LOAD) fail("load type");
    if (elf_segment_type(NULL) != 0) fail("null segment type");
    if (elf_segment_flags(load) != 0x5) fail("load flags");
    if (elf_segment_flags(tls) != 0x4) fail("tls flags");
    if (elf_segment_flags(NULL) != 0) fail("null segment flags");
    if (!elf_segment_contains_section(load, text)) fail("load contains text");
    if (elf_segment_contains_section(load, NULL)) fail("load contains NULL text");
    if (elf_segment_contains_section(NULL, text)) fail("NULL load contains text");
    if (elf_segment_section_count(load) < 1) fail("load section count");
    if (elf_segment_section_count(NULL) != 0) fail("null segment section count");
    if (elf_segment_count(obj) < 3) fail("segment count");

    if (elf_segment_align(load) != 0x1000) fail("load align");
    if (elf_segment_align(tls) != 8) fail("tls align");
    if (elf_segment_align(NULL) != 0) fail("null segment align");

    elf_segment_t *zero_align = elf_add_load_segment(obj, 0x5, 0);
    if (!zero_align) fail("add zero align segment");
    if (elf_segment_align(zero_align) != 1) fail("zero align segment should default to 1");

    if (elf_validate(obj, &diag) != ELF_OK) fail(diag ? diag : "validate");
    free(diag);

    if (elf_finalize(obj) != ELF_OK) fail("finalize");
    if (elf_section_set_align(text, 32) != ELF_ERR_STATE) fail("set align on immutable object");

    elf_close(obj);

    group_obj = elf_create(ET_REL, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!group_obj) fail("group obj");
    group_text = elf_add_section(group_obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!group_text) fail("group text");
    if (elf_section_set_data(group_text, text_bytes, 1) != ELF_OK) fail("group text data");
    if (elf_section_set_group(group_text, 1, 1) != ELF_OK) fail("set group");
    if (elf_section_set_group_signature(group_text, "group_sig") != ELF_OK) fail("set group signature");
    fd = mkstemp(tmp_path);
    if (fd < 0) fail("mkstemp");
    close(fd);
    if (elf_write_file(group_obj, tmp_path) != ELF_OK) fail("write file");
    if (elf_open(tmp_path, &roundtrip) != ELF_OK) fail("open roundtrip");
    group_sec = elf_find_section(roundtrip, ".group");
    if (group_sec == NULL || elf_section_type(group_sec) != SHT_GROUP) fail("missing .group");
    if (elf_section_size(group_sec) < 8) fail("group payload too small");
    elf_close(roundtrip);
    unlink(tmp_path);
    elf_close(group_obj);
    return 0;
}
