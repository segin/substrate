/*
 * ld_main.c — Phase 2 entry from asm.
 *
 * At call time, our own image's R_386_RELATIVE relocations have
 * already been applied by ld_start.S — so all globals and string
 * constants are reachable normally.  We're handed the original
 * kernel-built stack pointer (where argc lives).
 *
 * Phase 2 deliverable: locate the program's PT_DYNAMIC via the
 * auxv (AT_PHDR / AT_PHNUM / AT_PHENT), walk it, summarize what
 * we found, then jump to AT_ENTRY.  No DT_NEEDED loading yet —
 * that lands in Phase 3.
 */

#include "ld.h"

/* Walk auxv and pull out the entries we need.  argc/argv/envp
 * are skipped over — same logic as the asm AT_ENTRY scanner from
 * Phase 1, just doing it in C now that we can. */
typedef struct {
    ld_u32 phdr;
    ld_u32 phent;
    ld_u32 phnum;
    ld_u32 base;
    ld_u32 entry;
    ld_u32 pagesz;
} ld_auxv_t;

static void parse_auxv(ld_u32 *initial_stack, ld_auxv_t *out) {
    /* initial_stack[0] = argc */
    ld_u32 argc = initial_stack[0];
    ld_u32 *p = initial_stack + 1;
    /* Skip argv */
    p += argc + 1;          /* argv[0..argc-1] + NULL */
    /* Skip envp until terminating NULL */
    while (*p) p++;
    p++;                    /* step past envp NULL */

    /* Auxv now starts at p as (a_type, a_val) pairs. */
    out->phdr = out->phent = out->phnum = 0;
    out->base = out->entry = out->pagesz = 0;
    while (*p) {
        ld_u32 tag = p[0];
        ld_u32 val = p[1];
        switch (tag) {
        case AT_PHDR:   out->phdr = val;   break;
        case AT_PHENT:  out->phent = val;  break;
        case AT_PHNUM:  out->phnum = val;  break;
        case AT_BASE:   out->base = val;   break;
        case AT_ENTRY:  out->entry = val;  break;
        case AT_PAGESZ: out->pagesz = val; break;
        }
        p += 2;
    }
}

/* Scan program phdrs for PT_DYNAMIC.  Returns the dynamic table
 * pointer or NULL if absent (static binary).  Also returns the
 * PIE load bias via *load_bias_out so subsequent address math is
 * straightforward. */
static Elf32_Dyn *find_dynamic(const ld_auxv_t *a, ld_u32 *load_bias_out) {
    Elf32_Phdr *ph = (Elf32_Phdr *)a->phdr;
    ld_u32 ph_load_min = 0xFFFFFFFFu;
    ld_u32 ph_load_min_vaddr = 0xFFFFFFFFu;
    Elf32_Phdr *dyn_ph = 0;

    for (ld_u32 i = 0; i < a->phnum; i++) {
        Elf32_Phdr *p = (Elf32_Phdr *)((char *)ph + i * a->phent);
        if (p->p_type == PT_LOAD && p->p_vaddr < ph_load_min_vaddr) {
            ph_load_min_vaddr = p->p_vaddr;
            /* Lowest PT_LOAD's runtime address - vaddr = load bias.
             * The kernel told us where AT_PHDR landed; the program
             * header table itself sits inside (or just after) the
             * first PT_LOAD, so the bias = AT_PHDR - first PT_LOAD
             * file offset.  But we don't have the e_phoff easily; a
             * simpler equivalent: bias = lowest PT_LOAD's runtime
             * vaddr - lowest PT_LOAD's link-time vaddr.
             *
             * Since we can't directly observe runtime PT_LOAD addr
             * without re-deriving from the kernel mapping, take a
             * pragmatic shortcut: AT_PHDR is the program's own
             * runtime phdr address; the link-time phdr offset is
             * stored in the PT_PHDR phdr we'd find above.  For
             * static-PIE binaries the bias equals
             * (AT_PHDR - link_time_phdr_offset).  For now we
             * assume the simplest case (PIE built such that the
             * lowest PT_LOAD has vaddr=0) and report bias = first
             * PT_LOAD's runtime address as discovered via the
             * standard "lowest PT_LOAD vaddr is 0" PIE convention. */
        }
        if (p->p_type == PT_DYNAMIC) {
            dyn_ph = p;
        }
        if (p->p_type == PT_LOAD && p->p_vaddr < ph_load_min) {
            ph_load_min = p->p_vaddr;
        }
    }

    if (!dyn_ph) return 0;

    /* PIE convention: the lowest PT_LOAD has p_vaddr near 0, so
     * the runtime load bias equals (AT_PHDR - some constant).  A
     * safer-and-simpler computation: walk the phdrs to find PT_PHDR
     * if present; otherwise infer bias from AT_PHDR vs the first
     * PT_LOAD's link-time vaddr at offset 0 in the file.  For Phase
     * 2 we just report PT_DYNAMIC's link-time vaddr; the caller
     * can adjust once we wire the bias computation. */
    ld_u32 bias = 0;
    /* Try PT_PHDR -> bias = AT_PHDR - phdr_phdr->p_vaddr */
    for (ld_u32 i = 0; i < a->phnum; i++) {
        Elf32_Phdr *p = (Elf32_Phdr *)((char *)ph + i * a->phent);
        if (p->p_type == PT_PHDR) {
            bias = a->phdr - p->p_vaddr;
            break;
        }
    }
    /* No PT_PHDR — fall back to assuming the first PT_LOAD has
     * p_vaddr = 0, so bias = AT_PHDR - link_time_phdr_vaddr.
     * Without the file header we can't read e_phoff; but the
     * program header table lives at a fixed file offset that maps
     * inside the first PT_LOAD.  A common heuristic: if the
     * lowest PT_LOAD vaddr is 0 (typical PIE), bias is
     * AT_PHDR rounded down to the nearest page. */
    if (bias == 0 && ph_load_min_vaddr == 0) {
        bias = a->phdr & ~(a->pagesz - 1);
    }

    *load_bias_out = bias;
    return (Elf32_Dyn *)(dyn_ph->p_vaddr + bias);
}

/* Walk the dynamic table and report what we found.  Phase 2 just
 * prints; Phase 3 will store this in a per-object descriptor and
 * actually use it. */
static void summarize_dynamic(Elf32_Dyn *dyn, ld_u32 bias) {
    ld_u32 needed_count = 0;
    ld_u32 strtab = 0;
    ld_u32 strsz  = 0;
    ld_u32 symtab = 0;
    ld_u32 hash = 0;
    ld_u32 gnu_hash = 0;
    ld_u32 rel = 0;
    ld_u32 relsz = 0;
    ld_u32 jmprel = 0;
    ld_u32 pltrelsz = 0;

    for (Elf32_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_NEEDED:    needed_count++;        break;
        case DT_STRTAB:    strtab = d->d_un.d_ptr; break;
        case DT_STRSZ:     strsz  = d->d_un.d_val; break;
        case DT_SYMTAB:    symtab = d->d_un.d_ptr; break;
        case DT_HASH:      hash   = d->d_un.d_ptr; break;
        case DT_GNU_HASH:  gnu_hash = d->d_un.d_ptr; break;
        case DT_REL:       rel    = d->d_un.d_ptr; break;
        case DT_RELSZ:     relsz  = d->d_un.d_val; break;
        case DT_JMPREL:    jmprel = d->d_un.d_ptr; break;
        case DT_PLTRELSZ:  pltrelsz = d->d_un.d_val; break;
        }
    }

    ld_puts("ld.so: program PT_DYNAMIC at "); ld_putx((ld_u32)(unsigned long)dyn); ld_puts("\n");
    ld_puts("ld.so:   load bias = "); ld_putx(bias); ld_puts("\n");
    ld_puts("ld.so:   DT_NEEDED count = "); ld_putd(needed_count); ld_puts("\n");
    if (strtab) { ld_puts("ld.so:   DT_STRTAB  = "); ld_putx(strtab + bias); ld_puts(" ("); ld_putd(strsz); ld_puts(" bytes)\n"); }
    if (symtab) { ld_puts("ld.so:   DT_SYMTAB  = "); ld_putx(symtab + bias); ld_puts("\n"); }
    if (hash)   { ld_puts("ld.so:   DT_HASH    = "); ld_putx(hash + bias); ld_puts("\n"); }
    if (gnu_hash){ld_puts("ld.so:   DT_GNU_HASH= "); ld_putx(gnu_hash + bias); ld_puts("\n"); }
    if (rel)    { ld_puts("ld.so:   DT_REL     = "); ld_putx(rel + bias); ld_puts(" ("); ld_putd(relsz / 8); ld_puts(" entries)\n"); }
    if (jmprel) { ld_puts("ld.so:   DT_JMPREL  = "); ld_putx(jmprel + bias); ld_puts(" ("); ld_putd(pltrelsz / 8); ld_puts(" entries)\n"); }

    /* List DT_NEEDED entries by index into strtab.  We can't
     * dereference strtab safely until we know it's mapped
     * read-only and validated; for Phase 2 just list the offsets. */
    if (needed_count > 0 && strtab) {
        const char *s = (const char *)(strtab + bias);
        ld_u32 idx = 0;
        for (Elf32_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
            if (d->d_tag == DT_NEEDED) {
                ld_puts("ld.so:   needed[");
                ld_putd(idx++);
                ld_puts("] = ");
                ld_puts(s + d->d_un.d_val);
                ld_puts("\n");
            }
        }
    }
}

ld_u32 ld_main(ld_u32 *initial_stack) {
    ld_auxv_t a;
    parse_auxv(initial_stack, &a);

    ld_puts("ld.so: AT_BASE  = "); ld_putx(a.base);   ld_puts("\n");
    ld_puts("ld.so: AT_ENTRY = "); ld_putx(a.entry);  ld_puts("\n");
    ld_puts("ld.so: AT_PHDR  = "); ld_putx(a.phdr);   ld_puts("\n");
    ld_puts("ld.so: AT_PHNUM = "); ld_putd(a.phnum);  ld_puts("\n");
    ld_puts("ld.so: AT_PHENT = "); ld_putd(a.phent);  ld_puts("\n");
    ld_puts("ld.so: AT_PAGESZ= "); ld_putx(a.pagesz); ld_puts("\n");

    if (a.entry == 0)
        ld_die("AT_ENTRY missing from auxv");
    if (a.phdr == 0 || a.phnum == 0)
        ld_die("AT_PHDR/AT_PHNUM missing from auxv");

    ld_u32 bias = 0;
    Elf32_Dyn *dyn = find_dynamic(&a, &bias);
    if (!dyn) {
        ld_puts("ld.so: program has no PT_DYNAMIC (static-PIE)\n");
        return a.entry;
    }
    summarize_dynamic(dyn, bias);

    /* --- Phase 3 begins ---------------------------------------------
     * Wrap the program itself in an ld_obj_t so the resolver and
     * relocator treat it uniformly with the .so's it pulls in. */
    static ld_obj_t prog_obj;
    /* Copy field-by-field to avoid relying on libc memset semantics
     * inside the freestanding linker. */
    prog_obj.name[0]    = '\0';
    prog_obj.base       = bias;
    prog_obj.dynamic    = dyn;
    prog_obj.strtab     = 0;
    prog_obj.symtab     = 0;
    prog_obj.strsz      = 0;
    prog_obj.gnu_hash   = 0;
    prog_obj.hash       = 0;
    prog_obj.rel        = 0;
    prog_obj.relsz      = 0;
    prog_obj.jmprel     = 0;
    prog_obj.pltrelsz   = 0;
    prog_obj.init       = 0;
    prog_obj.init_array = 0;
    prog_obj.init_arraysz = 0;
    prog_obj.fini       = 0;
    prog_obj.fini_array = 0;
    prog_obj.fini_arraysz = 0;
    prog_obj.tls_image  = 0;
    prog_obj.tls_filesz = 0;
    prog_obj.tls_memsz  = 0;
    prog_obj.tls_align  = 1;
    prog_obj.tls_offset = 0;
    prog_obj.next       = 0;

    /* Detect PT_TLS in the program's phdrs.  AT_PHDR is the
     * program's own phdr table at runtime; iterate by AT_PHNUM. */
    {
        Elf32_Phdr *php = (Elf32_Phdr *)a.phdr;
        for (ld_u32 i = 0; i < a.phnum; i++) {
            Elf32_Phdr *pp = (Elf32_Phdr *)((char *)php + i * a.phent);
            if (pp->p_type == PT_TLS) {
                prog_obj.tls_image  = (const void *)(pp->p_vaddr + bias);
                prog_obj.tls_filesz = pp->p_filesz;
                prog_obj.tls_memsz  = pp->p_memsz;
                prog_obj.tls_align  = pp->p_align ? pp->p_align : 1;
                break;
            }
        }
    }
    {
        const char p_name[] = "main-program";
        for (ld_size i = 0; i < sizeof(p_name); i++) prog_obj.name[i] = p_name[i];
    }
    /* Cache dynamic-table pointers on the program too. */
    {
        ld_u32 strtab_off=0, symtab_off=0, hash_off=0, gnu_hash_off=0;
        ld_u32 rel_off=0, jmprel_off=0;
        ld_u32 init_off=0, fini_off=0, init_arr_off=0, fini_arr_off=0;
        for (Elf32_Dyn *d = dyn; d->d_tag != DT_NULL; d++) {
            switch (d->d_tag) {
            case DT_STRTAB:   strtab_off   = d->d_un.d_ptr; break;
            case DT_STRSZ:    prog_obj.strsz    = d->d_un.d_val; break;
            case DT_SYMTAB:   symtab_off   = d->d_un.d_ptr; break;
            case DT_HASH:     hash_off     = d->d_un.d_ptr; break;
            case DT_GNU_HASH: gnu_hash_off = d->d_un.d_ptr; break;
            case DT_REL:      rel_off      = d->d_un.d_ptr; break;
            case DT_RELSZ:    prog_obj.relsz    = d->d_un.d_val; break;
            case DT_JMPREL:   jmprel_off   = d->d_un.d_ptr; break;
            case DT_PLTRELSZ: prog_obj.pltrelsz = d->d_un.d_val; break;
            case DT_INIT:         init_off     = d->d_un.d_ptr; break;
            case DT_FINI:         fini_off     = d->d_un.d_ptr; break;
            case DT_INIT_ARRAY:   init_arr_off = d->d_un.d_ptr; break;
            case DT_INIT_ARRAYSZ: prog_obj.init_arraysz = d->d_un.d_val; break;
            case DT_FINI_ARRAY:   fini_arr_off = d->d_un.d_ptr; break;
            case DT_FINI_ARRAYSZ: prog_obj.fini_arraysz = d->d_un.d_val; break;
            }
        }
        prog_obj.strtab   = strtab_off   ? (const char *)(strtab_off   + bias) : 0;
        prog_obj.symtab   = symtab_off   ? (Elf32_Sym  *)(symtab_off   + bias) : 0;
        prog_obj.hash     = hash_off     ? (ld_u32     *)(hash_off     + bias) : 0;
        prog_obj.gnu_hash = gnu_hash_off ? (ld_u32     *)(gnu_hash_off + bias) : 0;
        prog_obj.rel      = rel_off      ? (Elf32_Rel  *)(rel_off      + bias) : 0;
        prog_obj.jmprel   = jmprel_off   ? (Elf32_Rel  *)(jmprel_off   + bias) : 0;
        prog_obj.init       = init_off       ? (void (*)(void))(init_off       + bias) : 0;
        prog_obj.fini       = fini_off       ? (void (*)(void))(fini_off       + bias) : 0;
        prog_obj.init_array = init_arr_off   ? (void (**)(void))(init_arr_off  + bias) : 0;
        prog_obj.fini_array = fini_arr_off   ? (void (**)(void))(fini_arr_off  + bias) : 0;
    }

    /* Splice the program into the loaded-object list head so the
     * resolver scans it first per the standard ELF lookup order. */
    extern void ld_obj_prepend(ld_obj_t *o);   /* see ld_load.c */
    ld_obj_prepend(&prog_obj);

    /* BFS over the loaded-object list resolving DT_NEEDED.  Walk
     * the list head-to-tail; for each object, load every entry in
     * its DT_NEEDED.  ld_load_object dedup's by SONAME and appends
     * new objects to the same list — the loop sees them and
     * processes their dependencies in turn.  Terminates when we
     * walk off the end (every reachable DT_NEEDED has been loaded
     * and its own deps already queued).
     *
     * Per ELF lookup order the program comes first; libraries are
     * appended in BFS order matching what the spec requires for
     * symbol resolution scope. */
    for (ld_obj_t *cur = ld_obj_list(); cur; cur = cur->next) {
        if (!cur->dynamic || !cur->strtab) continue;
        for (Elf32_Dyn *d = cur->dynamic; d->d_tag != DT_NULL; d++) {
            if (d->d_tag != DT_NEEDED) continue;
            const char *soname = cur->strtab + d->d_un.d_val;
            ld_obj_t *o = ld_load_object(soname);
            if (!o) {
                ld_puts("ld.so: ");
                ld_puts(cur->name);
                ld_puts(" needs ");
                ld_puts(soname);
                ld_puts(" — not found\n");
                ld_die("DT_NEEDED resolution failed");
            }
            ld_puts("ld.so: loaded ");
            ld_puts(o->name);
            ld_puts(" (needed by ");
            ld_puts(cur->name);
            ld_puts(") at base ");
            ld_putx(o->base);
            ld_puts("\n");
        }
    }

    /* Lay out per-thread TLS BEFORE relocations so TLS_TPOFF reloc
     * sites can reference each module's tls_offset. */
    if (ld_setup_tls() != 0) {
        ld_die("TLS setup failed");
    }

    /* Apply relocations across all loaded objects (program + libs). */
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        ld_puts("ld.so: relocating "); ld_puts(o->name); ld_puts("\n");
        if (ld_relocate(o) != 0) {
            ld_die("relocation failed");
        }
    }

    /* Run constructors in dependency order: deepest deps first
     * (= reverse of load order, since the program is at the head
     * and its deepest dependency is at the tail). */
    ld_run_init_arrays();

    ld_puts("ld.so: handoff to program entry\n");
    return a.entry;
}

/* Walk loaded-object list, gather pointers into a small fixed
 * array, then iterate in reverse so deepest deps run first.  Each
 * object's DT_INIT runs before its DT_INIT_ARRAY (per ABI), and
 * within the array entries fire in declared order.  The program
 * itself is included — it may carry its own __attribute__((ctor))
 * functions even without static C++ globals. */
void ld_run_init_arrays(void) {
    ld_obj_t *stack[LD_MAX_OBJS_INIT_LIMIT];
    ld_size n = 0;
    for (ld_obj_t *o = ld_obj_list(); o; o = o->next) {
        if (n >= LD_MAX_OBJS_INIT_LIMIT) break;
        stack[n++] = o;
    }
    /* Reverse-iterate so the last-loaded (deepest) object's
     * constructors run first. */
    for (ld_size i = n; i > 0; i--) {
        ld_obj_t *o = stack[i - 1];
        if (o->init) {
            ld_puts("ld.so: init "); ld_puts(o->name); ld_puts(" (DT_INIT)\n");
            o->init();
        }
        if (o->init_array && o->init_arraysz >= sizeof(void (*)(void))) {
            ld_u32 cnt = o->init_arraysz / sizeof(void (*)(void));
            ld_puts("ld.so: init "); ld_puts(o->name);
            ld_puts(" (DT_INIT_ARRAY x"); ld_putd(cnt); ld_puts(")\n");
            for (ld_u32 j = 0; j < cnt; j++) {
                if (o->init_array[j]) o->init_array[j]();
            }
        }
    }
}
