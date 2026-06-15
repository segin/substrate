#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_roundtrip_identity: %s\n", msg);
    exit(1);
}

static int files_equal(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    int ca;
    int cb;

    if (!fa || !fb) {
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        return 0;
    }

    for (;;) {
        ca = fgetc(fa);
        cb = fgetc(fb);
        if (ca != cb) {
            fclose(fa);
            fclose(fb);
            return 0;
        }
        if (ca == EOF) {
            fclose(fa);
            fclose(fb);
            return 1;
        }
    }
}

int main(void) {
    elfobj_t *obj;
    elfobj_t *reopen;
    elf_section_t *text;
    elf_symbol_t *sym;
    uint8_t code[] = {0x90, 0xC3};

    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (!obj) fail("create");
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!text) fail("add section");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set data");
    sym = elf_add_symbol(obj, "rt", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (!sym) fail("add symbol");
    if (elf_write_file(obj, "tmp_identity_a.o") != ELF_OK) fail("write first");
    elf_close(obj);

    if (elf_open("tmp_identity_a.o", &reopen) != ELF_OK) fail("open first");
    if (elf_write_file(reopen, "tmp_identity_b.o") != ELF_OK) fail("write second");
    elf_close(reopen);

    if (!files_equal("tmp_identity_a.o", "tmp_identity_b.o")) {
        fail("round-trip bytes differ");
    }
    return 0;
}
