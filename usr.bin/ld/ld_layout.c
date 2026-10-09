/*
 * ld_layout.c -- layout: section order, addresses, segments.
 */

#include "ld.h"

int alloc_section_class(uint64_t flags) {
    if ((flags & SHF_ALLOC) == 0) {
        return -1;
    }
    if ((flags & SHF_EXECINSTR) != 0) {
        return 0; /* RX */
    }
    if ((flags & SHF_WRITE) != 0) {
        return 2; /* RW */
    }
    return 1; /* RO */
}

int assign_section_addresses(elfobj_t *obj, uint64_t base_vaddr) {
    uint64_t off;
    uint64_t mem_end;
    uint64_t ehsize;
    uint64_t phentsz;
    uint64_t phnum;
    const uint64_t page_align = 0x1000u;
    int last_alloc_class = -1;
    int last_rw_relro = -1;
    size_t i;

    ehsize = elf_class(obj) == ELFOBJ_CLASS_64 ? 64u : 52u;
    phentsz = elf_class(obj) == ELFOBJ_CLASS_64 ? 56u : 32u;
    phnum = (uint64_t)elf_segment_count(obj);
    if (phnum == 0) {
        phnum = (uint64_t)elf_program_header_count(obj);
    }
    if (phnum == 0 && (elf_type(obj) == ET_EXEC || elf_type(obj) == ET_DYN)) {
        int has_dynamic = 0;
        int has_tls = 0;
        int has_interp = 0;
        phnum = 1;
        for (i = 0; i < elf_section_count(obj); ++i) {
            const elf_section_t *sec = elf_section_get(obj, i);
            const char *nm;
            if (sec == NULL) {
                continue;
            }
            nm = elf_section_name(sec);
            if (elf_section_type(sec) == SHT_DYNAMIC) {
                has_dynamic = 1;
            }
            if ((elf_section_flags(sec) & SHF_TLS) != 0) {
                has_tls = 1;
            }
            if (nm != NULL && strcmp(nm, ".interp") == 0) {
                has_interp = 1;
            }
        }
        if (has_dynamic) {
            phnum++;
        }
        if (has_interp) {
            phnum++;
        }
        if (has_tls) {
            phnum++;
        }
    }

    {
        uint64_t ph_total;
        if (!mul_u64_checked(phnum, phentsz, &ph_total) ||
            !add_u64_checked(ehsize, ph_total, &off) ||
            !add_u64_checked(base_vaddr, off, &mem_end)) {
            return -1;
        }
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        uint64_t align;
        uint64_t size;
        uint64_t flags;
        const char *name;
        uint64_t file_off = 0;
        uint64_t addr = 0;

        if (sec == NULL) {
            continue;
        }
        align = elf_section_align(sec);
        if (align == 0) {
            align = 1;
        }
        size = elf_section_size(sec);
        flags = elf_section_flags(sec);
        name = elf_section_name(sec);

        if ((flags & SHF_ALLOC) != 0) {
            int curr_alloc_class = alloc_section_class(flags);
            if (last_alloc_class != -1 && curr_alloc_class != last_alloc_class) {
                if (!align_up_u64_checked(off, page_align, &off) ||
                    !align_up_u64_checked(mem_end, page_align, &mem_end)) {
                    return -1;
                }
                last_rw_relro = -1;
            } else if (curr_alloc_class == 2) {
                int curr_relro = is_relro_candidate_name(name) ? 1 : 0;
                if (last_rw_relro != -1 && curr_relro != last_rw_relro) {
                    if (!align_up_u64_checked(off, page_align, &off) ||
                        !align_up_u64_checked(mem_end, page_align, &mem_end)) {
                        return -1;
                    }
                }
                last_rw_relro = curr_relro;
            } else {
                last_rw_relro = -1;
            }
            last_alloc_class = curr_alloc_class;
        }

        if (elf_section_type(sec) != SHT_NOBITS) {
            /* It is the address that a section's alignment is of.  The
             * two agree while the image begins on a multiple of it, which
             * a page-aligned base is for anything up to a page. */
            if ((flags & SHF_ALLOC) != 0) {
                uint64_t a;

                if (!add_u64_checked(base_vaddr, off, &a) || !align_up_u64_checked(a, align, &a)) {
                    return -1;
                }
                off = a - base_vaddr;
            } else if (!align_up_u64_checked(off, align, &off)) {
                return -1;
            }
            file_off = off;
            if (!add_u64_checked(off, size, &off)) {
                return -1;
            }
        }

        if ((flags & SHF_ALLOC) != 0) {
            if (elf_section_type(sec) == SHT_NOBITS) {
                if (!align_up_u64_checked(mem_end, align, &addr)) {
                    return -1;
                }
            } else {
                if (!add_u64_checked(base_vaddr, file_off, &addr)) {
                    return -1;
                }
                if ((addr & 0xfffu) != (file_off & 0xfffu)) {
                    return -1;
                }
            }
            {
                uint64_t addr_end;
                if (!add_u64_checked(addr, size, &addr_end)) {
                    return -1;
                }
                if (addr_end > mem_end) {
                    mem_end = addr_end;
                }
            }
            if (elf_section_set_addr(sec, addr) != ELF_OK) {
                return -1;
            }
        } else if (elf_section_set_addr(sec, 0) != ELF_OK) {
            return -1;
        }
    }

    return 0;
}

static int section_order_rank(const elf_section_t *sec) {
    const char *name;
    uint64_t flags;
    uint32_t type;

    if (sec == NULL) {
        return 999;
    }
    name = elf_section_name(sec);
    type = elf_section_type(sec);
    flags = elf_section_flags(sec);

    if (name != NULL &&
        (strcmp(name, ".symtab") == 0 || strcmp(name, ".strtab") == 0 || strcmp(name, ".shstrtab") == 0)) {
        return 200;
    }
    /*
     * Among what is writable, what is written only by the dynamic linker
     * comes first and together, so that it can be made read-only once it
     * has been (PT_GNU_RELRO) without anything the program writes lying
     * in the same range: a .data between .data.rel.ro and .got was in
     * the range, and read-only when the program came to write it.
     */
    if (name != NULL && (flags & (SHF_ALLOC | SHF_WRITE)) == (SHF_ALLOC | SHF_WRITE) &&
        type != SHT_NOBITS && is_relro_candidate_name(name)) {
        if (strcmp(name, ".dynamic") == 0) {
            return 28;
        }
        if (strncmp(name, ".got", 4) == 0) {
            return 29;
        }
        return strncmp(name, ".data.rel.ro", 12) == 0 ? 27 : 26;
    }
    if (name != NULL) {
        if (strcmp(name, ".got.plt") == 0) {
            return 30;
        }
        if (strcmp(name, ".tm_clone_table") == 0) {
            return 32;
        }
    }
    if (type == SHT_REL || type == SHT_RELA) {
        if ((flags & SHF_ALLOC) != 0) {
            return 25;
        }
        return 190;
    }
    if ((flags & SHF_ALLOC) != 0) {
        if ((flags & SHF_EXECINSTR) != 0) {
            return 10;
        }
        if ((flags & SHF_WRITE) == 0) {
            return 20;
        }
        if (type == SHT_NOBITS) {
            return 40;
        }
        return 31;
    }
    return 100;
}

int reorder_sections_default_policy(elfobj_t *obj) {
    size_t count;
    size_t target;

    if (obj == NULL) {
        return -1;
    }
    count = elf_section_count(obj);
    for (target = 0; target < count; ++target) {
        size_t i;
        size_t best = target;
        int best_rank = section_order_rank(elf_section_get(obj, target));

        for (i = target + 1; i < count; ++i) {
            int rank = section_order_rank(elf_section_get(obj, i));
            if (rank < best_rank) {
                best = i;
                best_rank = rank;
            }
        }
        if (best != target) {
            elf_section_t *best_sec = elf_section_get(obj, best);
            if (best_sec == NULL || elf_reorder_section(obj, best_sec, target) != ELF_OK) {
                return -1;
            }
        }
    }
    return 0;
}

int is_relro_candidate_name(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (strncmp(name, ".got.plt", 8) == 0) {
        return 0;
    }
    if (strncmp(name, ".got", 4) == 0 ||
        strncmp(name, ".data.rel.ro", 12) == 0 ||
        strcmp(name, ".dynamic") == 0 ||
        strncmp(name, ".init_array", 11) == 0 ||
        strncmp(name, ".fini_array", 11) == 0 ||
        strncmp(name, ".preinit_array", 14) == 0 ||
        strncmp(name, ".ctors", 6) == 0 ||
        strncmp(name, ".dtors", 6) == 0) {
        return 1;
    }
    return 0;
}

static int has_execstack_note(const elfobj_t *obj) {
    size_t i;

    if (obj == NULL) {
        return 0;
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        const char *name;
        if (sec == NULL) {
            continue;
        }
        name = elf_section_name(sec);
        if (name == NULL || strcmp(name, ".note.GNU-stack") != 0) {
            continue;
        }
        if ((elf_section_flags(sec) & SHF_EXECINSTR) != 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * Whether the script says where the output section `name` goes, rather
 * than leaving it to follow the one before: an address of its own, a
 * memory region the one before was not in, or an assignment to the
 * location counter since.  Such a section may be anywhere, and a segment
 * is one run of the file mapped at one run of addresses, so it begins a
 * segment of its own.
 */
static int script_places_section(const ld_ctx_t *ctx, const char *name) {
    const lds_script_t *sc = ctx->script;
    const lds_stmt_t *prev = NULL;
    int moved = 0;
    size_t i;

    if (sc == NULL || !sc->has_sections || name == NULL) {
        return 0;
    }
    for (i = 0; i < sc->stmts.count; ++i) {
        const lds_stmt_t *st = &sc->stmts.items[i];

        if (st->kind == LDS_ST_ASSIGN && strcmp(st->at.text, ".") == 0) {
            moved = 1;
        }
        if (st->kind != LDS_ST_OUTSEC || st->discard) {
            continue;
        }
        if (strcmp(st->at.text, name) == 0) {
            return moved || st->expr.count != 0 ||
                   (st->region != NULL &&
                    (prev == NULL || prev->region == NULL || strcmp(prev->region, st->region) != 0));
        }
        prev = st;
        moved = 0;
    }
    return 0;
}

int add_default_segments(elfobj_t *obj, const ld_ctx_t *ctx) {
    elf_segment_t *load_rx = NULL;
    elf_segment_t *load_ro = NULL;
    elf_segment_t *load_rw = NULL;
    elf_segment_t *tls_seg = NULL;
    elf_segment_t *relro_seg = NULL;
    elf_segment_t *eh_frame_seg = NULL;
    elf_segment_t *gnu_property_seg = NULL;
    elf_section_t *dyn;
    size_t i;
    int execstack_mode;
    int applied;

    if (obj == NULL || ctx == NULL) {
        return -1;
    }
    applied = add_script_segments(obj, ctx);
    if (applied != 0) {
        return applied < 0 ? -1 : 0;
    }

    if (elf_add_segment(obj, PT_PHDR, LD_PF_R, 8) == NULL) {
        return -1;
    }
    if (ctx->interp_path != NULL && ctx->interp_path[0] != '\0') {
        if (elf_add_interp_segment(obj, ctx->interp_path) == NULL) {
            return -1;
        }
    }

    for (i = 0; i < elf_section_count(obj); ++i) {
        elf_section_t *sec = elf_section_get(obj, i);
        const char *name;
        uint64_t flags;
        uint32_t type;
        int is_alloc;

        if (sec == NULL) {
            continue;
        }
        name = elf_section_name(sec);
        flags = elf_section_flags(sec);
        type = elf_section_type(sec);
        is_alloc = (flags & SHF_ALLOC) != 0;

        if (is_alloc && script_places_section(ctx, name)) {
            if ((flags & SHF_EXECINSTR) != 0) {
                load_rx = NULL;
            } else if ((flags & SHF_WRITE) != 0) {
                load_rw = NULL;
            } else {
                load_ro = NULL;
            }
        }
        if (is_alloc) {
            if ((flags & SHF_EXECINSTR) != 0) {
                if (load_rx == NULL) {
                    load_rx = elf_add_load_segment(obj, LD_PF_R | LD_PF_X, 0x1000);
                    if (load_rx == NULL) {
                        return -1;
                    }
                }
                if (elf_segment_add_section(load_rx, sec) != ELF_OK) {
                    return -1;
                }
            } else if ((flags & SHF_WRITE) != 0) {
                if (load_rw == NULL) {
                    load_rw = elf_add_load_segment(obj, LD_PF_R | LD_PF_W, 0x1000);
                    if (load_rw == NULL) {
                        return -1;
                    }
                }
                if (elf_segment_add_section(load_rw, sec) != ELF_OK) {
                    return -1;
                }
            } else {
                if (load_ro == NULL) {
                    load_ro = elf_add_load_segment(obj, LD_PF_R, 0x1000);
                    if (load_ro == NULL) {
                        return -1;
                    }
                }
                if (elf_segment_add_section(load_ro, sec) != ELF_OK) {
                    return -1;
                }
            }
        }

        if (is_alloc && type == SHT_NOTE) {
            elf_segment_t *note_seg = elf_add_segment(obj, PT_NOTE, LD_PF_R, 4);
            if (note_seg == NULL) {
                return -1;
            }
            if (elf_segment_add_section(note_seg, sec) != ELF_OK) {
                return -1;
            }
        }
        if (is_alloc && name != NULL && strcmp(name, ".eh_frame_hdr") == 0 && elf_section_size(sec) != 0) {
            if (eh_frame_seg == NULL) {
                eh_frame_seg = elf_add_segment(obj, PT_GNU_EH_FRAME, LD_PF_R, 4);
                if (eh_frame_seg == NULL) {
                    return -1;
                }
            }
            if (elf_segment_add_section(eh_frame_seg, sec) != ELF_OK) {
                return -1;
            }
        }
        if (is_alloc && name != NULL && strcmp(name, ".note.gnu.property") == 0) {
            if (gnu_property_seg == NULL) {
                gnu_property_seg = elf_add_segment(obj, PT_GNU_PROPERTY, LD_PF_R, 8);
                if (gnu_property_seg == NULL) {
                    return -1;
                }
            }
            if (elf_segment_add_section(gnu_property_seg, sec) != ELF_OK) {
                return -1;
            }
        }
        if (ctx->z_relro && is_alloc && (flags & SHF_WRITE) != 0 &&
            is_relro_candidate_name(name)) {
            if (relro_seg == NULL) {
                relro_seg = elf_add_segment(obj, PT_GNU_RELRO, LD_PF_R, 1);
                if (relro_seg == NULL) {
                    return -1;
                }
            }
            if (elf_segment_add_section(relro_seg, sec) != ELF_OK) {
                return -1;
            }
        }
        if (is_alloc && (flags & SHF_TLS) != 0) {
            if (tls_seg == NULL) {
                tls_seg = elf_add_tls_segment(obj, 8);
                if (tls_seg == NULL) {
                    return -1;
                }
            }
            if (elf_segment_add_section(tls_seg, sec) != ELF_OK) {
                return -1;
            }
        }
    }

    dyn = elf_find_section(obj, ".dynamic");
    if (dyn != NULL) {
        elf_segment_t *dyn_seg = elf_add_dynamic_segment(obj, 8);
        if (dyn_seg == NULL || elf_segment_add_section(dyn_seg, dyn) != ELF_OK) {
            return -1;
        }
    }

    execstack_mode = ctx->z_execstack;
    if (execstack_mode < 0) {
        execstack_mode = has_execstack_note(obj) ? 1 : 0;
    }
    if (elf_add_segment(obj, PT_GNU_STACK,
                        LD_PF_R | LD_PF_W | (execstack_mode ? LD_PF_X : 0), 16) == NULL) {
        return -1;
    }
    return 0;
}

int strip_group_sections_for_final(elfobj_t *obj) {
    size_t i = 0;

    while (i < elf_section_count(obj)) {
        elf_section_t *sec = elf_section_get(obj, i);
        const char *name;
        if (sec == NULL) {
            i++;
            continue;
        }
        name = elf_section_name(sec);
        if (name == NULL || strcmp(name, ".group") != 0) {
            i++;
            continue;
        }
        if (elf_remove_section(obj, sec) != ELF_OK) {
            return -1;
        }
    }
    return 0;
}

int enforce_wx_policy(const elfobj_t *obj) {
    size_t i;

    if (obj == NULL) {
        return -1;
    }
    for (i = 0; i < elf_section_count(obj); ++i) {
        const elf_section_t *sec = elf_section_get(obj, i);
        uint64_t flags;
        const char *name;
        if (sec == NULL) {
            continue;
        }
        flags = elf_section_flags(sec);
        if ((flags & SHF_ALLOC) == 0) {
            continue;
        }
        if ((flags & SHF_WRITE) != 0 && (flags & SHF_EXECINSTR) != 0) {
            name = elf_section_name(sec);
            fprintf(stderr,
                    "ld: W^X policy violation: section %s is both writable and executable\n",
                    name != NULL ? name : "<unnamed>");
            return -1;
        }
    }
    return 0;
}
