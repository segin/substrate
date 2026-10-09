/*
 * ld_arch.c -- what differs between the machines the linker links for.
 *
 * i386 and x86-64 do the same things about shared objects: a PLT entry
 * for each imported function, a GOT entry for each imported address, a
 * record for the dynamic linker beside each.  They differ in how wide a
 * word is, in whether the record carries its addend (Elf64_Rela) or
 * leaves it in the place relocated (Elf32_Rel), in what the relocations
 * are called, and in the bytes of the PLT.  Those are written down here
 * once, in a description of each machine, and the code that plans and
 * fills in the dynamic sections is written once against the description.
 * It was written twice, a copy for each machine with the numbers
 * changed, and a fix made to one copy was a fix to make again in the
 * other.
 */

#include "ld.h"

static int i386_is_call_ref(uint32_t type, const elf_symbol_t *sym) {
    /* R_386_PLT32 says so outright.  R_386_PC32 is what a compiler not
     * told to make position-independent code writes for every call; on
     * i386 nothing but a branch is pc-relative, so against something
     * undefined it is one too. */
    return type == R_386_PLT32 || (type == R_386_PC32 && pc_relative_ref_is_call(sym));
}

static int i386_is_got_ref(uint32_t type) {
    return type == R_386_GOT32 || type == R_386_GOT32X;
}

static int i386_is_tls_gd_ref(uint32_t type) {
    return type == R_386_TLS_GD;
}

static int i386_is_tls_ie_ref(uint32_t type) {
    return type == R_386_TLS_IE || type == R_386_TLS_GOTIE;
}

static void i386_write_plt0(uint8_t *p, elfobj_endian_t e, uint64_t plt_addr, uint64_t gotplt_addr, int pic) {
    (void)plt_addr;
    if (pic) {
        /* pushl 4(%ebx); jmp *8(%ebx) */
        p[0] = 0xff;
        p[1] = 0xb3;
        write_u32_endian(p + 2, e, 4);
        p[6] = 0xff;
        p[7] = 0xa3;
        write_u32_endian(p + 8, e, 8);
    } else {
        /* pushl GOT+4; jmp *GOT+8 */
        p[0] = 0xff;
        p[1] = 0x35;
        write_u32_endian(p + 2, e, (uint32_t)(gotplt_addr + 4));
        p[6] = 0xff;
        p[7] = 0x25;
        write_u32_endian(p + 8, e, (uint32_t)(gotplt_addr + 8));
    }
    memset(p + 12, 0x90, 4);
}

static void i386_write_plt_entry(uint8_t *p, elfobj_endian_t e, uint64_t ent_addr, uint64_t slot_addr,
                                 uint64_t plt_addr, size_t ent, size_t slot_off, size_t rel_off, int pic) {
    (void)ent;
    p[0] = 0xff;
    if (pic) {
        p[1] = 0xa3;                    /* jmp *slot(%ebx) */
        write_u32_endian(p + 2, e, (uint32_t)slot_off);
    } else {
        p[1] = 0x25;                    /* jmp *slot */
        write_u32_endian(p + 2, e, (uint32_t)slot_addr);
    }
    /* What is pushed for the resolver is where this entry's relocation
     * is in .rel.plt, in bytes (the i386 psABI), not which entry it is. */
    p[6] = 0x68;
    write_u32_endian(p + 7, e, (uint32_t)rel_off);
    p[11] = 0xe9;
    write_u32_endian(p + 12, e, (uint32_t)(int32_t)((int64_t)plt_addr - (int64_t)(ent_addr + 16)));
}

static int x64_is_call_ref(uint32_t type, const elf_symbol_t *sym) {
    /* On x86-64 data is addressed pc-relatively as much as functions are
     * called that way, so a PC32 is a call only to what is, or may be, a
     * function. */
    return type == R_X86_64_PLT32 ||
           (type == R_X86_64_PC32 && pc_relative_ref_is_call(sym) &&
            (elf_symbol_type(sym) == STT_FUNC || elf_symbol_type(sym) == STT_NOTYPE));
}

static int x64_is_got_ref(uint32_t type) {
    switch (type) {
    case R_X86_64_GOT32:
    case R_X86_64_GOTPCREL:
    case R_X86_64_GOTPC32:
    case R_X86_64_GOTPCRELX:
    case R_X86_64_REX_GOTPCRELX:
        return 1;
    default:
        return 0;
    }
}

static int x64_is_tls_gd_ref(uint32_t type) {
    return type == R_X86_64_TLSGD;
}

static int x64_is_tls_ie_ref(uint32_t type) {
    return type == R_X86_64_GOTTPOFF;
}

static void x64_write_plt0(uint8_t *p, elfobj_endian_t e, uint64_t plt_addr, uint64_t gotplt_addr, int pic) {
    (void)pic;
    /* pushq GOT+8(%rip); jmp *GOT+16(%rip); nopl 0(%rax) */
    p[0] = 0xff;
    p[1] = 0x35;
    write_u32_endian(p + 2, e, (uint32_t)(int32_t)((int64_t)(gotplt_addr + 8) - (int64_t)(plt_addr + 6)));
    p[6] = 0xff;
    p[7] = 0x25;
    write_u32_endian(p + 8, e, (uint32_t)(int32_t)((int64_t)(gotplt_addr + 16) - (int64_t)(plt_addr + 12)));
    p[12] = 0x0f;
    p[13] = 0x1f;
    p[14] = 0x40;
    p[15] = 0x00;
}

static void x64_write_plt_entry(uint8_t *p, elfobj_endian_t e, uint64_t ent_addr, uint64_t slot_addr,
                                uint64_t plt_addr, size_t ent, size_t slot_off, size_t rel_off, int pic) {
    (void)slot_off;
    (void)rel_off;
    (void)pic;
    p[0] = 0xff;                        /* jmp *slot(%rip) */
    p[1] = 0x25;
    write_u32_endian(p + 2, e, (uint32_t)(int32_t)((int64_t)slot_addr - (int64_t)(ent_addr + 6)));
    p[6] = 0x68;                        /* pushq which entry this is */
    write_u32_endian(p + 7, e, (uint32_t)ent);
    p[11] = 0xe9;
    write_u32_endian(p + 12, e, (uint32_t)(int32_t)((int64_t)plt_addr - (int64_t)(ent_addr + 16)));
}

static const ld_arch_t arch_i386 = {
    .mode = 32,
    .machine = EM_386,
    .word = 4,
    .rela = 0,
    .rel_size = 8,
    .rel_shtype = SHT_REL,
    .rel_dyn = ".rel.dyn",
    .rel_plt = ".rel.plt",
    .r_abs = R_386_32,
    .r_pc32 = R_386_PC32,
    .r_relative = R_386_RELATIVE,
    .r_glob_dat = R_386_GLOB_DAT,
    .r_jump_slot = R_386_JMP_SLOT,
    .r_copy = R_386_COPY,
    .r_tpoff = R_386_TLS_TPOFF,
    .r_dtpmod = R_386_TLS_DTPMOD32,
    .r_dtpoff = R_386_TLS_DTPOFF32,
    .r_irelative = R_386_IRELATIVE,
    .pc32_may_be_data = 0,
    .is_call_ref = i386_is_call_ref,
    .is_got_ref = i386_is_got_ref,
    .is_tls_gd_ref = i386_is_tls_gd_ref,
    .is_tls_ie_ref = i386_is_tls_ie_ref,
    .write_plt0 = i386_write_plt0,
    .write_plt_entry = i386_write_plt_entry,
};

static const ld_arch_t arch_x86_64 = {
    .mode = 64,
    .machine = EM_X86_64,
    .word = 8,
    .rela = 1,
    .rel_size = 24,
    .rel_shtype = SHT_RELA,
    .rel_dyn = ".rela.dyn",
    .rel_plt = ".rela.plt",
    .r_abs = R_X86_64_64,
    .r_pc32 = R_X86_64_PC32,
    .r_relative = R_X86_64_RELATIVE,
    .r_glob_dat = R_X86_64_GLOB_DAT,
    .r_jump_slot = R_X86_64_JUMP_SLOT,
    .r_copy = R_X86_64_COPY,
    .r_tpoff = R_X86_64_TPOFF64,
    .r_dtpmod = R_X86_64_DTPMOD64,
    .r_dtpoff = R_X86_64_DTPOFF64,
    .r_irelative = R_X86_64_IRELATIVE,
    .pc32_may_be_data = 1,
    .is_call_ref = x64_is_call_ref,
    .is_got_ref = x64_is_got_ref,
    .is_tls_gd_ref = x64_is_tls_gd_ref,
    .is_tls_ie_ref = x64_is_tls_ie_ref,
    .write_plt0 = x64_write_plt0,
    .write_plt_entry = x64_write_plt_entry,
};

/* The description for a link mode (32, 64), or for a machine; NULL for
 * one the linker does not make dynamic sections for. */
const ld_arch_t *ld_arch_of_mode(int mode) {
    return mode == 32 ? &arch_i386 : mode == 64 ? &arch_x86_64 : NULL;
}

const ld_arch_t *ld_arch_of_machine(uint16_t machine) {
    return machine == EM_386 ? &arch_i386 : machine == EM_X86_64 ? &arch_x86_64 : NULL;
}

/* A GOT entry. */
void ld_arch_put_word(const ld_arch_t *a, uint8_t *p, elfobj_endian_t e, uint64_t v) {
    if (a->word == 8) {
        write_u64_endian(p, e, v);
    } else {
        write_u32_endian(p, e, (uint32_t)v);
    }
}

/* A record for the dynamic linker: at `where`, do `type` with symbol
 * number `sym`.  The addend goes in the record where the machine's
 * records have one, and is otherwise the caller's to put at `where`. */
void ld_arch_put_rel(const ld_arch_t *a, uint8_t *p, elfobj_endian_t e, uint64_t where, uint32_t type,
                     uint32_t sym, uint64_t addend) {
    if (a->rela) {
        write_u64_endian(p, e, where);
        write_u64_endian(p + 8, e, ((uint64_t)sym << 32) | type);
        write_u64_endian(p + 16, e, addend);
    } else {
        write_u32_endian(p, e, (uint32_t)where);
        write_u32_endian(p + 4, e, (sym << 8) | type);
    }
}
