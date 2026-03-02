#include "elf_private.h"

typedef struct {
    uint32_t tag;
    uint64_t value;
    const char *str;
} arm_attr_item_t;

static char *g_last_attr_string;

static uint64_t read_uleb128(const uint8_t *p, size_t size, size_t *off, int *ok) {
    uint64_t v = 0;
    unsigned shift = 0;
    *ok = 0;
    while (*off < size && shift < 64) {
        uint8_t b = p[*off];
        (*off)++;
        v |= (uint64_t)(b & 0x7f) << shift;
        if ((b & 0x80u) == 0) {
            *ok = 1;
            return v;
        }
        shift += 7;
    }
    return 0;
}

int elfobj_section_is_gnu_extension(const elf_section_t *section) {
    const char *name = elf_section_name(section);
    if (name == NULL) {
        return 0;
    }
    return strncmp(name, ".gnu", 4) == 0 || strcmp(name, ".note.gnu.build-id") == 0;
}

size_t elf_note_count(const elfobj_t *obj) {
    size_t i;
    size_t total = 0;
    if (obj == NULL) {
        return 0;
    }
    for (i = 0; i < obj->section_count; ++i) {
        const elf_section_t *s = obj->sections[i];
        size_t off = 0;
        if (s == NULL || s->type != SHT_NOTE || s->data == NULL) {
            continue;
        }
        while (off + 12 <= s->data_size) {
            uint32_t namesz = elf__rd32(s->data + off + 0, obj->endian);
            uint32_t descsz = elf__rd32(s->data + off + 4, obj->endian);
            size_t noff = off + 12;
            size_t doff;
            noff = (noff + namesz + 3u) & ~3u;
            doff = noff;
            doff = (doff + descsz + 3u) & ~3u;
            if (doff > s->data_size) {
                break;
            }
            total++;
            off = doff;
        }
    }
    return total;
}

int elf_note_at(const elfobj_t *obj, size_t index, elf_note_info_t *out) {
    size_t i;
    size_t seen = 0;
    if (obj == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < obj->section_count; ++i) {
        const elf_section_t *s = obj->sections[i];
        size_t off = 0;
        if (s == NULL || s->type != SHT_NOTE || s->data == NULL) {
            continue;
        }
        while (off + 12 <= s->data_size) {
            uint32_t namesz = elf__rd32(s->data + off + 0, obj->endian);
            uint32_t descsz = elf__rd32(s->data + off + 4, obj->endian);
            uint32_t type = elf__rd32(s->data + off + 8, obj->endian);
            size_t nstart = off + 12;
            size_t nend = nstart + namesz;
            size_t dstart;
            size_t dend;
            if (nend > s->data_size) {
                return 0;
            }
            dstart = (nend + 3u) & ~3u;
            dend = (dstart + descsz + 3u) & ~3u;
            if (dend > s->data_size) {
                return 0;
            }
            if (seen == index) {
                out->name = (const char *)(s->data + nstart);
                out->type = type;
                out->desc_data = s->data + dstart;
                out->desc_size = descsz;
                return 1;
            }
            seen++;
            off = dend;
        }
    }
    return 0;
}

static int note_is_gnu_property(const elf_note_info_t *n) {
    if (n == NULL || n->name == NULL) {
        return 0;
    }
    return n->type == 5u && strcmp(n->name, "GNU") == 0;
}

size_t elf_gnu_property_count(const elfobj_t *obj) {
    size_t notes = elf_note_count(obj);
    size_t i;
    size_t total = 0;
    elf_note_info_t n;
    for (i = 0; i < notes; ++i) {
        if (!elf_note_at(obj, i, &n) || !note_is_gnu_property(&n)) {
            continue;
        }
        {
            size_t off = 0;
            while (off + 8 <= n.desc_size) {
                uint32_t datasz = elf__rd32((const uint8_t *)n.desc_data + off + 4, obj->endian);
                size_t next = off + 8 + datasz;
                next = (next + (obj->cls == ELFOBJ_CLASS_64 ? 7u : 3u)) &
                       ~(obj->cls == ELFOBJ_CLASS_64 ? 7u : 3u);
                if (next > n.desc_size) {
                    break;
                }
                total++;
                off = next;
            }
        }
    }
    return total;
}

int elf_gnu_property_at(const elfobj_t *obj, size_t index, elf_gnu_property_info_t *out) {
    size_t notes = elf_note_count(obj);
    size_t i;
    size_t seen = 0;
    elf_note_info_t n;
    if (obj == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < notes; ++i) {
        size_t off = 0;
        if (!elf_note_at(obj, i, &n) || !note_is_gnu_property(&n)) {
            continue;
        }
        while (off + 8 <= n.desc_size) {
            uint32_t type = elf__rd32((const uint8_t *)n.desc_data + off + 0, obj->endian);
            uint32_t datasz = elf__rd32((const uint8_t *)n.desc_data + off + 4, obj->endian);
            size_t dstart = off + 8;
            size_t next = dstart + datasz;
            next = (next + (obj->cls == ELFOBJ_CLASS_64 ? 7u : 3u)) &
                   ~(obj->cls == ELFOBJ_CLASS_64 ? 7u : 3u);
            if (next > n.desc_size) {
                return 0;
            }
            if (seen == index) {
                out->type = type;
                out->data = (const uint8_t *)n.desc_data + dstart;
                out->data_size = datasz;
                return 1;
            }
            seen++;
            off = next;
        }
    }
    return 0;
}

uint32_t elf_x86_isa_level(const elfobj_t *obj) {
    size_t n = elf_gnu_property_count(obj);
    size_t i;
    elf_gnu_property_info_t p;
    for (i = 0; i < n; ++i) {
        if (elf_gnu_property_at(obj, i, &p) && p.type == GNU_PROPERTY_X86_ISA_1_NEEDED &&
            p.data_size >= 4) {
            return elf__rd32((const uint8_t *)p.data, obj->endian);
        }
    }
    return 0;
}

uint32_t elf_x86_feature_flags(const elfobj_t *obj) {
    size_t n = elf_gnu_property_count(obj);
    size_t i;
    elf_gnu_property_info_t p;
    for (i = 0; i < n; ++i) {
        if (elf_gnu_property_at(obj, i, &p) && p.type == GNU_PROPERTY_X86_FEATURE_1_AND &&
            p.data_size >= 4) {
            return elf__rd32((const uint8_t *)p.data, obj->endian);
        }
    }
    return 0;
}

uint32_t elf_aarch64_feature_flags(const elfobj_t *obj) {
    size_t n = elf_gnu_property_count(obj);
    size_t i;
    elf_gnu_property_info_t p;
    for (i = 0; i < n; ++i) {
        if (elf_gnu_property_at(obj, i, &p) && p.type == GNU_PROPERTY_AARCH64_FEATURE_1_AND &&
            p.data_size >= 4) {
            return elf__rd32((const uint8_t *)p.data, obj->endian);
        }
    }
    return 0;
}

elf_err_t elf_add_gnu_property_x86(elfobj_t *obj, uint32_t isa_needed, uint32_t isa_used,
                                   uint32_t feature_1) {
    elf_section_t *sec;
    uint8_t data[64];
    size_t off = 0;

    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    sec = elf_find_section(obj, ".note.gnu.property");
    if (sec == NULL) {
        sec = elf_add_section(obj, ".note.gnu.property", SHT_NOTE, SHF_ALLOC);
        if (sec == NULL) {
            return obj->last_err == ELF_OK ? ELF_ERR_OOM : obj->last_err;
        }
        (void)elf_section_set_align(sec, obj->cls == ELFOBJ_CLASS_64 ? 8 : 4);
    }

    memset(data, 0, sizeof(data));
    elf__wr32(data + off, obj->endian, 4);
    off += 4;
    elf__wr32(data + off, obj->endian, 36);
    off += 4;
    elf__wr32(data + off, obj->endian, 5);
    off += 4;
    memcpy(data + off, "GNU\0", 4);
    off += 4;

    elf__wr32(data + off, obj->endian, GNU_PROPERTY_X86_ISA_1_NEEDED);
    off += 4;
    elf__wr32(data + off, obj->endian, 4);
    off += 4;
    elf__wr32(data + off, obj->endian,
              isa_needed & (GNU_PROPERTY_X86_ISA_1_BASELINE | GNU_PROPERTY_X86_ISA_1_V2 |
                            GNU_PROPERTY_X86_ISA_1_V3 | GNU_PROPERTY_X86_ISA_1_V4));
    off += 4;

    elf__wr32(data + off, obj->endian, GNU_PROPERTY_X86_ISA_1_USED);
    off += 4;
    elf__wr32(data + off, obj->endian, 4);
    off += 4;
    elf__wr32(data + off, obj->endian,
              isa_used & (GNU_PROPERTY_X86_ISA_1_BASELINE | GNU_PROPERTY_X86_ISA_1_V2 |
                          GNU_PROPERTY_X86_ISA_1_V3 | GNU_PROPERTY_X86_ISA_1_V4));
    off += 4;

    elf__wr32(data + off, obj->endian, GNU_PROPERTY_X86_FEATURE_1_AND);
    off += 4;
    elf__wr32(data + off, obj->endian, 4);
    off += 4;
    elf__wr32(data + off, obj->endian,
              feature_1 & (GNU_PROPERTY_X86_FEATURE_1_IBT | GNU_PROPERTY_X86_FEATURE_1_SHSTK));
    off += 4;

    return elf_section_set_data(sec, data, off);
}

int elf_build_id(const elfobj_t *obj, const uint8_t **out_data, size_t *out_size) {
    size_t notes = elf_note_count(obj);
    size_t i;
    elf_note_info_t n;
    if (obj == NULL || out_data == NULL || out_size == NULL) {
        return 0;
    }
    for (i = 0; i < notes; ++i) {
        if (!elf_note_at(obj, i, &n)) {
            continue;
        }
        if (n.type == 3u && n.name != NULL && strcmp(n.name, "GNU") == 0) {
            *out_data = (const uint8_t *)n.desc_data;
            *out_size = n.desc_size;
            return 1;
        }
    }
    return 0;
}

int elf_mips_abiflags(const elfobj_t *obj, elf_mips_abiflags_t *out) {
    size_t i;
    const elf_section_t *sec = NULL;
    const uint8_t *p;
    if (obj == NULL || out == NULL) {
        return 0;
    }
    for (i = 0; i < obj->section_count; ++i) {
        const elf_section_t *s = obj->sections[i];
        if (s == NULL) {
            continue;
        }
        if (s->type == SHT_MIPS_ABIFLAGS ||
            (s->name != NULL && strcmp(s->name, ".MIPS.abiflags") == 0)) {
            sec = s;
            break;
        }
    }
    if (sec == NULL || sec->data == NULL || sec->data_size < 24) {
        return 0;
    }
    p = sec->data;
    memset(out, 0, sizeof(*out));
    out->version = elf__rd16(p + 0, obj->endian);
    out->isa_level = p[2];
    out->isa_rev = p[3];
    out->gpr_size = p[4];
    out->cpr1_size = p[5];
    out->cpr2_size = p[6];
    out->fp_abi = p[7];
    out->isa_ext = elf__rd32(p + 8, obj->endian);
    out->ases = elf__rd32(p + 12, obj->endian);
    out->flags1 = elf__rd32(p + 16, obj->endian);
    out->flags2 = elf__rd32(p + 20, obj->endian);
    return 1;
}

static size_t parse_arm_attrs(const elfobj_t *obj, arm_attr_item_t *items, size_t max_items) {
    const elf_section_t *s;
    const uint8_t *p;
    size_t size;
    size_t off = 0;
    size_t outn = 0;

    if (obj == NULL) {
        return 0;
    }
    s = elf_find_section((elfobj_t *)obj, ".ARM.attributes");
    if (s == NULL || s->data == NULL || s->data_size < 5) {
        return 0;
    }
    p = s->data;
    size = s->data_size;
    if (p[0] != 'A') {
        return 0;
    }
    off = 1;
    while (off + 4 <= size) {
        uint32_t sect_len = elf__rd32(p + off, obj->endian);
        size_t start = off;
        size_t end;
        size_t vendor_off;
        if (sect_len < 5) {
            break;
        }
        end = start + sect_len;
        if (end > size) {
            break;
        }
        off += 4;
        vendor_off = off;
        while (off < end && p[off] != '\0') {
            off++;
        }
        if (off >= end) {
            break;
        }
        off++;
        if (off < end && p[off] == 1) {
            size_t sub_off;
            size_t sub_end;
            off++;
            if (off + 4 > end) {
                break;
            }
            sub_off = off;
            sub_end = sub_off + elf__rd32(p + sub_off, obj->endian);
            if (sub_end > end) {
                sub_end = end;
            }
            off += 4;
            while (off < sub_end) {
                int ok = 0;
                uint64_t tag = read_uleb128(p, sub_end, &off, &ok);
                uint64_t value = 0;
                const char *str = NULL;
                if (!ok) {
                    break;
                }
                if (tag == 4) {
                    size_t st = off;
                    while (off < sub_end && p[off] != '\0') {
                        off++;
                    }
                    if (off < sub_end) {
                        str = (const char *)(p + st);
                        off++;
                    }
                } else {
                    value = read_uleb128(p, sub_end, &off, &ok);
                    if (!ok) {
                        break;
                    }
                }
                if (items != NULL && outn < max_items) {
                    items[outn].tag = (uint32_t)tag;
                    items[outn].value = value;
                    items[outn].str = str;
                }
                outn++;
            }
        }
        off = end;
        (void)vendor_off;
    }
    return outn;
}

size_t elf_arm_attribute_count(const elfobj_t *obj) {
    return parse_arm_attrs(obj, NULL, 0);
}

uint32_t elf_arm_attribute_tag_at(const elfobj_t *obj, size_t index) {
    arm_attr_item_t item;
    if (parse_arm_attrs(obj, &item, index + 1) <= index) {
        return 0;
    }
    return item.tag;
}

uint64_t elf_arm_attribute_value_at(const elfobj_t *obj, size_t index) {
    arm_attr_item_t item;
    if (parse_arm_attrs(obj, &item, index + 1) <= index) {
        return 0;
    }
    return item.value;
}

const char *elf_arm_attribute_string_at(const elfobj_t *obj, size_t index) {
    arm_attr_item_t item;
    size_t n = parse_arm_attrs(obj, &item, index + 1);
    if (n <= index || item.str == NULL) {
        return NULL;
    }
    free(g_last_attr_string);
    g_last_attr_string = elf__strdup(item.str);
    return g_last_attr_string;
}
