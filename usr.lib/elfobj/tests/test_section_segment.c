#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_section_segment: %s\n", msg);
    exit(1);
}

int main(void) {
    elfobj_t *obj;
    elf_section_t *text;
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
    if (elf_section_set_group(text, 1, 1) != ELF_OK) fail("set group");
    if (elf_section_set_merge(data, 1, 1) != ELF_OK) fail("set merge");
    if (elf_section_set_tls(tdata, 1) != ELF_OK) fail("set tls");
    if (elf_section_set_note_info(note, 7, "TESTNOTE") != ELF_OK) fail("set note");

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
    if (elf_segment_flags(load) != 0x5) fail("load flags");
    if (elf_segment_flags(tls) != 0x4) fail("tls flags");
    if (elf_segment_flags(NULL) != 0) fail("null segment flags");
    if (!elf_segment_contains_section(load, text)) fail("load contains text");
    if (elf_segment_section_count(load) < 1) fail("load section count");
    if (elf_segment_count(obj) < 3) fail("segment count");

    if (elf_validate(obj, &diag) != ELF_OK) fail(diag ? diag : "validate");
    free(diag);
    elf_close(obj);
    return 0;
}
