#include "elf_private.h"
#include "leb128.h"

#define DW_LNS_copy 1
#define DW_LNS_advance_pc 2
#define DW_LNS_advance_line 3
#define DW_LNS_set_file 4
#define DW_LNS_set_column 5
#define DW_LNS_negate_stmt 6
#define DW_LNS_set_basic_block 7
#define DW_LNS_const_add_pc 8
#define DW_LNS_fixed_advance_pc 9
#define DW_LNS_set_prologue_end 10
#define DW_LNS_set_epilogue_begin 11
#define DW_LNS_set_isa 12

#define DW_LNE_end_sequence 1
#define DW_LNE_set_address 2
#define DW_LNE_define_file 3
#define DW_LNE_set_discriminator 4

static const char *read_string(const uint8_t *data, size_t *offset, size_t max_size) {
    if (*offset >= max_size) return NULL;
    const char *str = (const char *)(data + *offset);
    size_t len = 0;
    while (*offset + len < max_size && str[len] != '\0') {
        len++;
    }
    if (*offset + len >= max_size) return NULL;
    *offset += len + 1;
    return str;
}

elf_err_t elf_dwarf_get_line_info(elfobj_t *obj, uint64_t addr, char **out_file, int *out_line) {
    if (obj == NULL || out_file == NULL || out_line == NULL) return ELF_ERR_STATE;

    *out_file = NULL;
    *out_line = 0;

    const elf_section_t *debug_line = NULL;
    for (size_t i = 0; i < obj->section_count; ++i) {
        if (obj->sections[i] && obj->sections[i]->name && strcmp(obj->sections[i]->name, ".debug_line") == 0) {
            debug_line = obj->sections[i];
            break;
        }
    }

    if (debug_line == NULL || debug_line->data == NULL) return ELF_OK;

    const uint8_t *data = debug_line->data;
    size_t size = debug_line->data_size;
    size_t off = 0;

    char *best_file = NULL;
    int best_line = 0;
    uint64_t best_addr = 0;

    while (off < size) {
        if (off + 4 > size) break;
        size_t unit_length = elf__rd32(data + off, obj->endian);
        off += 4;

        int is_64bit = 0;
        if (unit_length == 0xffffffffu) {
            is_64bit = 1;
            if (off + 8 > size) break;
            uint64_t len64 = elf__rd64(data + off, obj->endian);
            off += 8;
            if (len64 > size - off) break;
            unit_length = (size_t)len64;
        }

        /* off + unit_length can wrap size_t on the 32-bit target; compare
         * with the non-wrapping form (off < size holds from the loop). */
        if (unit_length > size - off) break;
        size_t unit_end = off + unit_length;

        if (off + 2 > unit_end) break;
        uint16_t version = elf__rd16(data + off, obj->endian);
        off += 2;

        if (version < 2 || version > 4) {
            off = unit_end;
            continue;
        }

        size_t header_length;
        if (is_64bit) {
            if (off + 8 > unit_end) break;
            header_length = elf__rd64(data + off, obj->endian);
            off += 8;
        } else {
            if (off + 4 > unit_end) break;
            header_length = elf__rd32(data + off, obj->endian);
            off += 4;
        }

        if (header_length > unit_end - off) break;
        size_t program_start = off + header_length;

        /* Fixed header fields read below: min_inst_len, default_is_stmt,
         * line_base, line_range, opcode_base (5), plus max_ops_per_inst for
         * version >= 4 (6). The old +5 guard let the v4 opcode_base read one
         * byte past the section. */
        if (off + 5 + (version >= 4 ? 1u : 0u) > program_start) break;
        uint8_t min_inst_len = data[off++];
        if (version >= 4) {
            off++;
        }
        off++;
        int8_t line_base = (int8_t)data[off++];
        uint8_t line_range = data[off++];
        uint8_t opcode_base = data[off++];

        /* line_range is the divisor/modulus for special opcodes; a zero
         * value (DW_LNS_const_add_pc and special-opcode advance) is a
         * division by zero. Skip a unit with a malformed header. */
        if (line_range == 0) {
            off = unit_end;
            continue;
        }

        if (off + opcode_base - 1 > program_start) break;
        uint8_t std_opcode_lengths[256] = {0};
        for (int i = 1; i < opcode_base; ++i) {
            std_opcode_lengths[i] = data[off++];
        }

        int num_includes = 1;
        while (off < program_start) {
            const char *dir = read_string(data, &off, program_start);
            if (dir == NULL || dir[0] == '\0') break;
            if (num_includes < 256) {
                num_includes++;
            }
        }

        const char *file_names[1024];
        uint32_t num_files = 1;
        while (off < program_start) {
            const char *file = read_string(data, &off, program_start);
            if (file == NULL || file[0] == '\0') break;

            read_uleb128(data, &off, program_start);
            read_uleb128(data, &off, program_start);
            read_uleb128(data, &off, program_start);

            if (num_files < 1024) {
                file_names[num_files] = file;
                num_files++;
            }
        }

        off = program_start;

        uint64_t sm_address = 0;
        uint32_t sm_file = 1;
        uint32_t sm_line = 1;

        while (off < unit_end) {
            uint8_t opcode = data[off++];

            if (opcode >= opcode_base) {
                uint8_t adjusted_opcode = opcode - opcode_base;
                uint64_t addr_adv = (adjusted_opcode / line_range) * min_inst_len;
                int line_adv = line_base + (adjusted_opcode % line_range);

                sm_line += line_adv;
                sm_address += addr_adv;

                if (sm_address <= addr) {
                    if (sm_address > best_addr || best_file == NULL || (sm_address == addr)) {
                        best_addr = sm_address;
                        best_line = sm_line;
                        if (sm_file < num_files) {
                            best_file = (char *)file_names[sm_file];
                        }
                    }
                }
            } else if (opcode == 0) {
                uint64_t ext_len = read_uleb128(data, &off, unit_end);
                if (ext_len > unit_end - off) break;
                size_t ext_end = off + ext_len;
                /* A zero-length extended op (or a length ULEB that consumed
                 * up to unit_end) leaves no opcode byte; reading data[off]
                 * would be one past the section. */
                if (off >= ext_end) break;

                uint8_t ext_opcode = data[off++];

                if (ext_opcode == DW_LNE_end_sequence) {
                    sm_address = 0;
                    sm_file = 1;
                    sm_line = 1;
                } else if (ext_opcode == DW_LNE_set_address) {
                    if (ext_len - 1 == 4) {
                        sm_address = elf__rd32(data + off, obj->endian);
                    } else if (ext_len - 1 == 8) {
                        sm_address = elf__rd64(data + off, obj->endian);
                    }
                } else if (ext_opcode == DW_LNE_set_discriminator) {
                    read_uleb128(data, &off, ext_end);
                }

                off = ext_end;
            } else {
                switch (opcode) {
                    case DW_LNS_copy:
                        if (sm_address <= addr) {
                            if (sm_address > best_addr || best_file == NULL || (sm_address == addr)) {
                                best_addr = sm_address;
                                best_line = sm_line;
                                if (sm_file < num_files) {
                                    best_file = (char *)file_names[sm_file];
                                }
                            }
                        }
                        break;
                    case DW_LNS_advance_pc:
                        sm_address += read_uleb128(data, &off, unit_end) * min_inst_len;
                        break;
                    case DW_LNS_advance_line:
                        sm_line += read_sleb128(data, &off, unit_end);
                        break;
                    case DW_LNS_set_file:
                        sm_file = read_uleb128(data, &off, unit_end);
                        break;
                    case DW_LNS_set_column:
                        read_uleb128(data, &off, unit_end);
                        break;
                    case DW_LNS_negate_stmt:
                        break;
                    case DW_LNS_set_basic_block:
                        break;
                    case DW_LNS_const_add_pc: {
                        uint8_t adjusted_opcode = 255 - opcode_base;
                        uint64_t addr_adv = (adjusted_opcode / line_range) * min_inst_len;
                        sm_address += addr_adv;
                        break;
                    }
                    case DW_LNS_fixed_advance_pc:
                        if (off + 2 <= unit_end) {
                            sm_address += elf__rd16(data + off, obj->endian);
                            off += 2;
                        }
                        break;
                    case DW_LNS_set_prologue_end:
                    case DW_LNS_set_epilogue_begin:
                    case DW_LNS_set_isa:
                        break;
                    default:
                        for (int i = 0; i < std_opcode_lengths[opcode]; ++i) {
                            read_uleb128(data, &off, unit_end);
                        }
                        break;
                }
            }
        }
        off = unit_end;
    }

    if (best_file) {
        *out_file = elf__strdup(best_file);
        *out_line = best_line;
    }

    return ELF_OK;
}
