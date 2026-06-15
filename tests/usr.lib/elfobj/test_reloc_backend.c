#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_reloc_backend: %s\n", msg);
    exit(1);
}

typedef struct {
    int before_calls;
    int after_calls;
    int incr_calls;
} hook_state_t;

static int before_apply(const elf_reloc_t *reloc, void *user) {
    hook_state_t *st = (hook_state_t *)user;
    if (reloc == NULL) {
        return 0;
    }
    st->before_calls++;
    return 1;
}

static void after_apply(const elf_reloc_t *reloc, uint64_t value, void *user) {
    hook_state_t *st = (hook_state_t *)user;
    if (reloc == NULL || value == 0) {
        st->after_calls += 0;
    }
    st->after_calls++;
}

static void incr_note(const char *key, uint64_t value, void *user) {
    hook_state_t *st = (hook_state_t *)user;
    if (key != NULL && value != 0) {
        st->incr_calls += 0;
    }
    st->incr_calls++;
}

int main(void) {
    elfobj_t *obj32;
    elfobj_t *obj64;
    elf_section_t *text;
    elf_symbol_t *sym;
    elf_reloc_t *rel;
    elf_reloc_hooks_t hooks;
    hook_state_t state;
    uint64_t out = 0;
    char *diag = NULL;
    uint8_t code[8] = {0};

    obj32 = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    obj64 = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj32 || !obj64) fail("elf_create");

    if (elf_reloc_size_for_machine(EM_386, R_386_PC32) != 4) fail("i386 reloc size");
    if (!elf_reloc_is_pc_relative_for_machine(EM_386, R_386_PC32)) fail("i386 pc-relative");
    if (!elf_reloc_is_tls_for_machine(EM_386, R_386_TLS_LE)) fail("i386 tls detect");
    if (elf_reloc_size_for_machine(EM_X86_64, R_X86_64_64) != 8) fail("x86_64 reloc size");
    if (!elf_reloc_is_pc_relative_for_machine(EM_X86_64, R_X86_64_PC32)) fail("x86_64 pc-relative");
    if (!elf_reloc_is_tls_for_machine(EM_X86_64, R_X86_64_TPOFF32)) fail("x86_64 tls detect");

    if (elf_apply_relocation_value(obj32, R_386_PC32, 0x1004, 0x2000, -4, &out) != ELF_OK)
        fail("apply i386 pc32");
    if (out != (uint32_t)(0x2000 - 0x1004 - 4)) fail("i386 pc32 value");

    if (elf_apply_relocation_value(obj64, R_X86_64_32S, 0, 0x7fffffffULL, 1, &out) != ELF_ERR_RELOC)
        fail("x86_64 signed overflow");

    if (elf_apply_relocation_value(obj64, 0xffffffffu, 0, 0, 0, &out) != ELF_ERR_UNSUPPORTED)
        fail("unsupported relocation should fail");
    if (elf_validate(obj64, &diag) != ELF_OK) {
        free(diag);
        fail("validate baseline");
    }
    free(diag);

    text = elf_add_section(obj64, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!text) fail("add text");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set text data");
    sym = elf_add_symbol(obj64, "f", 0x3000, 4, STB_GLOBAL, STT_FUNC);
    if (!sym) fail("add symbol");
    if (elf_add_relocation(text, 0, sym, R_X86_64_64, 8) != ELF_OK) fail("add relocation");

    if (elf_section_reloc_count(text) != 1) fail("section reloc count");
    rel = elf_section_reloc_at(text, 0);
    if (!rel) fail("section reloc at");
    if (elf_reloc_type(rel) != R_X86_64_64) fail("reloc type");
    if (elf_reloc_offset(rel) != 0) fail("reloc offset");
    if (elf_reloc_addend(rel) != 8) fail("reloc addend");
    if (!elf_reloc_has_addend(rel)) fail("reloc has addend");
    if (elf_reloc_symbol(rel) != sym) fail("reloc symbol");
    if (elf_reloc_section(rel) != text) fail("reloc section");

    memset(&state, 0, sizeof(state));
    memset(&hooks, 0, sizeof(hooks));
    hooks.before_apply = before_apply;
    hooks.after_apply = after_apply;
    hooks.incremental_note = incr_note;
    if (elf_set_reloc_hooks(obj64, &hooks, &state) != ELF_OK) fail("set hooks");

    if (elf_apply_relocation(rel, 0x1000, 0x2000, &out) != ELF_OK) fail("apply relocation object");
    if (out != 0x2008) fail("relocation value");
    if (state.before_calls != 1 || state.after_calls != 1 || state.incr_calls != 1)
        fail("relocation hooks");

    elf_close(obj32);
    elf_close(obj64);
    return 0;
}
