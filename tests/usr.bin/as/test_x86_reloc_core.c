#include "as_x86_reloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef R_X86_64_GOTPCRELX
#define R_X86_64_GOTPCRELX 41
#endif
#ifndef R_X86_64_REX_GOTPCRELX
#define R_X86_64_REX_GOTPCRELX 42
#endif
#ifndef R_X86_64_TLSLD
#define R_X86_64_TLSLD 20
#endif

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void check_map(unsigned machine, as_x86_reloc_kind_t kind, uint32_t expected) {
    uint32_t t = 0;
    if (as_x86_reloc_type(machine, kind, &t) != 0 || t != expected) {
        fail("reloc map mismatch");
    }
}

static void emit_all_i386(void) {
    elfobj_t *o;
    elf_section_t *text;
    elf_symbol_t *sym;
    unsigned char data[128];
    size_t i;
    as_x86_reloc_kind_t kinds[] = {
        AS_X86_RELOC_R_386_32,      AS_X86_RELOC_R_386_PC32, AS_X86_RELOC_R_386_GOT32,
        AS_X86_RELOC_R_386_PLT32,   AS_X86_RELOC_R_386_GOTOFF,
        AS_X86_RELOC_R_386_GOTPC,   AS_X86_RELOC_R_386_TLS_GD,
        AS_X86_RELOC_R_386_TLS_LDM, AS_X86_RELOC_R_386_TLS_IE,
        AS_X86_RELOC_R_386_TLS_LE,
    };

    memset(data, 0, sizeof(data));
    o = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (o == NULL) {
        fail("elf_create i386 failed");
    }
    text = elf_add_section(o, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL || elf_section_set_data(text, data, sizeof(data)) != ELF_OK) {
        fail("i386 text section setup failed");
    }
    sym = elf_add_symbol(o, "sym", 0, 0, STB_GLOBAL, STT_FUNC);
    if (sym == NULL || elf_symbol_define(sym, text, 0) != ELF_OK) {
        fail("i386 symbol setup failed");
    }

    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        if (as_x86_emit_reloc(text, EM_386, kinds[i], i * 4, sym, 0) != 0) {
            fail("i386 relocation emission failed");
        }
    }

    if (elf_reloc_count(o) != sizeof(kinds) / sizeof(kinds[0])) {
        fail("i386 relocation count mismatch");
    }

    elf_close(o);
}

static void emit_all_x86_64(void) {
    elfobj_t *o;
    elf_section_t *text;
    elf_symbol_t *sym;
    unsigned char data[256];
    size_t i;
    as_x86_reloc_kind_t kinds[] = {
        AS_X86_RELOC_R_X86_64_64,
        AS_X86_RELOC_R_X86_64_PC32,
        AS_X86_RELOC_R_X86_64_32,
        AS_X86_RELOC_R_X86_64_32S,
        AS_X86_RELOC_R_X86_64_GOT32,
        AS_X86_RELOC_R_X86_64_PLT32,
        AS_X86_RELOC_R_X86_64_GOTPCREL,
        AS_X86_RELOC_R_X86_64_GOTPCRELX,
        AS_X86_RELOC_R_X86_64_REX_GOTPCRELX,
        AS_X86_RELOC_R_X86_64_TLSGD,
        AS_X86_RELOC_R_X86_64_TLSLD,
        AS_X86_RELOC_R_X86_64_GOTTPOFF,
        AS_X86_RELOC_R_X86_64_TPOFF32,
    };

    memset(data, 0, sizeof(data));
    o = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (o == NULL) {
        fail("elf_create x86_64 failed");
    }
    text = elf_add_section(o, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL || elf_section_set_data(text, data, sizeof(data)) != ELF_OK) {
        fail("x86_64 text section setup failed");
    }
    sym = elf_add_symbol(o, "sym", 0, 0, STB_GLOBAL, STT_FUNC);
    if (sym == NULL || elf_symbol_define(sym, text, 0) != ELF_OK) {
        fail("x86_64 symbol setup failed");
    }

    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        if (as_x86_emit_reloc(text, EM_X86_64, kinds[i], i * 8, sym, 0) != 0) {
            fail("x86_64 relocation emission failed");
        }
    }

    if (elf_reloc_count(o) != sizeof(kinds) / sizeof(kinds[0])) {
        fail("x86_64 relocation count mismatch");
    }

    elf_close(o);
}

int main(void) {
    check_map(EM_386, AS_X86_RELOC_R_386_32, R_386_32);
    check_map(EM_386, AS_X86_RELOC_R_386_PC32, R_386_PC32);
    check_map(EM_386, AS_X86_RELOC_R_386_GOT32, R_386_GOT32);
    check_map(EM_386, AS_X86_RELOC_R_386_PLT32, R_386_PLT32);
    check_map(EM_386, AS_X86_RELOC_R_386_GOTOFF, R_386_GOTOFF);
    check_map(EM_386, AS_X86_RELOC_R_386_GOTPC, R_386_GOTPC);
    check_map(EM_386, AS_X86_RELOC_R_386_TLS_GD, R_386_TLS_GD);
    check_map(EM_386, AS_X86_RELOC_R_386_TLS_LDM, R_386_TLS_LDM);
    check_map(EM_386, AS_X86_RELOC_R_386_TLS_IE, R_386_TLS_IE);
    check_map(EM_386, AS_X86_RELOC_R_386_TLS_LE, R_386_TLS_LE);

    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_64, R_X86_64_64);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_PC32, R_X86_64_PC32);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_32, R_X86_64_32);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_32S, R_X86_64_32S);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_GOT32, R_X86_64_GOT32);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_PLT32, R_X86_64_PLT32);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_GOTPCREL, R_X86_64_GOTPCREL);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_GOTPCRELX, R_X86_64_GOTPCRELX);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_REX_GOTPCRELX, R_X86_64_REX_GOTPCRELX);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_TLSGD, R_X86_64_TLSGD);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_TLSLD, R_X86_64_TLSLD);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_GOTTPOFF, R_X86_64_GOTTPOFF);
    check_map(EM_X86_64, AS_X86_RELOC_R_X86_64_TPOFF32, R_X86_64_TPOFF32);

    emit_all_i386();
    emit_all_x86_64();

    puts("ok");
    return 0;
}
