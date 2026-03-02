#include "elf_private.h"

#define ELFOBJ_MAX_RELOC_BACKENDS 16

static struct elf_reloc_backend g_backends[ELFOBJ_MAX_RELOC_BACKENDS];
static size_t g_backend_count;
static volatile int g_backend_lock;
static int g_builtin_backends_ready;

#if defined(__SIZEOF_INT128__)
typedef __int128 elf_swide_t;
#else
typedef long long elf_swide_t;
#endif

static void backend_lock(void) {
    while (!__sync_bool_compare_and_swap(&g_backend_lock, 0, 1)) {
    }
}

static void backend_unlock(void) {
    __sync_lock_release(&g_backend_lock);
}

static int swide_in_signed_bits(elf_swide_t v, int bits) {
    elf_swide_t minv;
    elf_swide_t maxv;

    if (bits <= 0 || bits > 64) {
        return 0;
    }
    if (bits == 64) {
        return 1;
    }
    minv = -((elf_swide_t)1 << (bits - 1));
    maxv = (((elf_swide_t)1 << (bits - 1)) - 1);
    return v >= minv && v <= maxv;
}

static int swide_in_unsigned_bits(elf_swide_t v, int bits) {
    elf_swide_t maxv;

    if (bits <= 0 || bits > 64) {
        return 0;
    }
    if (v < 0) {
        return 0;
    }
    if (bits == 64) {
        return 1;
    }
    maxv = (((elf_swide_t)1 << bits) - 1);
    return v <= maxv;
}

static uint64_t swide_to_width(elf_swide_t v, int bits) {
    uint64_t uv = (uint64_t)v;

    if (bits <= 0) {
        return 0;
    }
    if (bits >= 64) {
        return uv;
    }
    return uv & ((((uint64_t)1) << bits) - 1);
}

static int i386_reloc_size(uint32_t type) {
    switch (type) {
        case R_386_NONE:
            return 0;
        case R_386_32:
        case R_386_PC32:
        case R_386_GOT32:
        case R_386_PLT32:
        case R_386_RELATIVE:
        case R_386_GOTOFF:
        case R_386_GOTPC:
        case R_386_TLS_TPOFF:
        case R_386_TLS_IE:
        case R_386_TLS_GOTIE:
        case R_386_TLS_LE:
        case R_386_TLS_GD:
        case R_386_TLS_LDM:
        case R_386_TLS_LDO_32:
        case R_386_COPY:
        case R_386_GLOB_DAT:
        case R_386_JMP_SLOT:
        case R_386_TLS_DTPMOD32:
        case R_386_TLS_DTPOFF32:
        case R_386_TLS_LE_32:
        case R_386_TLS_TPOFF32:
        case R_386_SIZE32:
        case R_386_GOT32X:
        case R_386_IRELATIVE:
            return 4;
        case R_386_16:
        case R_386_PC16:
            return 2;
        case R_386_8:
        case R_386_PC8:
            return 1;
        default:
            return -1;
    }
}

static int i386_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_386_PC32:
        case R_386_PLT32:
        case R_386_GOTPC:
        case R_386_PC16:
        case R_386_PC8:
            return 1;
        default:
            return 0;
    }
}

static int i386_is_tls(uint32_t type) {
    switch (type) {
        case R_386_TLS_TPOFF:
        case R_386_TLS_IE:
        case R_386_TLS_GOTIE:
        case R_386_TLS_LE:
        case R_386_TLS_GD:
        case R_386_TLS_LDM:
        case R_386_TLS_LDO_32:
        case R_386_TLS_DTPMOD32:
        case R_386_TLS_DTPOFF32:
        case R_386_TLS_LE_32:
        case R_386_TLS_TPOFF32:
            return 1;
        default:
            return 0;
    }
}

static int i386_apply(const elfobj_reloc_ctx_t *ctx,
                      uint32_t type,
                      uint64_t place,
                      uint64_t sym_value,
                      int64_t addend,
                      uint64_t *out_value) {
    elf_swide_t v;
    (void)ctx;

    switch (type) {
        case R_386_NONE:
            *out_value = 0;
            return 0;
        case R_386_32:
        case R_386_RELATIVE:
        case R_386_COPY:
        case R_386_GLOB_DAT:
        case R_386_JMP_SLOT:
        case R_386_SIZE32:
        case R_386_GOT32X:
        case R_386_IRELATIVE:
        case R_386_TLS_DTPMOD32:
        case R_386_TLS_DTPOFF32:
        case R_386_TLS_LE_32:
        case R_386_TLS_TPOFF32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_386_PC32:
        case R_386_PLT32:
        case R_386_GOTPC:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_386_GOT32:
        case R_386_GOTOFF:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_386_TLS_TPOFF:
        case R_386_TLS_IE:
        case R_386_TLS_GOTIE:
        case R_386_TLS_LE:
        case R_386_TLS_GD:
        case R_386_TLS_LDM:
        case R_386_TLS_LDO_32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_386_16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_386_PC16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_386_8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        case R_386_PC8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        default:
            return -1;
    }
}

static int x64_reloc_size(uint32_t type) {
    switch (type) {
        case R_X86_64_NONE:
            return 0;
        case R_X86_64_64:
            return 8;
        case R_X86_64_PC32:
        case R_X86_64_GOT32:
        case R_X86_64_PLT32:
        case R_X86_64_GOTPCREL:
        case R_X86_64_32:
        case R_X86_64_32S:
        case R_X86_64_TLSGD:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_TPOFF32:
        case R_X86_64_COPY:
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
        case R_X86_64_RELATIVE:
        case R_X86_64_DTPOFF32:
        case R_X86_64_GOTPC32:
        case R_X86_64_SIZE32:
        case R_X86_64_GOTPC32_TLSDESC:
        case R_X86_64_TLSDESC_CALL:
        case R_X86_64_TLSDESC:
        case R_X86_64_GOTPCRELX:
        case R_X86_64_REX_GOTPCRELX:
            return 4;
        case R_X86_64_DTPMOD64:
        case R_X86_64_DTPOFF64:
        case R_X86_64_TPOFF64:
        case R_X86_64_PC64:
        case R_X86_64_GOTOFF64:
        case R_X86_64_SIZE64:
        case R_X86_64_IRELATIVE:
            return 8;
        case R_X86_64_16:
        case R_X86_64_PC16:
            return 2;
        case R_X86_64_8:
        case R_X86_64_PC8:
            return 1;
        default:
            return -1;
    }
}

static int x64_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_X86_64_PC32:
        case R_X86_64_PLT32:
        case R_X86_64_GOTPCREL:
        case R_X86_64_PC64:
        case R_X86_64_PC16:
        case R_X86_64_PC8:
            return 1;
        default:
            return 0;
    }
}

static int x64_is_tls(uint32_t type) {
    switch (type) {
        case R_X86_64_TLSGD:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_TPOFF32:
        case R_X86_64_DTPMOD64:
        case R_X86_64_DTPOFF64:
        case R_X86_64_TPOFF64:
        case R_X86_64_TLSLD:
        case R_X86_64_DTPOFF32:
        case R_X86_64_GOTPC32_TLSDESC:
        case R_X86_64_TLSDESC_CALL:
        case R_X86_64_TLSDESC:
            return 1;
        default:
            return 0;
    }
}

static int x64_apply(const elfobj_reloc_ctx_t *ctx,
                     uint32_t type,
                     uint64_t place,
                     uint64_t sym_value,
                     int64_t addend,
                     uint64_t *out_value) {
    elf_swide_t v;
    (void)ctx;

    switch (type) {
        case R_X86_64_NONE:
            *out_value = 0;
            return 0;
        case R_X86_64_64:
        case R_X86_64_DTPMOD64:
        case R_X86_64_DTPOFF64:
        case R_X86_64_TPOFF64:
        case R_X86_64_GOTOFF64:
        case R_X86_64_SIZE64:
        case R_X86_64_IRELATIVE:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 64)) {
                return -2;
            }
            *out_value = swide_to_width(v, 64);
            return 0;
        case R_X86_64_PC32:
        case R_X86_64_PLT32:
        case R_X86_64_GOTPCREL:
        case R_X86_64_GOTPCRELX:
        case R_X86_64_REX_GOTPCRELX:
        case R_X86_64_GOTPC32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_X86_64_32:
        case R_X86_64_GOT32:
        case R_X86_64_COPY:
        case R_X86_64_GLOB_DAT:
        case R_X86_64_JUMP_SLOT:
        case R_X86_64_RELATIVE:
        case R_X86_64_SIZE32:
        case R_X86_64_GOTPC32_TLSDESC:
        case R_X86_64_TLSDESC_CALL:
        case R_X86_64_TLSDESC:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_X86_64_32S:
        case R_X86_64_TLSGD:
        case R_X86_64_GOTTPOFF:
        case R_X86_64_TPOFF32:
        case R_X86_64_TLSLD:
        case R_X86_64_DTPOFF32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_X86_64_PC64:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            *out_value = swide_to_width(v, 64);
            return 0;
        case R_X86_64_16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_X86_64_PC16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_X86_64_8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        case R_X86_64_PC8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        default:
            return -1;
    }
}

static int mips_reloc_size(uint32_t type) {
    switch (type) {
        case R_MIPS_NONE:
            return 0;
        case R_MIPS_64:
        case R_MIPS_TLS_DTPMOD64:
        case R_MIPS_TLS_DTPREL64:
        case R_MIPS_TLS_TPREL64:
            return 8;
        case R_MIPS_16:
        case R_MIPS_GPREL16:
        case R_MIPS_LITERAL:
        case R_MIPS_GOT16:
        case R_MIPS_PC16:
        case R_MIPS_CALL16:
        case R_MIPS_HI16:
        case R_MIPS_LO16:
        case R_MIPS_GOT_HI16:
        case R_MIPS_GOT_LO16:
        case R_MIPS_CALL_HI16:
        case R_MIPS_CALL_LO16:
        case R_MIPS_TLS_DTPREL_HI16:
        case R_MIPS_TLS_DTPREL_LO16:
        case R_MIPS_TLS_TPREL_HI16:
        case R_MIPS_TLS_TPREL_LO16:
            return 2;
        default:
            return 4;
    }
}

static int mips_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_MIPS_PC16:
        case R_MIPS_REL16:
        case R_MIPS_REL32:
        case R_MICROMIPS_PC7_S1:
        case R_MICROMIPS_PC10_S1:
        case R_MICROMIPS_PC16_S1:
        case R_MICROMIPS_PC23_S2:
            return 1;
        default:
            return 0;
    }
}

static int mips_is_tls(uint32_t type) {
    switch (type) {
        case R_MIPS_TLS_DTPMOD32:
        case R_MIPS_TLS_DTPREL32:
        case R_MIPS_TLS_DTPMOD64:
        case R_MIPS_TLS_DTPREL64:
        case R_MIPS_TLS_GD:
        case R_MIPS_TLS_LDM:
        case R_MIPS_TLS_DTPREL_HI16:
        case R_MIPS_TLS_DTPREL_LO16:
        case R_MIPS_TLS_GOTTPREL:
        case R_MIPS_TLS_TPREL32:
        case R_MIPS_TLS_TPREL64:
        case R_MIPS_TLS_TPREL_HI16:
        case R_MIPS_TLS_TPREL_LO16:
            return 1;
        default:
            return 0;
    }
}

static int mips_apply(const elfobj_reloc_ctx_t *ctx,
                      uint32_t type,
                      uint64_t place,
                      uint64_t sym_value,
                      int64_t addend,
                      uint64_t *out_value) {
    elf_swide_t v;
    (void)ctx;

    switch (type) {
        case R_MIPS_NONE:
            *out_value = 0;
            return 0;
        case R_MIPS_32:
        case R_MIPS_REL32:
        case R_MIPS_COPY:
        case R_MIPS_GLOB_DAT:
        case R_MIPS_JUMP_SLOT:
        case R_MIPS_GPREL32:
        case R_MIPS_GOT_DISP:
        case R_MIPS_GOT_PAGE:
        case R_MIPS_GOT_OFST:
        case R_MIPS_RELGOT:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_MIPS_64:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            *out_value = swide_to_width(v, 64);
            return 0;
        case R_MIPS_16:
        case R_MIPS_GPREL16:
        case R_MIPS_LITERAL:
        case R_MIPS_GOT16:
        case R_MIPS_CALL16:
        case R_MIPS_TLS_DTPREL_LO16:
        case R_MIPS_TLS_TPREL_LO16:
        case R_MICROMIPS_HI16:
        case R_MICROMIPS_LO16:
        case R_MICROMIPS_GPREL16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 16) && !swide_in_unsigned_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_MIPS_PC16:
        case R_MIPS_REL16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_MIPS_HI16:
        case R_MIPS_GOT_HI16:
        case R_MIPS_CALL_HI16:
        case R_MIPS_TLS_DTPREL_HI16:
        case R_MIPS_TLS_TPREL_HI16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            *out_value = swide_to_width((v + 0x8000) >> 16, 16);
            return 0;
        case R_MIPS_LO16:
        case R_MIPS_GOT_LO16:
        case R_MIPS_CALL_LO16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_MIPS_26:
        case R_MICROMIPS_26_S1:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if ((v & 3) != 0) {
                return -2;
            }
            *out_value = swide_to_width(v >> 2, 26);
            return 0;
        case R_MICROMIPS_PC7_S1:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place) >> 1;
            if (!swide_in_signed_bits(v, 7)) {
                return -2;
            }
            *out_value = swide_to_width(v, 7);
            return 0;
        case R_MICROMIPS_PC10_S1:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place) >> 1;
            if (!swide_in_signed_bits(v, 10)) {
                return -2;
            }
            *out_value = swide_to_width(v, 10);
            return 0;
        case R_MICROMIPS_PC16_S1:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place) >> 1;
            if (!swide_in_signed_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_MICROMIPS_PC23_S2:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place) >> 2;
            if (!swide_in_signed_bits(v, 23)) {
                return -2;
            }
            *out_value = swide_to_width(v, 23);
            return 0;
        default:
            if (mips_is_tls(type)) {
                v = (elf_swide_t)sym_value + (elf_swide_t)addend;
                *out_value = swide_to_width(v, 64);
                return 0;
            }
            return -1;
    }
}

static int riscv_reloc_size(uint32_t type) {
    switch (type) {
        case R_RISCV_NONE:
            return 0;
        case R_RISCV_64:
        case R_RISCV_TLS_DTPMOD64:
        case R_RISCV_TLS_DTPREL64:
        case R_RISCV_TLS_TPREL64:
        case R_RISCV_ADD64:
        case R_RISCV_SUB64:
            return 8;
        case R_RISCV_ADD16:
        case R_RISCV_SUB16:
            return 2;
        case R_RISCV_ADD8:
        case R_RISCV_SUB8:
            return 1;
        default:
            return 4;
    }
}

static int riscv_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_RISCV_BRANCH:
        case R_RISCV_JAL:
        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT:
        case R_RISCV_PCREL_HI20:
        case R_RISCV_PCREL_LO12_I:
        case R_RISCV_PCREL_LO12_S:
        case R_RISCV_RVC_BRANCH:
        case R_RISCV_RVC_JUMP:
        case R_RISCV_32_PCREL:
            return 1;
        default:
            return 0;
    }
}

static int riscv_is_tls(uint32_t type) {
    switch (type) {
        case R_RISCV_TLS_DTPMOD32:
        case R_RISCV_TLS_DTPMOD64:
        case R_RISCV_TLS_DTPREL32:
        case R_RISCV_TLS_DTPREL64:
        case R_RISCV_TLS_TPREL32:
        case R_RISCV_TLS_TPREL64:
        case R_RISCV_TLS_GOT_HI20:
        case R_RISCV_TLS_GD_HI20:
        case R_RISCV_TPREL_HI20:
        case R_RISCV_TPREL_LO12_I:
        case R_RISCV_TPREL_LO12_S:
        case R_RISCV_TPREL_ADD:
            return 1;
        default:
            return 0;
    }
}

static int riscv_apply(const elfobj_reloc_ctx_t *ctx,
                       uint32_t type,
                       uint64_t place,
                       uint64_t sym_value,
                       int64_t addend,
                       uint64_t *out_value) {
    elf_swide_t v;
    (void)ctx;
    switch (type) {
        case R_RISCV_NONE:
        case R_RISCV_RELAX:
            *out_value = 0;
            return 0;
        case R_RISCV_32:
        case R_RISCV_RELATIVE:
        case R_RISCV_COPY:
        case R_RISCV_JUMP_SLOT:
        case R_RISCV_ADD32:
        case R_RISCV_SET32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_RISCV_64:
        case R_RISCV_IRELATIVE:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            *out_value = swide_to_width(v, 64);
            return 0;
        case R_RISCV_32_PCREL:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_RISCV_BRANCH:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if ((v & 1) != 0 || !swide_in_signed_bits(v >> 1, 13)) {
                return -2;
            }
            *out_value = swide_to_width(v >> 1, 13);
            return 0;
        case R_RISCV_JAL:
        case R_RISCV_CALL:
        case R_RISCV_CALL_PLT:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if ((v & 1) != 0 || !swide_in_signed_bits(v >> 1, 21)) {
                return -2;
            }
            *out_value = swide_to_width(v >> 1, 21);
            return 0;
        case R_RISCV_PCREL_HI20:
        case R_RISCV_HI20:
        case R_RISCV_GOT_HI20:
            v = (type == R_RISCV_HI20) ? (elf_swide_t)sym_value + (elf_swide_t)addend
                                       : (elf_swide_t)sym_value + (elf_swide_t)addend -
                                             (elf_swide_t)place;
            *out_value = swide_to_width((v + 0x800) >> 12, 20);
            return 0;
        case R_RISCV_PCREL_LO12_I:
        case R_RISCV_LO12_I:
        case R_RISCV_TPREL_LO12_I:
            v = (type == R_RISCV_LO12_I)
                    ? (elf_swide_t)sym_value + (elf_swide_t)addend
                    : (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            *out_value = swide_to_width(v, 12);
            return 0;
        case R_RISCV_PCREL_LO12_S:
        case R_RISCV_LO12_S:
        case R_RISCV_TPREL_LO12_S:
            v = (type == R_RISCV_LO12_S)
                    ? (elf_swide_t)sym_value + (elf_swide_t)addend
                    : (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            *out_value = swide_to_width(v, 12);
            return 0;
        case R_RISCV_RVC_BRANCH:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if ((v & 1) != 0 || !swide_in_signed_bits(v >> 1, 9)) {
                return -2;
            }
            *out_value = swide_to_width(v >> 1, 9);
            return 0;
        case R_RISCV_RVC_JUMP:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if ((v & 1) != 0 || !swide_in_signed_bits(v >> 1, 12)) {
                return -2;
            }
            *out_value = swide_to_width(v >> 1, 12);
            return 0;
        case R_RISCV_RVC_LUI:
        case R_RISCV_TPREL_HI20:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            *out_value = swide_to_width((v + 0x800) >> 12, 20);
            return 0;
        case R_RISCV_ADD8:
        case R_RISCV_SET8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        case R_RISCV_ADD16:
        case R_RISCV_SET16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_RISCV_SUB8:
        case R_RISCV_SUB16:
        case R_RISCV_SUB32:
        case R_RISCV_SUB64:
            v = (elf_swide_t)sym_value - (elf_swide_t)addend;
            *out_value = swide_to_width(v, type == R_RISCV_SUB8
                                              ? 8
                                              : type == R_RISCV_SUB16 ? 16
                                                                       : type == R_RISCV_SUB32 ? 32
                                                                                               : 64);
            return 0;
        default:
            if (riscv_is_tls(type)) {
                v = (elf_swide_t)sym_value + (elf_swide_t)addend;
                *out_value = swide_to_width(v, 64);
                return 0;
            }
            return -1;
    }
}

static int larch_reloc_size(uint32_t type) {
    switch (type) {
        case R_LARCH_NONE:
            return 0;
        case R_LARCH_64:
        case R_LARCH_TLS_DTPMOD64:
        case R_LARCH_TLS_DTPREL64:
        case R_LARCH_TLS_TPREL64:
        case R_LARCH_ADD64:
        case R_LARCH_SUB64:
            return 8;
        case R_LARCH_ADD16:
        case R_LARCH_SUB16:
            return 2;
        case R_LARCH_ADD8:
        case R_LARCH_SUB8:
            return 1;
        default:
            return 4;
    }
}

static int larch_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_LARCH_B16:
        case R_LARCH_B21:
        case R_LARCH_B26:
        case R_LARCH_PCALA_HI20:
        case R_LARCH_PCALA_LO12:
        case R_LARCH_PCALA64_LO20:
        case R_LARCH_PCALA64_HI12:
        case R_LARCH_GOT_PC_HI20:
        case R_LARCH_GOT_PC_LO12:
        case R_LARCH_GOT64_PC_LO20:
        case R_LARCH_GOT64_PC_HI12:
        case R_LARCH_TLS_IE_PC_HI20:
        case R_LARCH_TLS_IE_PC_LO12:
        case R_LARCH_TLS_IE64_PC_LO20:
        case R_LARCH_TLS_IE64_PC_HI12:
        case R_LARCH_TLS_DESC_PC_HI20:
        case R_LARCH_TLS_DESC_PC_LO12:
        case R_LARCH_TLS_DESC64_PC_LO20:
        case R_LARCH_TLS_DESC64_PC_HI12:
            return 1;
        default:
            return 0;
    }
}

static int larch_is_tls(uint32_t type) {
    switch (type) {
        case R_LARCH_TLS_DTPMOD32:
        case R_LARCH_TLS_DTPMOD64:
        case R_LARCH_TLS_DTPREL32:
        case R_LARCH_TLS_DTPREL64:
        case R_LARCH_TLS_TPREL32:
        case R_LARCH_TLS_TPREL64:
        case R_LARCH_TLS_LE_HI20:
        case R_LARCH_TLS_LE_LO12:
        case R_LARCH_TLS_LE64_LO20:
        case R_LARCH_TLS_LE64_HI12:
        case R_LARCH_TLS_IE_PC_HI20:
        case R_LARCH_TLS_IE_PC_LO12:
        case R_LARCH_TLS_IE64_PC_LO20:
        case R_LARCH_TLS_IE64_PC_HI12:
        case R_LARCH_TLS_LD_PC_HI20:
        case R_LARCH_TLS_GD_PC_HI20:
        case R_LARCH_TLS_DESC_PC_HI20:
        case R_LARCH_TLS_DESC_PC_LO12:
        case R_LARCH_TLS_DESC64_PC_LO20:
        case R_LARCH_TLS_DESC64_PC_HI12:
        case R_LARCH_TLS_DESC_HI20:
        case R_LARCH_TLS_DESC_LO12:
        case R_LARCH_TLS_DESC64_LO20:
        case R_LARCH_TLS_DESC64_HI12:
        case R_LARCH_TLS_DESC_LD:
        case R_LARCH_TLS_DESC_CALL:
        case R_LARCH_TLS_LE_HI20_R:
        case R_LARCH_TLS_LE_ADD_R:
        case R_LARCH_TLS_LE_LO12_R:
            return 1;
        default:
            return 0;
    }
}

static int larch_apply(const elfobj_reloc_ctx_t *ctx,
                       uint32_t type,
                       uint64_t place,
                       uint64_t sym_value,
                       int64_t addend,
                       uint64_t *out_value) {
    elf_swide_t v;
    (void)ctx;
    switch (type) {
        case R_LARCH_NONE:
        case R_LARCH_RELAX:
            *out_value = 0;
            return 0;
        case R_LARCH_32:
        case R_LARCH_RELATIVE:
        case R_LARCH_COPY:
        case R_LARCH_JUMP_SLOT:
        case R_LARCH_ADD32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_LARCH_64:
        case R_LARCH_IRELATIVE:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            *out_value = swide_to_width(v, 64);
            return 0;
        case R_LARCH_ABS_HI20:
        case R_LARCH_PCALA_HI20:
        case R_LARCH_GOT_PC_HI20:
        case R_LARCH_TLS_LE_HI20:
        case R_LARCH_TLS_IE_PC_HI20:
        case R_LARCH_TLS_LD_PC_HI20:
        case R_LARCH_TLS_GD_PC_HI20:
        case R_LARCH_TLS_DESC_PC_HI20:
        case R_LARCH_TLS_DESC_HI20:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (larch_is_pc_relative(type)) {
                v -= (elf_swide_t)place;
            }
            *out_value = swide_to_width((v + 0x800) >> 12, 20);
            return 0;
        case R_LARCH_ABS_LO12:
        case R_LARCH_PCALA_LO12:
        case R_LARCH_GOT_PC_LO12:
        case R_LARCH_TLS_LE_LO12:
        case R_LARCH_TLS_IE_PC_LO12:
        case R_LARCH_TLS_DESC_PC_LO12:
        case R_LARCH_TLS_DESC_LO12:
        case R_LARCH_TLS_LE_LO12_R:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (larch_is_pc_relative(type)) {
                v -= (elf_swide_t)place;
            }
            *out_value = swide_to_width(v, 12);
            return 0;
        case R_LARCH_B16:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place) >> 2;
            if (!swide_in_signed_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_LARCH_B21:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place) >> 2;
            if (!swide_in_signed_bits(v, 21)) {
                return -2;
            }
            *out_value = swide_to_width(v, 21);
            return 0;
        case R_LARCH_B26:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place) >> 2;
            if (!swide_in_signed_bits(v, 26)) {
                return -2;
            }
            *out_value = swide_to_width(v, 26);
            return 0;
        case R_LARCH_ADD6:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 6)) {
                return -2;
            }
            *out_value = swide_to_width(v, 6);
            return 0;
        case R_LARCH_ADD8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        case R_LARCH_ADD16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_LARCH_SUB6:
            v = (elf_swide_t)sym_value - (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 6)) {
                return -2;
            }
            *out_value = swide_to_width(v, 6);
            return 0;
        case R_LARCH_SUB8:
            v = (elf_swide_t)sym_value - (elf_swide_t)addend;
            *out_value = swide_to_width(v, 8);
            return 0;
        case R_LARCH_SUB16:
            v = (elf_swide_t)sym_value - (elf_swide_t)addend;
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_LARCH_SUB32:
            v = (elf_swide_t)sym_value - (elf_swide_t)addend;
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_LARCH_SUB64:
            v = (elf_swide_t)sym_value - (elf_swide_t)addend;
            *out_value = swide_to_width(v, 64);
            return 0;
        default:
            if (larch_is_tls(type)) {
                v = (elf_swide_t)sym_value + (elf_swide_t)addend;
                *out_value = swide_to_width(v, 64);
                return 0;
            }
            return -1;
    }
}

static int m68k_reloc_size(uint32_t type) {
    switch (type) {
        case R_68K_NONE:
            return 0;
        case R_68K_16:
        case R_68K_PC16:
        case R_68K_GOT16:
        case R_68K_GOT16O:
        case R_68K_PLT16:
        case R_68K_PLT16O:
        case R_68K_TLS_GD16:
        case R_68K_TLS_LDM16:
        case R_68K_TLS_LDO16:
        case R_68K_TLS_IE16:
        case R_68K_TLS_LE16:
            return 2;
        case R_68K_8:
        case R_68K_PC8:
        case R_68K_GOT8:
        case R_68K_GOT8O:
        case R_68K_PLT8:
        case R_68K_PLT8O:
        case R_68K_TLS_GD8:
        case R_68K_TLS_LDM8:
        case R_68K_TLS_LDO8:
        case R_68K_TLS_IE8:
        case R_68K_TLS_LE8:
            return 1;
        default:
            return 4;
    }
}

static int m68k_is_pc_relative(uint32_t type) {
    return type == R_68K_PC32 || type == R_68K_PC16 || type == R_68K_PC8;
}

static int m68k_is_tls(uint32_t type) {
    switch (type) {
        case R_68K_TLS_GD32:
        case R_68K_TLS_GD16:
        case R_68K_TLS_GD8:
        case R_68K_TLS_LDM32:
        case R_68K_TLS_LDM16:
        case R_68K_TLS_LDM8:
        case R_68K_TLS_LDO32:
        case R_68K_TLS_LDO16:
        case R_68K_TLS_LDO8:
        case R_68K_TLS_IE32:
        case R_68K_TLS_IE16:
        case R_68K_TLS_IE8:
        case R_68K_TLS_LE32:
        case R_68K_TLS_LE16:
        case R_68K_TLS_LE8:
        case R_68K_TLS_DTPMOD32:
        case R_68K_TLS_DTPREL32:
        case R_68K_TLS_TPREL32:
            return 1;
        default:
            return 0;
    }
}

static int m68k_apply(const elfobj_reloc_ctx_t *ctx,
                      uint32_t type,
                      uint64_t place,
                      uint64_t sym_value,
                      int64_t addend,
                      uint64_t *out_value) {
    elf_swide_t v;
    (void)ctx;
    switch (type) {
        case R_68K_NONE:
            *out_value = 0;
            return 0;
        case R_68K_32:
        case R_68K_GOT32:
        case R_68K_GOT32O:
        case R_68K_PLT32:
        case R_68K_PLT32O:
        case R_68K_COPY:
        case R_68K_GLOB_DAT:
        case R_68K_JMP_SLOT:
        case R_68K_RELATIVE:
        case R_68K_TLS_GD32:
        case R_68K_TLS_LDM32:
        case R_68K_TLS_LDO32:
        case R_68K_TLS_IE32:
        case R_68K_TLS_LE32:
        case R_68K_TLS_DTPMOD32:
        case R_68K_TLS_DTPREL32:
        case R_68K_TLS_TPREL32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_68K_PC32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_68K_16:
        case R_68K_GOT16:
        case R_68K_GOT16O:
        case R_68K_PLT16:
        case R_68K_PLT16O:
        case R_68K_TLS_GD16:
        case R_68K_TLS_LDM16:
        case R_68K_TLS_LDO16:
        case R_68K_TLS_IE16:
        case R_68K_TLS_LE16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 16) && !swide_in_unsigned_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_68K_PC16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_68K_8:
        case R_68K_GOT8:
        case R_68K_GOT8O:
        case R_68K_PLT8:
        case R_68K_PLT8O:
        case R_68K_TLS_GD8:
        case R_68K_TLS_LDM8:
        case R_68K_TLS_LDO8:
        case R_68K_TLS_IE8:
        case R_68K_TLS_LE8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        case R_68K_PC8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        default:
            if (m68k_is_tls(type)) {
                v = (elf_swide_t)sym_value + (elf_swide_t)addend;
                *out_value = swide_to_width(v, 32);
                return 0;
            }
            return -1;
    }
}

static int vax_reloc_size(uint32_t type) {
    switch (type) {
        case R_VAX_NONE:
            return 0;
        case R_VAX_16:
        case R_VAX_PC16:
            return 2;
        case R_VAX_8:
        case R_VAX_PC8:
            return 1;
        default:
            return 4;
    }
}

static int vax_is_pc_relative(uint32_t type) {
    return type == R_VAX_PC32 || type == R_VAX_PC16 || type == R_VAX_PC8;
}

static int vax_apply(const elfobj_reloc_ctx_t *ctx,
                     uint32_t type,
                     uint64_t place,
                     uint64_t sym_value,
                     int64_t addend,
                     uint64_t *out_value) {
    elf_swide_t v;
    (void)ctx;
    switch (type) {
        case R_VAX_NONE:
            *out_value = 0;
            return 0;
        case R_VAX_32:
        case R_VAX_GOT32:
        case R_VAX_PLT32:
        case R_VAX_COPY:
        case R_VAX_GLOB_DAT:
        case R_VAX_JMP_SLOT:
        case R_VAX_RELATIVE:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_VAX_PC32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_VAX_16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 16) && !swide_in_unsigned_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_VAX_PC16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_VAX_8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        case R_VAX_PC8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        default:
            return -1;
    }
}

static int ppc_reloc_size(uint32_t type) {
    switch (type) {
        case R_PPC_NONE:
            return 0;
        case R_PPC_ADDR16:
        case R_PPC_ADDR16_LO:
        case R_PPC_ADDR16_HI:
        case R_PPC_ADDR16_HA:
        case R_PPC_ADDR14:
        case R_PPC_ADDR14_BRTAKEN:
        case R_PPC_ADDR14_BRNTAKEN:
        case R_PPC_REL14:
        case R_PPC_REL14_BRTAKEN:
        case R_PPC_REL14_BRNTAKEN:
        case R_PPC_GOT16:
        case R_PPC_GOT16_LO:
        case R_PPC_GOT16_HI:
        case R_PPC_GOT16_HA:
        case R_PPC_PLT16_LO:
        case R_PPC_PLT16_HI:
        case R_PPC_PLT16_HA:
        case R_PPC_UADDR16:
        case R_PPC_SDAREL16:
        case R_PPC_SECTOFF_LO:
        case R_PPC_SECTOFF_HI:
        case R_PPC_SECTOFF_HA:
        case R_PPC_TPREL16:
        case R_PPC_TPREL16_LO:
        case R_PPC_TPREL16_HI:
        case R_PPC_TPREL16_HA:
        case R_PPC_DTPREL16:
        case R_PPC_DTPREL16_LO:
        case R_PPC_DTPREL16_HI:
        case R_PPC_DTPREL16_HA:
        case R_PPC_GOT_TLSGD16:
        case R_PPC_GOT_TLSGD16_LO:
        case R_PPC_GOT_TLSGD16_HI:
        case R_PPC_GOT_TLSGD16_HA:
        case R_PPC_GOT_TLSLD16:
        case R_PPC_GOT_TLSLD16_LO:
        case R_PPC_GOT_TLSLD16_HI:
        case R_PPC_GOT_TLSLD16_HA:
        case R_PPC_GOT_TPREL16:
        case R_PPC_GOT_TPREL16_LO:
        case R_PPC_GOT_TPREL16_HI:
        case R_PPC_GOT_TPREL16_HA:
            return 2;
        default:
            return 4;
    }
}

static int ppc64_reloc_size(uint32_t type) {
    switch (type) {
        case R_PPC64_NONE:
            return 0;
        case R_PPC64_ADDR64:
        case R_PPC64_UADDR64:
        case R_PPC64_REL64:
        case R_PPC64_PLT64:
        case R_PPC64_PLTREL64:
        case R_PPC64_TOC:
        case R_PPC64_DTPMOD64:
        case R_PPC64_TPREL64:
        case R_PPC64_DTPREL64:
            return 8;
        case R_PPC64_ADDR16_HIGHER:
        case R_PPC64_ADDR16_HIGHERA:
        case R_PPC64_ADDR16_HIGHEST:
        case R_PPC64_ADDR16_HIGHESTA:
        case R_PPC64_TOC16:
        case R_PPC64_TOC16_LO:
        case R_PPC64_TOC16_HI:
        case R_PPC64_TOC16_HA:
        case R_PPC64_ADDR16_DS:
        case R_PPC64_ADDR16_LO_DS:
        case R_PPC64_GOT16_DS:
        case R_PPC64_GOT16_LO_DS:
        case R_PPC64_PLT16_LO_DS:
        case R_PPC64_SECTOFF_DS:
        case R_PPC64_SECTOFF_LO_DS:
        case R_PPC64_TOC16_DS:
        case R_PPC64_TOC16_LO_DS:
        case R_PPC64_GOT_TLSGD16:
        case R_PPC64_GOT_TLSGD16_LO:
        case R_PPC64_GOT_TLSGD16_HI:
        case R_PPC64_GOT_TLSGD16_HA:
        case R_PPC64_GOT_TLSLD16:
        case R_PPC64_GOT_TLSLD16_LO:
        case R_PPC64_GOT_TLSLD16_HI:
        case R_PPC64_GOT_TLSLD16_HA:
        case R_PPC64_GOT_TPREL16_DS:
        case R_PPC64_GOT_TPREL16_LO_DS:
        case R_PPC64_GOT_TPREL16_HI:
        case R_PPC64_GOT_TPREL16_HA:
        case R_PPC64_TPREL16_DS:
        case R_PPC64_TPREL16_LO_DS:
        case R_PPC64_TPREL16_HIGHER:
        case R_PPC64_TPREL16_HIGHERA:
        case R_PPC64_TPREL16_HIGHEST:
        case R_PPC64_TPREL16_HIGHESTA:
        case R_PPC64_DTPREL16_DS:
        case R_PPC64_DTPREL16_LO_DS:
        case R_PPC64_DTPREL16_HIGHER:
        case R_PPC64_DTPREL16_HIGHERA:
        case R_PPC64_DTPREL16_HIGHEST:
        case R_PPC64_DTPREL16_HIGHESTA:
            return 2;
        default:
            return 4;
    }
}

static int ppc_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_PPC_REL24:
        case R_PPC_REL14:
        case R_PPC_REL14_BRTAKEN:
        case R_PPC_REL14_BRNTAKEN:
        case R_PPC_REL32:
        case R_PPC_LOCAL24PC:
        case R_PPC_PLTREL24:
        case R_PPC_PLTREL32:
            return 1;
        default:
            return 0;
    }
}

static int ppc64_is_pc_relative(uint32_t type) {
    if (ppc_is_pc_relative(type)) {
        return 1;
    }
    switch (type) {
        case R_PPC64_REL64:
        case R_PPC64_PCREL34:
        case R_PPC64_GOT_PCREL34:
        case R_PPC64_PLT_PCREL34:
            return 1;
        default:
            return 0;
    }
}

static int ppc_is_tls(uint32_t type) {
    return type >= R_PPC_TLS && type <= R_PPC_GOT_TPREL16_HA;
}

static int ppc64_is_tls(uint32_t type) {
    return (type >= R_PPC64_TLS && type <= R_PPC64_DTPREL64) ||
           (type >= R_PPC64_GOT_TLSGD16 && type <= R_PPC64_TPREL16_HIGHESTA) ||
           (type >= R_PPC64_DTPREL16_DS && type <= R_PPC64_DTPREL16_HIGHESTA);
}

static int ppc_apply(const elfobj_reloc_ctx_t *ctx,
                     uint32_t type,
                     uint64_t place,
                     uint64_t sym_value,
                     int64_t addend,
                     uint64_t *out_value) {
    elf_swide_t v = (elf_swide_t)sym_value + (elf_swide_t)addend;
    elf_swide_t rel = v - (elf_swide_t)place;
    (void)ctx;
    switch (type) {
        case R_PPC_NONE:
            *out_value = 0;
            return 0;
        case R_PPC_ADDR16_HI:
        case R_PPC_GOT16_HI:
        case R_PPC_PLT16_HI:
        case R_PPC_SECTOFF_HI:
        case R_PPC_TPREL16_HI:
        case R_PPC_DTPREL16_HI:
        case R_PPC_GOT_TLSGD16_HI:
        case R_PPC_GOT_TLSLD16_HI:
        case R_PPC_GOT_TPREL16_HI:
            *out_value = (uint64_t)(((uint64_t)v >> 16) & 0xffffu);
            return 0;
        case R_PPC_ADDR16_HA:
        case R_PPC_GOT16_HA:
        case R_PPC_PLT16_HA:
        case R_PPC_SECTOFF_HA:
        case R_PPC_TPREL16_HA:
        case R_PPC_DTPREL16_HA:
        case R_PPC_GOT_TLSGD16_HA:
        case R_PPC_GOT_TLSLD16_HA:
        case R_PPC_GOT_TPREL16_HA:
            *out_value = (uint64_t)((((uint64_t)v + 0x8000u) >> 16) & 0xffffu);
            return 0;
        case R_PPC_REL24:
        case R_PPC_PLTREL24:
        case R_PPC_LOCAL24PC:
            if ((rel & 3) != 0 || !swide_in_signed_bits(rel, 26)) {
                return -2;
            }
            *out_value = (uint64_t)((uint64_t)(rel >> 2) & 0x00ffffffu);
            return 0;
        case R_PPC_REL14:
        case R_PPC_REL14_BRTAKEN:
        case R_PPC_REL14_BRNTAKEN:
            if ((rel & 3) != 0 || !swide_in_signed_bits(rel, 16)) {
                return -2;
            }
            *out_value = (uint64_t)((uint64_t)(rel >> 2) & 0x00003fffu);
            return 0;
        case R_PPC_REL32:
        case R_PPC_PLTREL32:
            if (!swide_in_signed_bits(rel, 32)) {
                return -2;
            }
            *out_value = swide_to_width(rel, 32);
            return 0;
        default:
            if (ppc_reloc_size(type) == 2) {
                *out_value = swide_to_width(v, 16);
                return 0;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
    }
}

static int ppc64_apply(const elfobj_reloc_ctx_t *ctx,
                       uint32_t type,
                       uint64_t place,
                       uint64_t sym_value,
                       int64_t addend,
                       uint64_t *out_value) {
    elf_swide_t v = (elf_swide_t)sym_value + (elf_swide_t)addend;
    elf_swide_t rel = v - (elf_swide_t)place;
    elf_swide_t toc_rel = v;
    (void)ctx;
    switch (type) {
        case R_PPC64_NONE:
            *out_value = 0;
            return 0;
        case R_PPC64_ADDR16_HIGHER:
        case R_PPC64_TPREL16_HIGHER:
        case R_PPC64_DTPREL16_HIGHER:
            *out_value = (uint64_t)(((uint64_t)v >> 32) & 0xffffu);
            return 0;
        case R_PPC64_ADDR16_HIGHERA:
        case R_PPC64_TPREL16_HIGHERA:
        case R_PPC64_DTPREL16_HIGHERA:
            *out_value = (uint64_t)((((uint64_t)v + 0x80000000ull) >> 32) & 0xffffu);
            return 0;
        case R_PPC64_ADDR16_HIGHEST:
        case R_PPC64_TPREL16_HIGHEST:
        case R_PPC64_DTPREL16_HIGHEST:
            *out_value = (uint64_t)(((uint64_t)v >> 48) & 0xffffu);
            return 0;
        case R_PPC64_ADDR16_HIGHESTA:
        case R_PPC64_TPREL16_HIGHESTA:
        case R_PPC64_DTPREL16_HIGHESTA:
            *out_value = (uint64_t)((((uint64_t)v + 0x800000000000ull) >> 48) & 0xffffu);
            return 0;
        case R_PPC64_TOC16:
        case R_PPC64_TOC16_LO:
        case R_PPC64_TOC16_HI:
        case R_PPC64_TOC16_HA:
        case R_PPC64_TOC16_DS:
        case R_PPC64_TOC16_LO_DS:
            if (type == R_PPC64_TOC16_HI) {
                *out_value = (uint64_t)(((uint64_t)toc_rel >> 16) & 0xffffu);
                return 0;
            }
            if (type == R_PPC64_TOC16_HA) {
                *out_value = (uint64_t)((((uint64_t)toc_rel + 0x8000u) >> 16) & 0xffffu);
                return 0;
            }
            if (type == R_PPC64_TOC16_DS || type == R_PPC64_TOC16_LO_DS) {
                if (((uint64_t)toc_rel & 3u) != 0) {
                    return -2;
                }
                *out_value = (uint64_t)((uint64_t)toc_rel & 0xfffcu);
                return 0;
            }
            *out_value = (uint64_t)((uint64_t)toc_rel & 0xffffu);
            return 0;
        case R_PPC64_ADDR16_DS:
        case R_PPC64_ADDR16_LO_DS:
        case R_PPC64_GOT16_DS:
        case R_PPC64_GOT16_LO_DS:
        case R_PPC64_PLT16_LO_DS:
        case R_PPC64_SECTOFF_DS:
        case R_PPC64_SECTOFF_LO_DS:
        case R_PPC64_GOT_TPREL16_DS:
        case R_PPC64_GOT_TPREL16_LO_DS:
        case R_PPC64_TPREL16_DS:
        case R_PPC64_TPREL16_LO_DS:
        case R_PPC64_DTPREL16_DS:
        case R_PPC64_DTPREL16_LO_DS:
            if (((uint64_t)v & 3u) != 0) {
                return -2;
            }
            *out_value = (uint64_t)((uint64_t)v & 0xfffcu);
            return 0;
        case R_PPC64_REL64:
        case R_PPC64_PLTREL64:
            *out_value = swide_to_width(rel, 64);
            return 0;
        case R_PPC64_PCREL34:
        case R_PPC64_GOT_PCREL34:
        case R_PPC64_PLT_PCREL34:
            if (!swide_in_signed_bits(rel, 34)) {
                return -2;
            }
            *out_value = swide_to_width(rel, 34);
            return 0;
        case R_PPC64_ENTRY:
            *out_value = swide_to_width(v, 32);
            return 0;
        default:
            if (ppc64_reloc_size(type) == 2) {
                *out_value = swide_to_width(v, 16);
            } else if (ppc64_reloc_size(type) == 8) {
                *out_value = swide_to_width(v, 64);
            } else {
                *out_value = swide_to_width(v, 32);
            }
            return 0;
    }
}

static int alpha_reloc_size(uint32_t type) {
    switch (type) {
        case R_ALPHA_NONE:
            return 0;
        case R_ALPHA_REFQUAD:
        case R_ALPHA_SREL64:
        case R_ALPHA_DTPMOD64:
        case R_ALPHA_DTPREL64:
        case R_ALPHA_TPREL64:
            return 8;
        case R_ALPHA_SREL16:
        case R_ALPHA_GPREL16:
        case R_ALPHA_DTPREL16:
        case R_ALPHA_TPREL16:
            return 2;
        default:
            return 4;
    }
}

static int alpha_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_ALPHA_BRADDR:
        case R_ALPHA_HINT:
        case R_ALPHA_SREL16:
        case R_ALPHA_SREL32:
        case R_ALPHA_SREL64:
            return 1;
        default:
            return 0;
    }
}

static int alpha_is_tls(uint32_t type) {
    switch (type) {
        case R_ALPHA_TLSGD:
        case R_ALPHA_TLSLDM:
        case R_ALPHA_DTPMOD64:
        case R_ALPHA_GOTDTPREL:
        case R_ALPHA_DTPREL64:
        case R_ALPHA_DTPRELHI:
        case R_ALPHA_DTPRELLO:
        case R_ALPHA_DTPREL16:
        case R_ALPHA_GOTTPREL:
        case R_ALPHA_TPREL64:
        case R_ALPHA_TPRELHI:
        case R_ALPHA_TPRELLO:
        case R_ALPHA_TPREL16:
            return 1;
        default:
            return 0;
    }
}

static int alpha_apply(const elfobj_reloc_ctx_t *ctx,
                       uint32_t type,
                       uint64_t place,
                       uint64_t sym_value,
                       int64_t addend,
                       uint64_t *out_value) {
    elf_swide_t v = (elf_swide_t)sym_value + (elf_swide_t)addend;
    elf_swide_t rel = v - (elf_swide_t)place;
    (void)ctx;
    switch (type) {
        case R_ALPHA_NONE:
            *out_value = 0;
            return 0;
        case R_ALPHA_REFLONG:
        case R_ALPHA_GPREL32:
        case R_ALPHA_LITERAL:
        case R_ALPHA_LITUSE:
        case R_ALPHA_GPDISP:
        case R_ALPHA_COPY:
        case R_ALPHA_GLOB_DAT:
        case R_ALPHA_JMP_SLOT:
        case R_ALPHA_RELATIVE:
        case R_ALPHA_TLSGD:
        case R_ALPHA_TLSLDM:
        case R_ALPHA_GOTDTPREL:
        case R_ALPHA_DTPRELHI:
        case R_ALPHA_DTPRELLO:
        case R_ALPHA_GOTTPREL:
        case R_ALPHA_TPRELHI:
        case R_ALPHA_TPRELLO:
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_ALPHA_REFQUAD:
            *out_value = swide_to_width(v, 64);
            return 0;
        case R_ALPHA_BRADDR:
            if ((rel & 3) != 0 || !swide_in_signed_bits(rel, 23)) {
                return -2;
            }
            *out_value = (uint64_t)((uint64_t)(rel >> 2) & 0x001fffffu);
            return 0;
        case R_ALPHA_HINT:
            if (!swide_in_signed_bits(rel, 16)) {
                return -2;
            }
            *out_value = (uint64_t)((uint64_t)(rel >> 2) & 0x3fffu);
            return 0;
        case R_ALPHA_SREL16:
            if (!swide_in_signed_bits(rel, 16)) {
                return -2;
            }
            *out_value = swide_to_width(rel, 16);
            return 0;
        case R_ALPHA_SREL32:
            if (!swide_in_signed_bits(rel, 32)) {
                return -2;
            }
            *out_value = swide_to_width(rel, 32);
            return 0;
        case R_ALPHA_SREL64:
            *out_value = swide_to_width(rel, 64);
            return 0;
        case R_ALPHA_GPRELHIGH:
            *out_value = (uint64_t)(((uint64_t)v >> 16) & 0xffffu);
            return 0;
        case R_ALPHA_GPRELLOW:
            *out_value = (uint64_t)((uint64_t)v & 0xffffu);
            return 0;
        case R_ALPHA_GPREL16:
        case R_ALPHA_DTPREL16:
        case R_ALPHA_TPREL16:
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_ALPHA_DTPMOD64:
        case R_ALPHA_DTPREL64:
        case R_ALPHA_TPREL64:
            *out_value = swide_to_width(v, 64);
            return 0;
        default:
            if (alpha_is_tls(type)) {
                *out_value = swide_to_width(v, 64);
                return 0;
            }
            return -1;
    }
}

static int arm_reloc_size(uint32_t type) {
    switch (type) {
        case R_ARM_NONE:
            return 0;
        case R_ARM_ABS16:
            return 2;
        case R_ARM_ABS8:
            return 1;
        case R_ARM_THM_JUMP11:
        case R_ARM_THM_JUMP8:
        case R_ARM_THM_JUMP6:
            return 2;
        case R_ARM_ABS32:
        case R_ARM_REL32:
        case R_ARM_GOTOFF32:
        case R_ARM_GOTPC:
        case R_ARM_GOT_BREL:
        case R_ARM_PLT32:
        case R_ARM_CALL:
        case R_ARM_JUMP24:
        case R_ARM_TARGET1:
        case R_ARM_TARGET2:
        case R_ARM_PREL31:
        case R_ARM_MOVW_ABS_NC:
        case R_ARM_MOVT_ABS:
        case R_ARM_MOVW_PREL_NC:
        case R_ARM_MOVT_PREL:
        case R_ARM_ABS32_NOI:
        case R_ARM_REL32_NOI:
        case R_ARM_PC24:
        case R_ARM_ABS12:
        case R_ARM_THM_CALL:
        case R_ARM_THM_JUMP24:
        case R_ARM_THM_MOVW_ABS_NC:
        case R_ARM_THM_MOVT_ABS:
        case R_ARM_THM_MOVW_PREL_NC:
        case R_ARM_THM_MOVT_PREL:
        case R_ARM_THM_JUMP19:
        case R_ARM_COPY:
        case R_ARM_GLOB_DAT:
        case R_ARM_JUMP_SLOT:
        case R_ARM_RELATIVE:
        case R_ARM_IRELATIVE:
        case R_ARM_V4BX:
        case R_ARM_TLS_DTPMOD32:
        case R_ARM_TLS_DTPOFF32:
        case R_ARM_TLS_TPOFF32:
        case R_ARM_TLS_GD32:
        case R_ARM_TLS_LDM32:
        case R_ARM_TLS_LDO32:
        case R_ARM_TLS_IE32:
        case R_ARM_TLS_LE32:
        case R_ARM_TLS_LDO12:
        case R_ARM_TLS_LE12:
        case R_ARM_TLS_IE12GP:
        case R_ARM_TLS_DESC:
        case R_ARM_TLS_GOTDESC:
        case R_ARM_TLS_CALL:
        case R_ARM_TLS_DESCSEQ:
        case R_ARM_THM_TLS_CALL:
        case R_ARM_ALU_PC_G0_NC:
        case R_ARM_ALU_PC_G0:
        case R_ARM_ALU_PC_G1_NC:
        case R_ARM_ALU_PC_G1:
        case R_ARM_ALU_PC_G2:
        case R_ARM_LDR_PC_G0:
        case R_ARM_LDR_PC_G1:
        case R_ARM_LDR_PC_G2:
        case R_ARM_LDRS_PC_G0:
        case R_ARM_LDRS_PC_G1:
        case R_ARM_LDRS_PC_G2:
        case R_ARM_LDC_PC_G0:
        case R_ARM_LDC_PC_G1:
        case R_ARM_LDC_PC_G2:
        case R_ARM_ALU_SB_G0_NC:
        case R_ARM_ALU_SB_G0:
        case R_ARM_ALU_SB_G1_NC:
        case R_ARM_ALU_SB_G1:
        case R_ARM_ALU_SB_G2:
        case R_ARM_LDR_SB_G0:
        case R_ARM_LDR_SB_G1:
        case R_ARM_LDR_SB_G2:
        case R_ARM_LDRS_SB_G0:
        case R_ARM_LDRS_SB_G1:
        case R_ARM_LDRS_SB_G2:
        case R_ARM_LDC_SB_G0:
        case R_ARM_LDC_SB_G1:
        case R_ARM_LDC_SB_G2:
            return 4;
        default:
            return -1;
    }
}

static int arm_is_pc_group(uint32_t type) {
    switch (type) {
        case R_ARM_ALU_PC_G0_NC:
        case R_ARM_ALU_PC_G0:
        case R_ARM_ALU_PC_G1_NC:
        case R_ARM_ALU_PC_G1:
        case R_ARM_ALU_PC_G2:
        case R_ARM_LDR_PC_G0:
        case R_ARM_LDR_PC_G1:
        case R_ARM_LDR_PC_G2:
        case R_ARM_LDRS_PC_G0:
        case R_ARM_LDRS_PC_G1:
        case R_ARM_LDRS_PC_G2:
        case R_ARM_LDC_PC_G0:
        case R_ARM_LDC_PC_G1:
        case R_ARM_LDC_PC_G2:
            return 1;
        default:
            return 0;
    }
}

static int arm_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_ARM_PC24:
        case R_ARM_REL32:
        case R_ARM_PLT32:
        case R_ARM_CALL:
        case R_ARM_JUMP24:
        case R_ARM_THM_CALL:
        case R_ARM_THM_JUMP24:
        case R_ARM_THM_JUMP19:
        case R_ARM_THM_JUMP11:
        case R_ARM_THM_JUMP8:
        case R_ARM_PREL31:
        case R_ARM_MOVW_PREL_NC:
        case R_ARM_MOVT_PREL:
        case R_ARM_THM_MOVW_PREL_NC:
        case R_ARM_THM_MOVT_PREL:
        case R_ARM_GOTPC:
        case R_ARM_GOT_PREL:
        case R_ARM_REL32_NOI:
            return 1;
        default:
            return arm_is_pc_group(type);
    }
}

static int arm_is_tls(uint32_t type) {
    switch (type) {
        case R_ARM_TLS_DTPMOD32:
        case R_ARM_TLS_DTPOFF32:
        case R_ARM_TLS_TPOFF32:
        case R_ARM_TLS_GD32:
        case R_ARM_TLS_LDM32:
        case R_ARM_TLS_LDO32:
        case R_ARM_TLS_IE32:
        case R_ARM_TLS_LE32:
        case R_ARM_TLS_LDO12:
        case R_ARM_TLS_LE12:
        case R_ARM_TLS_IE12GP:
        case R_ARM_TLS_DESC:
        case R_ARM_TLS_GOTDESC:
        case R_ARM_TLS_CALL:
        case R_ARM_TLS_DESCSEQ:
        case R_ARM_THM_TLS_CALL:
            return 1;
        default:
            return 0;
    }
}

static int arm_apply(const elfobj_reloc_ctx_t *ctx,
                     uint32_t type,
                     uint64_t place,
                     uint64_t sym_value,
                     int64_t addend,
                     uint64_t *out_value) {
    elf_swide_t v;
    uint64_t thumb_t;
    (void)ctx;

    thumb_t = (sym_value & 1u) ? 1u : 0u;
    switch (type) {
        case R_ARM_NONE:
            *out_value = 0;
            return 0;
        case R_ARM_ABS32:
        case R_ARM_ABS32_NOI:
        case R_ARM_COPY:
        case R_ARM_GLOB_DAT:
        case R_ARM_JUMP_SLOT:
        case R_ARM_RELATIVE:
        case R_ARM_IRELATIVE:
        case R_ARM_TLS_DTPMOD32:
        case R_ARM_TLS_DTPOFF32:
        case R_ARM_TLS_TPOFF32:
        case R_ARM_TLS_GD32:
        case R_ARM_TLS_LDM32:
        case R_ARM_TLS_LDO32:
        case R_ARM_TLS_IE32:
        case R_ARM_TLS_LE32:
        case R_ARM_TLS_LDO12:
        case R_ARM_TLS_LE12:
        case R_ARM_TLS_IE12GP:
        case R_ARM_TLS_DESC:
        case R_ARM_TLS_GOTDESC:
        case R_ARM_TLS_CALL:
        case R_ARM_TLS_DESCSEQ:
        case R_ARM_THM_TLS_CALL:
        case R_ARM_SBREL32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_ARM_REL32:
        case R_ARM_REL32_NOI:
        case R_ARM_PLT32:
        case R_ARM_GOTPC:
        case R_ARM_GOT_PREL:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_ARM_GOTOFF32:
        case R_ARM_GOT_BREL:
        case R_ARM_TARGET2:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_ARM_PC24:
        case R_ARM_CALL:
        case R_ARM_JUMP24: {
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend + (elf_swide_t)thumb_t) -
                (elf_swide_t)place;
            if ((v & 3) != 0 || !swide_in_signed_bits(v >> 2, 24)) {
                return -2;
            }
            *out_value = (uint64_t)((uint32_t)((v >> 2) & 0x00ffffff));
            return 0;
        }
        case R_ARM_THM_CALL:
        case R_ARM_THM_JUMP24: {
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend + (elf_swide_t)thumb_t) -
                (elf_swide_t)place;
            if ((v & 1) != 0 || !swide_in_signed_bits(v >> 1, 24)) {
                return -2;
            }
            *out_value = (uint64_t)((uint32_t)((v >> 1) & 0x00ffffff));
            return 0;
        }
        case R_ARM_THM_JUMP19:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if ((v & 1) != 0 || !swide_in_signed_bits(v >> 1, 20)) {
                return -2;
            }
            *out_value = (uint64_t)((uint32_t)((v >> 1) & 0x000fffff));
            return 0;
        case R_ARM_THM_JUMP11:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if ((v & 1) != 0 || !swide_in_signed_bits(v >> 1, 11)) {
                return -2;
            }
            *out_value = (uint64_t)((uint32_t)((v >> 1) & 0x7ff));
            return 0;
        case R_ARM_THM_JUMP8:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if ((v & 1) != 0 || !swide_in_signed_bits(v >> 1, 8)) {
                return -2;
            }
            *out_value = (uint64_t)((uint32_t)((v >> 1) & 0xff));
            return 0;
        case R_ARM_PREL31:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 31)) {
                return -2;
            }
            *out_value = (uint64_t)((uint32_t)v & 0x7fffffffU);
            return 0;
        case R_ARM_MOVW_ABS_NC:
        case R_ARM_THM_MOVW_ABS_NC:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            *out_value = (uint64_t)((uint32_t)v & 0xffffu);
            return 0;
        case R_ARM_MOVT_ABS:
        case R_ARM_THM_MOVT_ABS:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            *out_value = (uint64_t)(((uint64_t)v >> 16) & 0xffffu);
            return 0;
        case R_ARM_MOVW_PREL_NC:
        case R_ARM_THM_MOVW_PREL_NC:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend + (elf_swide_t)thumb_t) -
                (elf_swide_t)place;
            *out_value = (uint64_t)((uint32_t)v & 0xffffu);
            return 0;
        case R_ARM_MOVT_PREL:
        case R_ARM_THM_MOVT_PREL:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend + (elf_swide_t)thumb_t) -
                (elf_swide_t)place;
            *out_value = (uint64_t)(((uint64_t)v >> 16) & 0xffffu);
            return 0;
        case R_ARM_TARGET1:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_ARM_ABS16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 16) && !swide_in_signed_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_ARM_ABS12:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 12)) {
                return -2;
            }
            *out_value = swide_to_width(v, 12);
            return 0;
        case R_ARM_ABS8:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 8)) {
                return -2;
            }
            *out_value = swide_to_width(v, 8);
            return 0;
        case R_ARM_V4BX:
            *out_value = 0x01a0f000u | (sym_value & 0xfu);
            return 0;
        default:
            if (arm_is_pc_group(type)) {
                v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
                if (!swide_in_signed_bits(v, 32)) {
                    return -2;
                }
                *out_value = swide_to_width(v, 32);
                return 0;
            }
            if (type == R_ARM_ALU_SB_G0_NC || type == R_ARM_ALU_SB_G0 ||
                type == R_ARM_ALU_SB_G1_NC || type == R_ARM_ALU_SB_G1 ||
                type == R_ARM_ALU_SB_G2 || type == R_ARM_LDR_SB_G0 ||
                type == R_ARM_LDR_SB_G1 || type == R_ARM_LDR_SB_G2 ||
                type == R_ARM_LDRS_SB_G0 || type == R_ARM_LDRS_SB_G1 ||
                type == R_ARM_LDRS_SB_G2 || type == R_ARM_LDC_SB_G0 ||
                type == R_ARM_LDC_SB_G1 || type == R_ARM_LDC_SB_G2) {
                v = (elf_swide_t)sym_value + (elf_swide_t)addend;
                if (!swide_in_signed_bits(v, 32)) {
                    return -2;
                }
                *out_value = swide_to_width(v, 32);
                return 0;
            }
            return -1;
    }
}

static uint32_t aarch64_extract_imm26(uint32_t insn) {
    return insn & 0x03ffffffu;
}

static uint32_t aarch64_insert_imm26(uint32_t insn, int32_t value) {
    return (insn & ~0x03ffffffu) | ((uint32_t)value & 0x03ffffffu);
}

static uint32_t aarch64_extract_imm19(uint32_t insn) {
    return (insn >> 5) & 0x7ffffu;
}

static uint32_t aarch64_insert_imm19(uint32_t insn, int32_t value) {
    return (insn & ~(0x7ffffu << 5)) | (((uint32_t)value & 0x7ffffu) << 5);
}

static uint32_t aarch64_extract_imm14(uint32_t insn) {
    return (insn >> 5) & 0x3fffu;
}

static uint32_t aarch64_insert_imm14(uint32_t insn, int32_t value) {
    return (insn & ~(0x3fffu << 5)) | (((uint32_t)value & 0x3fffu) << 5);
}

static int32_t aarch64_extract_adr_imm(uint32_t insn) {
    uint32_t immlo = (insn >> 29) & 0x3u;
    uint32_t immhi = (insn >> 5) & 0x7ffffu;
    return (int32_t)((immhi << 2) | immlo);
}

static uint32_t aarch64_insert_adr_imm(uint32_t insn, int32_t value) {
    uint32_t immlo = (uint32_t)value & 0x3u;
    uint32_t immhi = ((uint32_t)value >> 2) & 0x7ffffu;
    uint32_t out = insn;
    out &= ~((0x7ffffu << 5) | (0x3u << 29));
    out |= (immhi << 5);
    out |= (immlo << 29);
    return out;
}

static uint32_t aarch64_extract_imm12(uint32_t insn) {
    return (insn >> 10) & 0xfffu;
}

static uint32_t aarch64_insert_imm12(uint32_t insn, uint32_t value) {
    return (insn & ~(0xfffu << 10)) | ((value & 0xfffu) << 10);
}

static uint16_t aarch64_extract_movw_imm16(uint32_t insn) {
    return (uint16_t)((insn >> 5) & 0xffffu);
}

static uint32_t aarch64_insert_movw_imm16(uint32_t insn, uint16_t value) {
    return (insn & ~(0xffffu << 5)) | (((uint32_t)value & 0xffffu) << 5);
}

static uint64_t aarch64_page(uint64_t addr) {
    return addr & ~0xfffu;
}

static int aarch64_reloc_size(uint32_t type) {
    switch (type) {
        case R_AARCH64_NONE:
            return 0;
        case R_AARCH64_ABS64:
        case R_AARCH64_PREL64:
        case R_AARCH64_COPY:
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
        case R_AARCH64_RELATIVE:
        case R_AARCH64_TLS_DTPMOD64:
        case R_AARCH64_TLS_DTPREL64:
        case R_AARCH64_TLS_TPREL64:
        case R_AARCH64_TLSDESC:
        case R_AARCH64_IRELATIVE:
            return 8;
        case R_AARCH64_ABS32:
        case R_AARCH64_PREL32:
            return 4;
        case R_AARCH64_ABS16:
        case R_AARCH64_PREL16:
            return 2;
        default:
            if (type >= R_AARCH64_MOVW_UABS_G0 && type <= R_AARCH64_TLSDESC_CALL) {
                return 4;
            }
            return -1;
    }
}

static int aarch64_is_pc_relative(uint32_t type) {
    switch (type) {
        case R_AARCH64_PREL64:
        case R_AARCH64_PREL32:
        case R_AARCH64_PREL16:
        case R_AARCH64_ADR_PREL_LO21:
        case R_AARCH64_ADR_PREL_PG_HI21:
        case R_AARCH64_ADR_PREL_PG_HI21_NC:
        case R_AARCH64_JUMP26:
        case R_AARCH64_CALL26:
        case R_AARCH64_CONDBR19:
        case R_AARCH64_TSTBR14:
        case R_AARCH64_LD_PREL_LO19:
        case R_AARCH64_GOT_LD_PREL19:
        case R_AARCH64_ADR_GOT_PAGE:
        case R_AARCH64_MOVW_PREL_G0:
        case R_AARCH64_MOVW_PREL_G0_NC:
        case R_AARCH64_MOVW_PREL_G1:
        case R_AARCH64_MOVW_PREL_G1_NC:
        case R_AARCH64_MOVW_PREL_G2:
        case R_AARCH64_MOVW_PREL_G2_NC:
        case R_AARCH64_MOVW_PREL_G3:
        case R_AARCH64_TLSGD_ADR_PREL21:
        case R_AARCH64_TLSLD_ADR_PREL21:
        case R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
        case R_AARCH64_TLSDESC_LD_PREL19:
        case R_AARCH64_TLSDESC_ADR_PREL21:
            return 1;
        default:
            return 0;
    }
}

static int aarch64_is_tls(uint32_t type) {
    if (type >= R_AARCH64_TLSGD_ADR_PREL21 && type <= R_AARCH64_TLSDESC) {
        return 1;
    }
    switch (type) {
        case R_AARCH64_TLS_DTPMOD64:
        case R_AARCH64_TLS_DTPREL64:
        case R_AARCH64_TLS_TPREL64:
            return 1;
        default:
            return 0;
    }
}

static int aarch64_apply(const elfobj_reloc_ctx_t *ctx,
                         uint32_t type,
                         uint64_t place,
                         uint64_t sym_value,
                         int64_t addend,
                         uint64_t *out_value) {
    elf_swide_t v;
    (void)ctx;

    switch (type) {
        case R_AARCH64_NONE:
            *out_value = 0;
            return 0;
        case R_AARCH64_ABS64:
        case R_AARCH64_COPY:
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
        case R_AARCH64_RELATIVE:
        case R_AARCH64_TLS_DTPMOD64:
        case R_AARCH64_TLS_DTPREL64:
        case R_AARCH64_TLS_TPREL64:
        case R_AARCH64_TLSDESC:
        case R_AARCH64_IRELATIVE:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            *out_value = swide_to_width(v, 64);
            return 0;
        case R_AARCH64_ABS32:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_unsigned_bits(v, 32) && !swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_AARCH64_ABS16:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            if (!swide_in_signed_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_AARCH64_PREL64:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            *out_value = swide_to_width(v, 64);
            return 0;
        case R_AARCH64_PREL32:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 32)) {
                return -2;
            }
            *out_value = swide_to_width(v, 32);
            return 0;
        case R_AARCH64_PREL16:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 16)) {
                return -2;
            }
            *out_value = swide_to_width(v, 16);
            return 0;
        case R_AARCH64_ADR_PREL_LO21:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if (!swide_in_signed_bits(v, 21)) {
                return -2;
            }
            *out_value = (uint64_t)aarch64_extract_adr_imm(aarch64_insert_adr_imm(0, (int32_t)v));
            return 0;
        case R_AARCH64_ADR_PREL_PG_HI21:
        case R_AARCH64_ADR_PREL_PG_HI21_NC:
            v = (elf_swide_t)aarch64_page(sym_value + (uint64_t)addend) -
                (elf_swide_t)aarch64_page(place);
            if (type == R_AARCH64_ADR_PREL_PG_HI21 && !swide_in_signed_bits(v, 33)) {
                return -2;
            }
            *out_value = (uint64_t)aarch64_extract_adr_imm(
                aarch64_insert_adr_imm(0, (int32_t)(v >> 12)));
            return 0;
        case R_AARCH64_ADD_ABS_LO12_NC:
        case R_AARCH64_LDST8_ABS_LO12_NC:
            *out_value = (uint64_t)aarch64_extract_imm12(
                aarch64_insert_imm12(0, (uint32_t)((sym_value + (uint64_t)addend) & 0xfffu)));
            return 0;
        case R_AARCH64_LDST16_ABS_LO12_NC:
        case R_AARCH64_LDST32_ABS_LO12_NC:
        case R_AARCH64_LDST64_ABS_LO12_NC:
        case R_AARCH64_LDST128_ABS_LO12_NC: {
            uint64_t addr = sym_value + (uint64_t)addend;
            uint32_t shift = (type == R_AARCH64_LDST16_ABS_LO12_NC) ? 1 :
                             (type == R_AARCH64_LDST32_ABS_LO12_NC) ? 2 :
                             (type == R_AARCH64_LDST64_ABS_LO12_NC) ? 3 : 4;
            if ((addr & (((uint64_t)1 << shift) - 1)) != 0) {
                return -2;
            }
            *out_value = (uint64_t)aarch64_extract_imm12(
                aarch64_insert_imm12(0, (uint32_t)((addr & 0xfffu) >> shift)));
            return 0;
        }
        case R_AARCH64_MOVW_UABS_G0:
        case R_AARCH64_MOVW_UABS_G0_NC:
            *out_value = (sym_value + (uint64_t)addend) & 0xffffu;
            return 0;
        case R_AARCH64_MOVW_UABS_G1:
        case R_AARCH64_MOVW_UABS_G1_NC:
            *out_value = ((sym_value + (uint64_t)addend) >> 16) & 0xffffu;
            return 0;
        case R_AARCH64_MOVW_UABS_G2:
        case R_AARCH64_MOVW_UABS_G2_NC:
            *out_value = ((sym_value + (uint64_t)addend) >> 32) & 0xffffu;
            return 0;
        case R_AARCH64_MOVW_UABS_G3:
            *out_value = ((sym_value + (uint64_t)addend) >> 48) & 0xffffu;
            return 0;
        case R_AARCH64_MOVW_SABS_G0:
        case R_AARCH64_MOVW_SABS_G1:
        case R_AARCH64_MOVW_SABS_G2:
            v = (elf_swide_t)sym_value + (elf_swide_t)addend;
            *out_value = (uint64_t)((uint64_t)v >>
                                    (type == R_AARCH64_MOVW_SABS_G0 ? 0 :
                                     type == R_AARCH64_MOVW_SABS_G1 ? 16 : 32)) &
                         0xffffu;
            return 0;
        case R_AARCH64_JUMP26:
        case R_AARCH64_CALL26:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if ((v & 3) != 0 || !swide_in_signed_bits(v >> 2, 26)) {
                return -2;
            }
            *out_value = (uint64_t)aarch64_extract_imm26(
                aarch64_insert_imm26(0, (int32_t)(v >> 2)));
            return 0;
        case R_AARCH64_CONDBR19:
        case R_AARCH64_LD_PREL_LO19:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if ((v & 3) != 0 || !swide_in_signed_bits(v >> 2, 19)) {
                return -2;
            }
            *out_value = (uint64_t)aarch64_extract_imm19(
                aarch64_insert_imm19(0, (int32_t)(v >> 2)));
            return 0;
        case R_AARCH64_TSTBR14:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            if ((v & 3) != 0 || !swide_in_signed_bits(v >> 2, 14)) {
                return -2;
            }
            *out_value = (uint64_t)aarch64_extract_imm14(
                aarch64_insert_imm14(0, (int32_t)(v >> 2)));
            return 0;
        case R_AARCH64_MOVW_PREL_G0:
        case R_AARCH64_MOVW_PREL_G0_NC:
        case R_AARCH64_MOVW_PREL_G1:
        case R_AARCH64_MOVW_PREL_G1_NC:
        case R_AARCH64_MOVW_PREL_G2:
        case R_AARCH64_MOVW_PREL_G2_NC:
        case R_AARCH64_MOVW_PREL_G3:
            v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
            *out_value = (uint64_t)aarch64_extract_movw_imm16(aarch64_insert_movw_imm16(
                0,
                (uint16_t)(((uint64_t)v >>
                            (type == R_AARCH64_MOVW_PREL_G0 || type == R_AARCH64_MOVW_PREL_G0_NC
                                 ? 0
                                 : type == R_AARCH64_MOVW_PREL_G1 ||
                                           type == R_AARCH64_MOVW_PREL_G1_NC
                                       ? 16
                                       : type == R_AARCH64_MOVW_PREL_G2 ||
                                                 type == R_AARCH64_MOVW_PREL_G2_NC
                                             ? 32
                                             : 48)) &
                           0xffffu)));
            return 0;
        default:
            if (aarch64_is_tls(type)) {
                *out_value = sym_value + (uint64_t)addend;
                return 0;
            }
            if (type == R_AARCH64_GOT_LD_PREL19 || type == R_AARCH64_ADR_GOT_PAGE ||
                type == R_AARCH64_LD64_GOT_LO12_NC || type == R_AARCH64_LD64_GOTPAGE_LO15) {
                v = ((elf_swide_t)sym_value + (elf_swide_t)addend) - (elf_swide_t)place;
                *out_value = swide_to_width(v, 64);
                return 0;
            }
            return -1;
    }
}

static void register_builtin_backends_locked(void) {
    struct elf_reloc_backend b;

    if (g_builtin_backends_ready) {
        return;
    }

    memset(&b, 0, sizeof(b));
    b.machine = EM_386;
    b.apply_reloc = i386_apply;
    b.reloc_size = i386_reloc_size;
    b.is_pc_relative = i386_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_X86_64;
    b.apply_reloc = x64_apply;
    b.reloc_size = x64_reloc_size;
    b.is_pc_relative = x64_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_ARM;
    b.apply_reloc = arm_apply;
    b.reloc_size = arm_reloc_size;
    b.is_pc_relative = arm_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_AARCH64;
    b.apply_reloc = aarch64_apply;
    b.reloc_size = aarch64_reloc_size;
    b.is_pc_relative = aarch64_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_MIPS;
    b.apply_reloc = mips_apply;
    b.reloc_size = mips_reloc_size;
    b.is_pc_relative = mips_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_RISCV;
    b.apply_reloc = riscv_apply;
    b.reloc_size = riscv_reloc_size;
    b.is_pc_relative = riscv_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_LOONGARCH;
    b.apply_reloc = larch_apply;
    b.reloc_size = larch_reloc_size;
    b.is_pc_relative = larch_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_68K;
    b.apply_reloc = m68k_apply;
    b.reloc_size = m68k_reloc_size;
    b.is_pc_relative = m68k_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_VAX;
    b.apply_reloc = vax_apply;
    b.reloc_size = vax_reloc_size;
    b.is_pc_relative = vax_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_PPC;
    b.apply_reloc = ppc_apply;
    b.reloc_size = ppc_reloc_size;
    b.is_pc_relative = ppc_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_PPC64;
    b.apply_reloc = ppc64_apply;
    b.reloc_size = ppc64_reloc_size;
    b.is_pc_relative = ppc64_is_pc_relative;
    g_backends[g_backend_count++] = b;

    memset(&b, 0, sizeof(b));
    b.machine = EM_ALPHA;
    b.apply_reloc = alpha_apply;
    b.reloc_size = alpha_reloc_size;
    b.is_pc_relative = alpha_is_pc_relative;
    g_backends[g_backend_count++] = b;

    g_builtin_backends_ready = 1;
}

static const struct elf_reloc_backend *find_backend(uint32_t machine) {
    size_t i;
    const struct elf_reloc_backend *ret = NULL;

    backend_lock();
    register_builtin_backends_locked();
    for (i = 0; i < g_backend_count; ++i) {
        if (g_backends[i].machine == machine) {
            ret = &g_backends[i];
            break;
        }
    }
    backend_unlock();
    return ret;
}

elf_err_t elf__push_reloc(elfobj_t *obj, struct elf_reloc *rel) {
    void *next;

    if (obj->reloc_count == obj->reloc_cap) {
        size_t new_cap = obj->reloc_cap == 0 ? 16 : obj->reloc_cap * 2;
        next = elf__reallocarray(obj->relocs, new_cap, sizeof(obj->relocs[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        obj->relocs = (struct elf_reloc **)next;
        obj->reloc_cap = new_cap;
    }
    obj->relocs[obj->reloc_count++] = rel;
    return ELF_OK;
}

elf_err_t elf__section_push_reloc(struct elf_section *section, struct elf_reloc *rel) {
    void *next;

    if (section->reloc_count == section->reloc_cap) {
        size_t new_cap = section->reloc_cap == 0 ? 8 : section->reloc_cap * 2;
        next = elf__reallocarray(section->relocs, new_cap, sizeof(section->relocs[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        section->relocs = (struct elf_reloc **)next;
        section->reloc_cap = new_cap;
    }
    section->relocs[section->reloc_count++] = rel;
    return ELF_OK;
}

elf_err_t elf_add_relocation(elf_section_t *section, uint64_t offset, elf_symbol_t *symbol,
                             uint32_t type, int64_t addend) {
    struct elf_reloc *rel;
    elf_err_t err;

    if (section == NULL || section->obj == NULL || symbol == NULL) {
        return ELF_ERR_STATE;
    }
    if (section->obj != symbol->obj) {
        return ELF_ERR_STATE;
    }
    if (section->obj->readonly || section->obj->finalized) {
        elf__set_err(section->obj, ELF_ERR_STATE, "cannot mutate finalized/read-only object");
        return ELF_ERR_STATE;
    }

    rel = (struct elf_reloc *)elf__calloc(1, sizeof(*rel));
    if (rel == NULL) {
        elf__set_err(section->obj, ELF_ERR_OOM, "alloc relocation failed");
        return ELF_ERR_OOM;
    }

    rel->section = section;
    rel->offset = offset;
    rel->symbol = symbol;
    rel->type = type;
    rel->addend = addend;
    rel->has_addend = section->obj->cls == ELFOBJ_CLASS_64 ? 1 : 0;

    err = elf__section_push_reloc(section, rel);
    if (err != ELF_OK) {
        free(rel);
        return err;
    }
    err = elf__push_reloc(section->obj, rel);
    if (err != ELF_OK) {
        section->reloc_count--;
        free(rel);
        return err;
    }
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_register_reloc_backend(const struct elf_reloc_backend *backend) {
    size_t i;

    if (backend == NULL || backend->apply_reloc == NULL) {
        return ELF_ERR_STATE;
    }

    backend_lock();
    register_builtin_backends_locked();
    for (i = 0; i < g_backend_count; ++i) {
        if (g_backends[i].machine == backend->machine) {
            g_backends[i] = *backend;
            backend_unlock();
            return ELF_OK;
        }
    }

    if (g_backend_count >= ELFOBJ_MAX_RELOC_BACKENDS) {
        backend_unlock();
        return ELF_ERR_OOM;
    }
    g_backends[g_backend_count++] = *backend;
    backend_unlock();
    return ELF_OK;
}

size_t elf_section_reloc_count(const elf_section_t *section) {
    if (section == NULL) {
        return 0;
    }
    if (section->obj != NULL) {
        (void)elf__ensure_symbols_relocs(section->obj);
    }
    return section->reloc_count;
}

elf_reloc_t *elf_section_reloc_at(elf_section_t *section, size_t index) {
    if (section == NULL) {
        return NULL;
    }
    if (section->obj != NULL) {
        (void)elf__ensure_symbols_relocs(section->obj);
    }
    if (index >= section->reloc_count) {
        return NULL;
    }
    return section->relocs[index];
}

elf_reloc_t *elf_reloc_at(elfobj_t *obj, size_t index) {
    if (obj == NULL) {
        return NULL;
    }
    (void)elf__ensure_symbols_relocs(obj);
    if (index >= obj->reloc_count) {
        return NULL;
    }
    return obj->relocs[index];
}

uint64_t elf_reloc_offset(const elf_reloc_t *reloc) {
    return reloc == NULL ? 0 : reloc->offset;
}

uint32_t elf_reloc_type(const elf_reloc_t *reloc) {
    return reloc == NULL ? 0 : reloc->type;
}

int64_t elf_reloc_addend(const elf_reloc_t *reloc) {
    return reloc == NULL ? 0 : reloc->addend;
}

int elf_reloc_has_addend(const elf_reloc_t *reloc) {
    return reloc == NULL ? 0 : reloc->has_addend != 0;
}

elf_symbol_t *elf_reloc_symbol(const elf_reloc_t *reloc) {
    return reloc == NULL ? NULL : reloc->symbol;
}

elf_section_t *elf_reloc_section(const elf_reloc_t *reloc) {
    return reloc == NULL ? NULL : reloc->section;
}

elf_err_t elf_set_reloc_hooks(elfobj_t *obj, const elf_reloc_hooks_t *hooks, void *user) {
    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (hooks == NULL) {
        memset(&obj->reloc_hooks, 0, sizeof(obj->reloc_hooks));
        obj->reloc_hook_user = NULL;
    } else {
        obj->reloc_hooks = *hooks;
        obj->reloc_hook_user = user;
    }
    return ELF_OK;
}

int elf_reloc_size_for_machine(uint16_t machine, uint32_t type) {
    const struct elf_reloc_backend *backend = find_backend(machine);
    if (backend == NULL || backend->reloc_size == NULL) {
        return -1;
    }
    return backend->reloc_size(type);
}

int elf_reloc_is_pc_relative_for_machine(uint16_t machine, uint32_t type) {
    const struct elf_reloc_backend *backend = find_backend(machine);
    if (backend == NULL || backend->is_pc_relative == NULL) {
        return 0;
    }
    return backend->is_pc_relative(type);
}

int elf_reloc_is_tls_for_machine(uint16_t machine, uint32_t type) {
    if (machine == EM_386) {
        return i386_is_tls(type);
    }
    if (machine == EM_X86_64) {
        return x64_is_tls(type);
    }
    if (machine == EM_ARM) {
        return arm_is_tls(type);
    }
    if (machine == EM_AARCH64) {
        return aarch64_is_tls(type);
    }
    if (machine == EM_MIPS) {
        return mips_is_tls(type);
    }
    if (machine == EM_RISCV) {
        return riscv_is_tls(type);
    }
    if (machine == EM_LOONGARCH) {
        return larch_is_tls(type);
    }
    if (machine == EM_68K) {
        return m68k_is_tls(type);
    }
    if (machine == EM_PPC) {
        return ppc_is_tls(type);
    }
    if (machine == EM_PPC64) {
        return ppc64_is_tls(type);
    }
    if (machine == EM_ALPHA) {
        return alpha_is_tls(type);
    }
    return 0;
}

const char *elf_reloc_name_for_machine(uint16_t machine, uint32_t type) {
    static char unknown[32];
    switch (machine) {
        case EM_386:
            switch (type) {
                case R_386_NONE: return "R_386_NONE";
                case R_386_32: return "R_386_32";
                case R_386_PC32: return "R_386_PC32";
                case R_386_GOT32: return "R_386_GOT32";
                case R_386_PLT32: return "R_386_PLT32";
                case R_386_COPY: return "R_386_COPY";
                case R_386_GLOB_DAT: return "R_386_GLOB_DAT";
                case R_386_JMP_SLOT: return "R_386_JMP_SLOT";
                case R_386_RELATIVE: return "R_386_RELATIVE";
                case R_386_GOTOFF: return "R_386_GOTOFF";
                case R_386_GOTPC: return "R_386_GOTPC";
                case R_386_16: return "R_386_16";
                case R_386_PC16: return "R_386_PC16";
                case R_386_8: return "R_386_8";
                case R_386_PC8: return "R_386_PC8";
                case R_386_TLS_TPOFF: return "R_386_TLS_TPOFF";
                case R_386_TLS_IE: return "R_386_TLS_IE";
                case R_386_TLS_GOTIE: return "R_386_TLS_GOTIE";
                case R_386_TLS_LE: return "R_386_TLS_LE";
                case R_386_TLS_GD: return "R_386_TLS_GD";
                case R_386_TLS_LDM: return "R_386_TLS_LDM";
                case R_386_TLS_LDO_32: return "R_386_TLS_LDO_32";
                case R_386_TLS_LE_32: return "R_386_TLS_LE_32";
                case R_386_TLS_DTPMOD32: return "R_386_TLS_DTPMOD32";
                case R_386_TLS_DTPOFF32: return "R_386_TLS_DTPOFF32";
                case R_386_TLS_TPOFF32: return "R_386_TLS_TPOFF32";
                case R_386_SIZE32: return "R_386_SIZE32";
                case R_386_IRELATIVE: return "R_386_IRELATIVE";
                case R_386_GOT32X: return "R_386_GOT32X";
                default: break;
            }
            break;
        case EM_X86_64:
            switch (type) {
                case R_X86_64_NONE: return "R_X86_64_NONE";
                case R_X86_64_64: return "R_X86_64_64";
                case R_X86_64_PC32: return "R_X86_64_PC32";
                case R_X86_64_GOT32: return "R_X86_64_GOT32";
                case R_X86_64_PLT32: return "R_X86_64_PLT32";
                case R_X86_64_COPY: return "R_X86_64_COPY";
                case R_X86_64_GLOB_DAT: return "R_X86_64_GLOB_DAT";
                case R_X86_64_JUMP_SLOT: return "R_X86_64_JUMP_SLOT";
                case R_X86_64_RELATIVE: return "R_X86_64_RELATIVE";
                case R_X86_64_GOTPCREL: return "R_X86_64_GOTPCREL";
                case R_X86_64_32: return "R_X86_64_32";
                case R_X86_64_32S: return "R_X86_64_32S";
                case R_X86_64_16: return "R_X86_64_16";
                case R_X86_64_PC16: return "R_X86_64_PC16";
                case R_X86_64_8: return "R_X86_64_8";
                case R_X86_64_PC8: return "R_X86_64_PC8";
                case R_X86_64_DTPMOD64: return "R_X86_64_DTPMOD64";
                case R_X86_64_DTPOFF64: return "R_X86_64_DTPOFF64";
                case R_X86_64_TPOFF64: return "R_X86_64_TPOFF64";
                case R_X86_64_TLSGD: return "R_X86_64_TLSGD";
                case R_X86_64_TLSLD: return "R_X86_64_TLSLD";
                case R_X86_64_DTPOFF32: return "R_X86_64_DTPOFF32";
                case R_X86_64_GOTTPOFF: return "R_X86_64_GOTTPOFF";
                case R_X86_64_TPOFF32: return "R_X86_64_TPOFF32";
                case R_X86_64_PC64: return "R_X86_64_PC64";
                case R_X86_64_GOTOFF64: return "R_X86_64_GOTOFF64";
                case R_X86_64_GOTPC32: return "R_X86_64_GOTPC32";
                case R_X86_64_SIZE32: return "R_X86_64_SIZE32";
                case R_X86_64_SIZE64: return "R_X86_64_SIZE64";
                case R_X86_64_GOTPC32_TLSDESC: return "R_X86_64_GOTPC32_TLSDESC";
                case R_X86_64_TLSDESC_CALL: return "R_X86_64_TLSDESC_CALL";
                case R_X86_64_TLSDESC: return "R_X86_64_TLSDESC";
                case R_X86_64_IRELATIVE: return "R_X86_64_IRELATIVE";
                case R_X86_64_GOTPCRELX: return "R_X86_64_GOTPCRELX";
                case R_X86_64_REX_GOTPCRELX: return "R_X86_64_REX_GOTPCRELX";
                default: break;
            }
            break;
        case EM_ARM:
            if (type <= 255) {
                return "R_ARM_*";
            }
            break;
        case EM_MIPS:
            switch (type) {
                case R_MIPS_NONE: return "R_MIPS_NONE";
                case R_MIPS_16: return "R_MIPS_16";
                case R_MIPS_32: return "R_MIPS_32";
                case R_MIPS_REL32: return "R_MIPS_REL32";
                case R_MIPS_26: return "R_MIPS_26";
                case R_MIPS_HI16: return "R_MIPS_HI16";
                case R_MIPS_LO16: return "R_MIPS_LO16";
                case R_MIPS_GPREL16: return "R_MIPS_GPREL16";
                case R_MIPS_LITERAL: return "R_MIPS_LITERAL";
                case R_MIPS_GOT16: return "R_MIPS_GOT16";
                case R_MIPS_PC16: return "R_MIPS_PC16";
                case R_MIPS_CALL16: return "R_MIPS_CALL16";
                case R_MIPS_GPREL32: return "R_MIPS_GPREL32";
                case R_MIPS_64: return "R_MIPS_64";
                case R_MIPS_GOT_DISP: return "R_MIPS_GOT_DISP";
                case R_MIPS_GOT_PAGE: return "R_MIPS_GOT_PAGE";
                case R_MIPS_GOT_OFST: return "R_MIPS_GOT_OFST";
                case R_MIPS_GOT_HI16: return "R_MIPS_GOT_HI16";
                case R_MIPS_GOT_LO16: return "R_MIPS_GOT_LO16";
                case R_MIPS_SUB: return "R_MIPS_SUB";
                case R_MIPS_HIGHER: return "R_MIPS_HIGHER";
                case R_MIPS_HIGHEST: return "R_MIPS_HIGHEST";
                case R_MIPS_CALL_HI16: return "R_MIPS_CALL_HI16";
                case R_MIPS_CALL_LO16: return "R_MIPS_CALL_LO16";
                case R_MIPS_REL16: return "R_MIPS_REL16";
                case R_MIPS_PJUMP: return "R_MIPS_PJUMP";
                case R_MIPS_RELGOT: return "R_MIPS_RELGOT";
                case R_MIPS_JALR: return "R_MIPS_JALR";
                case R_MIPS_TLS_DTPMOD32: return "R_MIPS_TLS_DTPMOD32";
                case R_MIPS_TLS_DTPREL32: return "R_MIPS_TLS_DTPREL32";
                case R_MIPS_TLS_DTPMOD64: return "R_MIPS_TLS_DTPMOD64";
                case R_MIPS_TLS_DTPREL64: return "R_MIPS_TLS_DTPREL64";
                case R_MIPS_TLS_GD: return "R_MIPS_TLS_GD";
                case R_MIPS_TLS_LDM: return "R_MIPS_TLS_LDM";
                case R_MIPS_TLS_DTPREL_HI16: return "R_MIPS_TLS_DTPREL_HI16";
                case R_MIPS_TLS_DTPREL_LO16: return "R_MIPS_TLS_DTPREL_LO16";
                case R_MIPS_TLS_GOTTPREL: return "R_MIPS_TLS_GOTTPREL";
                case R_MIPS_TLS_TPREL32: return "R_MIPS_TLS_TPREL32";
                case R_MIPS_TLS_TPREL64: return "R_MIPS_TLS_TPREL64";
                case R_MIPS_TLS_TPREL_HI16: return "R_MIPS_TLS_TPREL_HI16";
                case R_MIPS_TLS_TPREL_LO16: return "R_MIPS_TLS_TPREL_LO16";
                case R_MIPS_GLOB_DAT: return "R_MIPS_GLOB_DAT";
                case R_MIPS_COPY: return "R_MIPS_COPY";
                case R_MIPS_JUMP_SLOT: return "R_MIPS_JUMP_SLOT";
                case R_MICROMIPS_26_S1: return "R_MICROMIPS_26_S1";
                case R_MICROMIPS_HI16: return "R_MICROMIPS_HI16";
                case R_MICROMIPS_LO16: return "R_MICROMIPS_LO16";
                case R_MICROMIPS_GPREL16: return "R_MICROMIPS_GPREL16";
                case R_MICROMIPS_PC7_S1: return "R_MICROMIPS_PC7_S1";
                case R_MICROMIPS_PC10_S1: return "R_MICROMIPS_PC10_S1";
                case R_MICROMIPS_PC16_S1: return "R_MICROMIPS_PC16_S1";
                case R_MICROMIPS_PC23_S2: return "R_MICROMIPS_PC23_S2";
                default: break;
            }
            break;
        case EM_RISCV:
            switch (type) {
                case R_RISCV_NONE: return "R_RISCV_NONE";
                case R_RISCV_32: return "R_RISCV_32";
                case R_RISCV_64: return "R_RISCV_64";
                case R_RISCV_RELATIVE: return "R_RISCV_RELATIVE";
                case R_RISCV_COPY: return "R_RISCV_COPY";
                case R_RISCV_JUMP_SLOT: return "R_RISCV_JUMP_SLOT";
                case R_RISCV_BRANCH: return "R_RISCV_BRANCH";
                case R_RISCV_JAL: return "R_RISCV_JAL";
                case R_RISCV_CALL: return "R_RISCV_CALL";
                case R_RISCV_CALL_PLT: return "R_RISCV_CALL_PLT";
                case R_RISCV_GOT_HI20: return "R_RISCV_GOT_HI20";
                case R_RISCV_PCREL_HI20: return "R_RISCV_PCREL_HI20";
                case R_RISCV_PCREL_LO12_I: return "R_RISCV_PCREL_LO12_I";
                case R_RISCV_PCREL_LO12_S: return "R_RISCV_PCREL_LO12_S";
                case R_RISCV_HI20: return "R_RISCV_HI20";
                case R_RISCV_LO12_I: return "R_RISCV_LO12_I";
                case R_RISCV_LO12_S: return "R_RISCV_LO12_S";
                case R_RISCV_RVC_BRANCH: return "R_RISCV_RVC_BRANCH";
                case R_RISCV_RVC_JUMP: return "R_RISCV_RVC_JUMP";
                case R_RISCV_RELAX: return "R_RISCV_RELAX";
                case R_RISCV_32_PCREL: return "R_RISCV_32_PCREL";
                case R_RISCV_IRELATIVE: return "R_RISCV_IRELATIVE";
                case R_RISCV_TLS_DTPMOD32: return "R_RISCV_TLS_DTPMOD32";
                case R_RISCV_TLS_DTPMOD64: return "R_RISCV_TLS_DTPMOD64";
                case R_RISCV_TLS_DTPREL32: return "R_RISCV_TLS_DTPREL32";
                case R_RISCV_TLS_DTPREL64: return "R_RISCV_TLS_DTPREL64";
                case R_RISCV_TLS_TPREL32: return "R_RISCV_TLS_TPREL32";
                case R_RISCV_TLS_TPREL64: return "R_RISCV_TLS_TPREL64";
                case R_RISCV_TLS_GOT_HI20: return "R_RISCV_TLS_GOT_HI20";
                case R_RISCV_TLS_GD_HI20: return "R_RISCV_TLS_GD_HI20";
                case R_RISCV_TPREL_HI20: return "R_RISCV_TPREL_HI20";
                case R_RISCV_TPREL_LO12_I: return "R_RISCV_TPREL_LO12_I";
                case R_RISCV_TPREL_LO12_S: return "R_RISCV_TPREL_LO12_S";
                case R_RISCV_TPREL_ADD: return "R_RISCV_TPREL_ADD";
                default: break;
            }
            break;
        case EM_LOONGARCH:
            switch (type) {
                case R_LARCH_NONE: return "R_LARCH_NONE";
                case R_LARCH_32: return "R_LARCH_32";
                case R_LARCH_64: return "R_LARCH_64";
                case R_LARCH_RELATIVE: return "R_LARCH_RELATIVE";
                case R_LARCH_COPY: return "R_LARCH_COPY";
                case R_LARCH_JUMP_SLOT: return "R_LARCH_JUMP_SLOT";
                case R_LARCH_IRELATIVE: return "R_LARCH_IRELATIVE";
                case R_LARCH_B16: return "R_LARCH_B16";
                case R_LARCH_B21: return "R_LARCH_B21";
                case R_LARCH_B26: return "R_LARCH_B26";
                case R_LARCH_ABS_HI20: return "R_LARCH_ABS_HI20";
                case R_LARCH_ABS_LO12: return "R_LARCH_ABS_LO12";
                case R_LARCH_PCALA_HI20: return "R_LARCH_PCALA_HI20";
                case R_LARCH_PCALA_LO12: return "R_LARCH_PCALA_LO12";
                case R_LARCH_GOT_PC_HI20: return "R_LARCH_GOT_PC_HI20";
                case R_LARCH_GOT_PC_LO12: return "R_LARCH_GOT_PC_LO12";
                case R_LARCH_TLS_LE_HI20: return "R_LARCH_TLS_LE_HI20";
                case R_LARCH_TLS_LE_LO12: return "R_LARCH_TLS_LE_LO12";
                case R_LARCH_TLS_IE_PC_HI20: return "R_LARCH_TLS_IE_PC_HI20";
                case R_LARCH_TLS_IE_PC_LO12: return "R_LARCH_TLS_IE_PC_LO12";
                case R_LARCH_TLS_DESC_PC_HI20: return "R_LARCH_TLS_DESC_PC_HI20";
                case R_LARCH_TLS_DESC_PC_LO12: return "R_LARCH_TLS_DESC_PC_LO12";
                case R_LARCH_TLS_DESC_CALL: return "R_LARCH_TLS_DESC_CALL";
                case R_LARCH_RELAX: return "R_LARCH_RELAX";
                case R_LARCH_ALIGN: return "R_LARCH_ALIGN";
                case R_LARCH_ADD6: return "R_LARCH_ADD6";
                case R_LARCH_ADD8: return "R_LARCH_ADD8";
                case R_LARCH_ADD16: return "R_LARCH_ADD16";
                case R_LARCH_ADD32: return "R_LARCH_ADD32";
                case R_LARCH_ADD64: return "R_LARCH_ADD64";
                case R_LARCH_SUB6: return "R_LARCH_SUB6";
                case R_LARCH_SUB8: return "R_LARCH_SUB8";
                case R_LARCH_SUB16: return "R_LARCH_SUB16";
                case R_LARCH_SUB32: return "R_LARCH_SUB32";
                case R_LARCH_SUB64: return "R_LARCH_SUB64";
                default: break;
            }
            break;
        case EM_68K:
            switch (type) {
                case R_68K_NONE: return "R_68K_NONE";
                case R_68K_32: return "R_68K_32";
                case R_68K_16: return "R_68K_16";
                case R_68K_8: return "R_68K_8";
                case R_68K_PC32: return "R_68K_PC32";
                case R_68K_PC16: return "R_68K_PC16";
                case R_68K_PC8: return "R_68K_PC8";
                case R_68K_GOT32: return "R_68K_GOT32";
                case R_68K_GOT16: return "R_68K_GOT16";
                case R_68K_GOT8: return "R_68K_GOT8";
                case R_68K_GOT32O: return "R_68K_GOT32O";
                case R_68K_GOT16O: return "R_68K_GOT16O";
                case R_68K_GOT8O: return "R_68K_GOT8O";
                case R_68K_PLT32: return "R_68K_PLT32";
                case R_68K_PLT16: return "R_68K_PLT16";
                case R_68K_PLT8: return "R_68K_PLT8";
                case R_68K_PLT32O: return "R_68K_PLT32O";
                case R_68K_PLT16O: return "R_68K_PLT16O";
                case R_68K_PLT8O: return "R_68K_PLT8O";
                case R_68K_COPY: return "R_68K_COPY";
                case R_68K_GLOB_DAT: return "R_68K_GLOB_DAT";
                case R_68K_JMP_SLOT: return "R_68K_JMP_SLOT";
                case R_68K_RELATIVE: return "R_68K_RELATIVE";
                case R_68K_TLS_GD32: return "R_68K_TLS_GD32";
                case R_68K_TLS_GD16: return "R_68K_TLS_GD16";
                case R_68K_TLS_GD8: return "R_68K_TLS_GD8";
                case R_68K_TLS_LDM32: return "R_68K_TLS_LDM32";
                case R_68K_TLS_LDM16: return "R_68K_TLS_LDM16";
                case R_68K_TLS_LDM8: return "R_68K_TLS_LDM8";
                case R_68K_TLS_LDO32: return "R_68K_TLS_LDO32";
                case R_68K_TLS_LDO16: return "R_68K_TLS_LDO16";
                case R_68K_TLS_LDO8: return "R_68K_TLS_LDO8";
                case R_68K_TLS_IE32: return "R_68K_TLS_IE32";
                case R_68K_TLS_IE16: return "R_68K_TLS_IE16";
                case R_68K_TLS_IE8: return "R_68K_TLS_IE8";
                case R_68K_TLS_LE32: return "R_68K_TLS_LE32";
                case R_68K_TLS_LE16: return "R_68K_TLS_LE16";
                case R_68K_TLS_LE8: return "R_68K_TLS_LE8";
                case R_68K_TLS_DTPMOD32: return "R_68K_TLS_DTPMOD32";
                case R_68K_TLS_DTPREL32: return "R_68K_TLS_DTPREL32";
                case R_68K_TLS_TPREL32: return "R_68K_TLS_TPREL32";
                default: break;
            }
            break;
        case EM_VAX:
            switch (type) {
                case R_VAX_NONE: return "R_VAX_NONE";
                case R_VAX_32: return "R_VAX_32";
                case R_VAX_16: return "R_VAX_16";
                case R_VAX_8: return "R_VAX_8";
                case R_VAX_PC32: return "R_VAX_PC32";
                case R_VAX_PC16: return "R_VAX_PC16";
                case R_VAX_PC8: return "R_VAX_PC8";
                case R_VAX_GOT32: return "R_VAX_GOT32";
                case R_VAX_PLT32: return "R_VAX_PLT32";
                case R_VAX_COPY: return "R_VAX_COPY";
                case R_VAX_GLOB_DAT: return "R_VAX_GLOB_DAT";
                case R_VAX_JMP_SLOT: return "R_VAX_JMP_SLOT";
                case R_VAX_RELATIVE: return "R_VAX_RELATIVE";
                default: break;
            }
            break;
        case EM_PPC:
            switch (type) {
                case R_PPC_NONE: return "R_PPC_NONE";
                case R_PPC_ADDR32: return "R_PPC_ADDR32";
                case R_PPC_ADDR24: return "R_PPC_ADDR24";
                case R_PPC_ADDR16: return "R_PPC_ADDR16";
                case R_PPC_ADDR16_LO: return "R_PPC_ADDR16_LO";
                case R_PPC_ADDR16_HI: return "R_PPC_ADDR16_HI";
                case R_PPC_ADDR16_HA: return "R_PPC_ADDR16_HA";
                case R_PPC_ADDR14: return "R_PPC_ADDR14";
                case R_PPC_ADDR14_BRTAKEN: return "R_PPC_ADDR14_BRTAKEN";
                case R_PPC_ADDR14_BRNTAKEN: return "R_PPC_ADDR14_BRNTAKEN";
                case R_PPC_REL24: return "R_PPC_REL24";
                case R_PPC_REL14: return "R_PPC_REL14";
                case R_PPC_REL14_BRTAKEN: return "R_PPC_REL14_BRTAKEN";
                case R_PPC_REL14_BRNTAKEN: return "R_PPC_REL14_BRNTAKEN";
                case R_PPC_GOT16: return "R_PPC_GOT16";
                case R_PPC_GOT16_LO: return "R_PPC_GOT16_LO";
                case R_PPC_GOT16_HI: return "R_PPC_GOT16_HI";
                case R_PPC_GOT16_HA: return "R_PPC_GOT16_HA";
                case R_PPC_PLTREL24: return "R_PPC_PLTREL24";
                case R_PPC_COPY: return "R_PPC_COPY";
                case R_PPC_GLOB_DAT: return "R_PPC_GLOB_DAT";
                case R_PPC_JMP_SLOT: return "R_PPC_JMP_SLOT";
                case R_PPC_RELATIVE: return "R_PPC_RELATIVE";
                case R_PPC_LOCAL24PC: return "R_PPC_LOCAL24PC";
                case R_PPC_UADDR32: return "R_PPC_UADDR32";
                case R_PPC_UADDR16: return "R_PPC_UADDR16";
                case R_PPC_REL32: return "R_PPC_REL32";
                case R_PPC_PLT32: return "R_PPC_PLT32";
                case R_PPC_PLTREL32: return "R_PPC_PLTREL32";
                case R_PPC_PLT16_LO: return "R_PPC_PLT16_LO";
                case R_PPC_PLT16_HI: return "R_PPC_PLT16_HI";
                case R_PPC_PLT16_HA: return "R_PPC_PLT16_HA";
                case R_PPC_SDAREL16: return "R_PPC_SDAREL16";
                case R_PPC_SECTOFF: return "R_PPC_SECTOFF";
                case R_PPC_SECTOFF_LO: return "R_PPC_SECTOFF_LO";
                case R_PPC_SECTOFF_HI: return "R_PPC_SECTOFF_HI";
                case R_PPC_SECTOFF_HA: return "R_PPC_SECTOFF_HA";
                case R_PPC_TLS: return "R_PPC_TLS";
                case R_PPC_DTPMOD32: return "R_PPC_DTPMOD32";
                case R_PPC_TPREL16: return "R_PPC_TPREL16";
                case R_PPC_TPREL16_LO: return "R_PPC_TPREL16_LO";
                case R_PPC_TPREL16_HI: return "R_PPC_TPREL16_HI";
                case R_PPC_TPREL16_HA: return "R_PPC_TPREL16_HA";
                case R_PPC_TPREL32: return "R_PPC_TPREL32";
                case R_PPC_DTPREL16: return "R_PPC_DTPREL16";
                case R_PPC_DTPREL16_LO: return "R_PPC_DTPREL16_LO";
                case R_PPC_DTPREL16_HI: return "R_PPC_DTPREL16_HI";
                case R_PPC_DTPREL16_HA: return "R_PPC_DTPREL16_HA";
                case R_PPC_DTPREL32: return "R_PPC_DTPREL32";
                case R_PPC_GOT_TLSGD16: return "R_PPC_GOT_TLSGD16";
                case R_PPC_GOT_TLSGD16_LO: return "R_PPC_GOT_TLSGD16_LO";
                case R_PPC_GOT_TLSGD16_HI: return "R_PPC_GOT_TLSGD16_HI";
                case R_PPC_GOT_TLSGD16_HA: return "R_PPC_GOT_TLSGD16_HA";
                case R_PPC_GOT_TLSLD16: return "R_PPC_GOT_TLSLD16";
                case R_PPC_GOT_TLSLD16_LO: return "R_PPC_GOT_TLSLD16_LO";
                case R_PPC_GOT_TLSLD16_HI: return "R_PPC_GOT_TLSLD16_HI";
                case R_PPC_GOT_TLSLD16_HA: return "R_PPC_GOT_TLSLD16_HA";
                case R_PPC_GOT_TPREL16: return "R_PPC_GOT_TPREL16";
                case R_PPC_GOT_TPREL16_LO: return "R_PPC_GOT_TPREL16_LO";
                case R_PPC_GOT_TPREL16_HI: return "R_PPC_GOT_TPREL16_HI";
                case R_PPC_GOT_TPREL16_HA: return "R_PPC_GOT_TPREL16_HA";
                case R_PPC_IRELATIVE: return "R_PPC_IRELATIVE";
                default: break;
            }
            break;
        case EM_PPC64:
            switch (type) {
                case R_PPC64_ADDR64: return "R_PPC64_ADDR64";
                case R_PPC64_ADDR16_HIGHER: return "R_PPC64_ADDR16_HIGHER";
                case R_PPC64_ADDR16_HIGHERA: return "R_PPC64_ADDR16_HIGHERA";
                case R_PPC64_ADDR16_HIGHEST: return "R_PPC64_ADDR16_HIGHEST";
                case R_PPC64_ADDR16_HIGHESTA: return "R_PPC64_ADDR16_HIGHESTA";
                case R_PPC64_UADDR64: return "R_PPC64_UADDR64";
                case R_PPC64_REL64: return "R_PPC64_REL64";
                case R_PPC64_PLT64: return "R_PPC64_PLT64";
                case R_PPC64_PLTREL64: return "R_PPC64_PLTREL64";
                case R_PPC64_TOC16: return "R_PPC64_TOC16";
                case R_PPC64_TOC16_LO: return "R_PPC64_TOC16_LO";
                case R_PPC64_TOC16_HI: return "R_PPC64_TOC16_HI";
                case R_PPC64_TOC16_HA: return "R_PPC64_TOC16_HA";
                case R_PPC64_TOC: return "R_PPC64_TOC";
                case R_PPC64_ADDR16_DS: return "R_PPC64_ADDR16_DS";
                case R_PPC64_ADDR16_LO_DS: return "R_PPC64_ADDR16_LO_DS";
                case R_PPC64_GOT16_DS: return "R_PPC64_GOT16_DS";
                case R_PPC64_GOT16_LO_DS: return "R_PPC64_GOT16_LO_DS";
                case R_PPC64_PLT16_LO_DS: return "R_PPC64_PLT16_LO_DS";
                case R_PPC64_SECTOFF_DS: return "R_PPC64_SECTOFF_DS";
                case R_PPC64_SECTOFF_LO_DS: return "R_PPC64_SECTOFF_LO_DS";
                case R_PPC64_TOC16_DS: return "R_PPC64_TOC16_DS";
                case R_PPC64_TOC16_LO_DS: return "R_PPC64_TOC16_LO_DS";
                case R_PPC64_ENTRY: return "R_PPC64_ENTRY";
                case R_PPC64_PCREL34: return "R_PPC64_PCREL34";
                case R_PPC64_GOT_PCREL34: return "R_PPC64_GOT_PCREL34";
                case R_PPC64_PLT_PCREL34: return "R_PPC64_PLT_PCREL34";
                case R_PPC64_TLS: return "R_PPC64_TLS";
                case R_PPC64_DTPMOD64: return "R_PPC64_DTPMOD64";
                case R_PPC64_TPREL64: return "R_PPC64_TPREL64";
                case R_PPC64_DTPREL64: return "R_PPC64_DTPREL64";
                case R_PPC64_TPREL16_DS: return "R_PPC64_TPREL16_DS";
                case R_PPC64_TPREL16_LO_DS: return "R_PPC64_TPREL16_LO_DS";
                case R_PPC64_TPREL16_HIGHER: return "R_PPC64_TPREL16_HIGHER";
                case R_PPC64_TPREL16_HIGHERA: return "R_PPC64_TPREL16_HIGHERA";
                case R_PPC64_TPREL16_HIGHEST: return "R_PPC64_TPREL16_HIGHEST";
                case R_PPC64_TPREL16_HIGHESTA: return "R_PPC64_TPREL16_HIGHESTA";
                case R_PPC64_DTPREL16_DS: return "R_PPC64_DTPREL16_DS";
                case R_PPC64_DTPREL16_LO_DS: return "R_PPC64_DTPREL16_LO_DS";
                case R_PPC64_DTPREL16_HIGHER: return "R_PPC64_DTPREL16_HIGHER";
                case R_PPC64_DTPREL16_HIGHERA: return "R_PPC64_DTPREL16_HIGHERA";
                case R_PPC64_DTPREL16_HIGHEST: return "R_PPC64_DTPREL16_HIGHEST";
                case R_PPC64_DTPREL16_HIGHESTA: return "R_PPC64_DTPREL16_HIGHESTA";
                case R_PPC64_GOT_TLSGD16: return "R_PPC64_GOT_TLSGD16";
                case R_PPC64_GOT_TLSGD16_LO: return "R_PPC64_GOT_TLSGD16_LO";
                case R_PPC64_GOT_TLSGD16_HI: return "R_PPC64_GOT_TLSGD16_HI";
                case R_PPC64_GOT_TLSGD16_HA: return "R_PPC64_GOT_TLSGD16_HA";
                case R_PPC64_GOT_TLSLD16: return "R_PPC64_GOT_TLSLD16";
                case R_PPC64_GOT_TLSLD16_LO: return "R_PPC64_GOT_TLSLD16_LO";
                case R_PPC64_GOT_TLSLD16_HI: return "R_PPC64_GOT_TLSLD16_HI";
                case R_PPC64_GOT_TLSLD16_HA: return "R_PPC64_GOT_TLSLD16_HA";
                case R_PPC64_GOT_TPREL16_DS: return "R_PPC64_GOT_TPREL16_DS";
                case R_PPC64_GOT_TPREL16_LO_DS: return "R_PPC64_GOT_TPREL16_LO_DS";
                case R_PPC64_GOT_TPREL16_HI: return "R_PPC64_GOT_TPREL16_HI";
                case R_PPC64_GOT_TPREL16_HA: return "R_PPC64_GOT_TPREL16_HA";
                case R_PPC64_IRELATIVE: return "R_PPC64_IRELATIVE";
                default: break;
            }
            break;
        case EM_ALPHA:
            switch (type) {
                case R_ALPHA_NONE: return "R_ALPHA_NONE";
                case R_ALPHA_REFLONG: return "R_ALPHA_REFLONG";
                case R_ALPHA_REFQUAD: return "R_ALPHA_REFQUAD";
                case R_ALPHA_GPREL32: return "R_ALPHA_GPREL32";
                case R_ALPHA_LITERAL: return "R_ALPHA_LITERAL";
                case R_ALPHA_LITUSE: return "R_ALPHA_LITUSE";
                case R_ALPHA_GPDISP: return "R_ALPHA_GPDISP";
                case R_ALPHA_BRADDR: return "R_ALPHA_BRADDR";
                case R_ALPHA_HINT: return "R_ALPHA_HINT";
                case R_ALPHA_SREL16: return "R_ALPHA_SREL16";
                case R_ALPHA_SREL32: return "R_ALPHA_SREL32";
                case R_ALPHA_SREL64: return "R_ALPHA_SREL64";
                case R_ALPHA_GPRELHIGH: return "R_ALPHA_GPRELHIGH";
                case R_ALPHA_GPRELLOW: return "R_ALPHA_GPRELLOW";
                case R_ALPHA_GPREL16: return "R_ALPHA_GPREL16";
                case R_ALPHA_COPY: return "R_ALPHA_COPY";
                case R_ALPHA_GLOB_DAT: return "R_ALPHA_GLOB_DAT";
                case R_ALPHA_JMP_SLOT: return "R_ALPHA_JMP_SLOT";
                case R_ALPHA_RELATIVE: return "R_ALPHA_RELATIVE";
                case R_ALPHA_TLSGD: return "R_ALPHA_TLSGD";
                case R_ALPHA_TLSLDM: return "R_ALPHA_TLSLDM";
                case R_ALPHA_DTPMOD64: return "R_ALPHA_DTPMOD64";
                case R_ALPHA_GOTDTPREL: return "R_ALPHA_GOTDTPREL";
                case R_ALPHA_DTPREL64: return "R_ALPHA_DTPREL64";
                case R_ALPHA_DTPRELHI: return "R_ALPHA_DTPRELHI";
                case R_ALPHA_DTPRELLO: return "R_ALPHA_DTPRELLO";
                case R_ALPHA_DTPREL16: return "R_ALPHA_DTPREL16";
                case R_ALPHA_GOTTPREL: return "R_ALPHA_GOTTPREL";
                case R_ALPHA_TPREL64: return "R_ALPHA_TPREL64";
                case R_ALPHA_TPRELHI: return "R_ALPHA_TPRELHI";
                case R_ALPHA_TPRELLO: return "R_ALPHA_TPRELLO";
                case R_ALPHA_TPREL16: return "R_ALPHA_TPREL16";
                default: break;
            }
            break;
        case EM_AARCH64:
            if (type >= 257 && type <= 1032) {
                return "R_AARCH64_*";
            }
            break;
        default:
            break;
    }
    (void)snprintf(unknown, sizeof(unknown), "UNKNOWN(%u)", type);
    return unknown;
}

elf_err_t elf_apply_relocation_value(const elfobj_t *obj, uint32_t type, uint64_t place,
                                     uint64_t sym_value, int64_t addend, uint64_t *out_value) {
    elfobj_reloc_ctx_t ctx;
    const struct elf_reloc_backend *backend;
    int rc;

    if (obj == NULL || out_value == NULL) {
        return ELF_ERR_STATE;
    }

    backend = find_backend(obj->machine);
    if (backend == NULL || backend->apply_reloc == NULL) {
        elf__set_err((elfobj_t *)obj, ELF_ERR_UNSUPPORTED, "no relocation backend for machine");
        (void)elf__append_diag_fmt((elfobj_t *)obj, "machine=", obj->machine);
        return ELF_ERR_UNSUPPORTED;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.machine = obj->machine;
    ctx.use_rela = obj->cls == ELFOBJ_CLASS_64 ? 1 : 0;

    rc = backend->apply_reloc(&ctx, type, place, sym_value, addend, out_value);
    if (rc == 0) {
        return ELF_OK;
    }
    if (rc == -1) {
        elf__set_err((elfobj_t *)obj, ELF_ERR_UNSUPPORTED, "unsupported relocation type");
        (void)elf__append_diag_fmt((elfobj_t *)obj, "machine=", obj->machine);
        (void)elf__append_diag_fmt((elfobj_t *)obj, "reloc-type=", type);
        return ELF_ERR_UNSUPPORTED;
    }
    if (rc == -2) {
        elf__set_err((elfobj_t *)obj, ELF_ERR_RELOC, "relocation overflow");
        (void)elf__append_diag_fmt((elfobj_t *)obj, "machine=", obj->machine);
        (void)elf__append_diag_fmt((elfobj_t *)obj, "reloc-type=", type);
        return ELF_ERR_RELOC;
    }
    elf__set_err((elfobj_t *)obj, ELF_ERR_RELOC, "relocation backend apply failure");
    return ELF_ERR_RELOC;
}

elf_err_t elf_apply_relocation(const elf_reloc_t *reloc, uint64_t place, uint64_t sym_value,
                               uint64_t *out_value) {
    elfobj_t *obj;
    elf_err_t err;

    if (reloc == NULL || reloc->section == NULL || reloc->section->obj == NULL || out_value == NULL) {
        return ELF_ERR_STATE;
    }
    obj = reloc->section->obj;

    if (obj->reloc_hooks.before_apply != NULL) {
        if (!obj->reloc_hooks.before_apply(reloc, obj->reloc_hook_user)) {
            elf__set_err(obj, ELF_ERR_RELOC, "relocation apply blocked by relax hook");
            return ELF_ERR_RELOC;
        }
    }

    err = elf_apply_relocation_value(obj, reloc->type, place, sym_value, reloc->addend, out_value);
    if (err != ELF_OK) {
        return err;
    }

    if (obj->reloc_hooks.after_apply != NULL) {
        obj->reloc_hooks.after_apply(reloc, *out_value, obj->reloc_hook_user);
    }
    if (obj->reloc_hooks.incremental_note != NULL) {
        obj->reloc_hooks.incremental_note("reloc_type", reloc->type, obj->reloc_hook_user);
    }
    return ELF_OK;
}
