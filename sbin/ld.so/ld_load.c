/*
 * ld_load.c — load a shared object into memory.
 *
 * Phase 3 strategy: open the file, read its ELF header off the
 * first page, walk PT_LOAD entries, mmap each segment with the
 * right protection bits, then cache the dynamic-table pointers
 * (DT_*) into a ld_obj_t record for the resolver and relocator.
 *
 * Limits we accept for the first cut:
 *   - Up to LD_MAX_OBJS loaded shared objects (program counts as
 *     one).  Plenty for an init-style boot.
 *   - SONAME-based dedup: if a .so with this DT_SONAME is already
 *     loaded, return the cached descriptor.  Loaders without a
 *     SONAME fall back to basename matching.
 *   - No DT_RPATH / DT_RUNPATH yet — search /lib, /usr/lib, then
 *     LD_LIBRARY_PATH (later).
 *   - Each PT_LOAD must be page-aligned in p_vaddr / p_offset
 *     (per the ELF spec).
 */

#include "ld.h"

#define LD_MAX_OBJS 192   /* was 32; a full GTK+ 2.x app loads ~40-60 DSOs */
#define PAGE_SIZE   0x1000

static ld_obj_t  ld_obj_pool[LD_MAX_OBJS];
static ld_size   ld_obj_count = 0;
static ld_obj_t *ld_obj_head = 0;
static ld_obj_t *ld_obj_tail = 0;

/* Built-in (trusted) system search paths, always searched first.
 * Extending the list is just adding an entry — keep terminating NULL. */
static const char *const ld_search_paths[] = {
    "/lib",
    "/usr/lib",
    "/usr/local/lib",
    0,
};

/*
 * Additional search directories from /etc/ld.so.conf — the system
 * default library path (the moral equivalent of a baked-in
 * LD_LIBRARY_PATH).  Parsed once, lazily, on the first object load.
 * Format: one absolute directory per line; blank lines and lines
 * beginning with '#' are comments.  (`include` / glob directives are
 * not supported — ld.so has no directory globbing — and are ignored.)
 * Directory strings point into ld_conf_buf, which persists for the
 * lifetime of the process.
 */
#define LD_CONF_MAX_DIRS 32
#define LD_CONF_BUF      4096
static char        ld_conf_buf[LD_CONF_BUF];
static const char *ld_conf_dirs[LD_CONF_MAX_DIRS + 1];
static int         ld_conf_loaded = 0;

static ld_size ld_strlen(const char *s) {
    const char *p = s;
    while (*p) p++;
    return (ld_size)(p - s);
}

static int ld_strncmp(const char *a, const char *b, ld_size n) {
    while (n--) {
        if (*a != *b) return (int)((unsigned char)*a) - (int)((unsigned char)*b);
        if (!*a) return 0;
        a++; b++;
    }
    return 0;
}

static int ld_streq(const char *a, const char *b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void ld_strncpy(char *dst, const char *src, ld_size cap) {
    ld_size i = 0;
    if (cap == 0) return;
    for (; i + 1 < cap && src[i]; i++) dst[i] = src[i];
    dst[i] = '\0';
}

/* Append `right` onto `left/`.  Returns total length, or -1 if it
 * doesn't fit in `cap`. */
static int ld_path_join(char *out, ld_size cap,
                        const char *left, const char *right) {
    ld_size ll = ld_strlen(left);
    ld_size lr = ld_strlen(right);
    if (ll + 1 + lr + 1 > cap) return -1;
    for (ld_size i = 0; i < ll; i++) out[i] = left[i];
    out[ll] = '/';
    for (ld_size i = 0; i < lr; i++) out[ll + 1 + i] = right[i];
    out[ll + 1 + lr] = '\0';
    return (int)(ll + 1 + lr);
}

/* Look up a previously-loaded object by its SONAME (or basename
 * if SONAME absent).  Returns NULL if not present. */
static ld_obj_t *find_loaded(const char *name) {
    for (ld_obj_t *o = ld_obj_head; o; o = o->next) {
        if (ld_streq(o->name, name)) return o;
    }
    return 0;
}

/* Walk an Elf32_Dyn array, populating the cached pointers in `o`.
 * `o->base` and `o->dynamic` must already be set; everything else
 * is filled here.  We compute SONAME late because we need DT_STRTAB
 * first. */
static void ld_cache_dynamic(ld_obj_t *o) {
    ld_u32 strtab_off = 0;
    ld_u32 symtab_off = 0;
    ld_u32 rel_off = 0;
    ld_u32 jmprel_off = 0;
    ld_u32 hash_off = 0;
    ld_u32 gnu_hash_off = 0;
    ld_u32 init_off = 0;
    ld_u32 fini_off = 0;
    ld_u32 init_arr_off = 0;
    ld_u32 fini_arr_off = 0;
    ld_u32 soname_str = 0;
    int    soname_seen = 0;
    ld_u32 versym_off = 0, verdef_off = 0, verneed_off = 0;

    for (Elf32_Dyn *d = o->dynamic; d->d_tag != DT_NULL; d++) {
        switch (d->d_tag) {
        case DT_STRTAB:    strtab_off = d->d_un.d_ptr; break;
        case DT_STRSZ:     o->strsz   = d->d_un.d_val; break;
        case DT_SYMTAB:    symtab_off = d->d_un.d_ptr; break;
        case DT_HASH:      hash_off   = d->d_un.d_ptr; break;
        case DT_GNU_HASH:  gnu_hash_off = d->d_un.d_ptr; break;
        case DT_REL:       rel_off    = d->d_un.d_ptr; break;
        case DT_RELSZ:     o->relsz   = d->d_un.d_val; break;
        case DT_JMPREL:    jmprel_off = d->d_un.d_ptr; break;
        case DT_PLTRELSZ:  o->pltrelsz = d->d_un.d_val; break;
        case DT_INIT:      init_off   = d->d_un.d_ptr; break;
        case DT_FINI:      fini_off   = d->d_un.d_ptr; break;
        case DT_INIT_ARRAY:    init_arr_off = d->d_un.d_ptr; break;
        case DT_INIT_ARRAYSZ:  o->init_arraysz = d->d_un.d_val; break;
        case DT_FINI_ARRAY:    fini_arr_off = d->d_un.d_ptr; break;
        case DT_FINI_ARRAYSZ:  o->fini_arraysz = d->d_un.d_val; break;
        case DT_SONAME:    soname_str = d->d_un.d_val; soname_seen = 1; break;
        case DT_VERSYM:    versym_off  = d->d_un.d_ptr; break;
        case DT_VERDEF:    verdef_off  = d->d_un.d_ptr; break;
        case DT_VERDEFNUM: o->verdefnum  = d->d_un.d_val; break;
        case DT_VERNEED:   verneed_off = d->d_un.d_ptr; break;
        case DT_VERNEEDNUM: o->verneednum = d->d_un.d_val; break;
        }
    }

    o->strtab   = strtab_off   ? (const char *)(strtab_off   + o->base) : 0;
    o->symtab   = symtab_off   ? (Elf32_Sym  *)(symtab_off   + o->base) : 0;
    o->hash     = hash_off     ? (ld_u32     *)(hash_off     + o->base) : 0;
    o->gnu_hash = gnu_hash_off ? (ld_u32     *)(gnu_hash_off + o->base) : 0;
    o->rel      = rel_off      ? (Elf32_Rel  *)(rel_off      + o->base) : 0;
    o->jmprel   = jmprel_off   ? (Elf32_Rel  *)(jmprel_off   + o->base) : 0;
    o->init       = init_off       ? (void (*)(void))(init_off       + o->base) : 0;
    o->fini       = fini_off       ? (void (*)(void))(fini_off       + o->base) : 0;
    o->init_array = init_arr_off   ? (void (**)(void))(init_arr_off  + o->base) : 0;
    o->fini_array = fini_arr_off   ? (void (**)(void))(fini_arr_off  + o->base) : 0;
    o->versym  = versym_off  ? (Elf32_Half    *)(versym_off  + o->base) : 0;
    o->verdef  = verdef_off  ? (Elf32_Verdef  *)(verdef_off  + o->base) : 0;
    o->verneed = verneed_off ? (Elf32_Verneed *)(verneed_off + o->base) : 0;

    if (soname_seen && o->strtab && o->name[0] == '\0') {
        ld_strncpy(o->name, o->strtab + soname_str, sizeof(o->name));
    }
}

/* Append `o` to the global loaded-object list, preserving order. */
void ld_obj_append(ld_obj_t *o) {
    o->next = 0;
    if (!ld_obj_head) ld_obj_head = o;
    if (ld_obj_tail) ld_obj_tail->next = o;
    ld_obj_tail = o;
}

/* Load a single .so file from `path` into memory.  Returns the
 * descriptor or NULL on failure. */
static ld_obj_t *load_from_path(const char *path) {
    if (ld_obj_count >= LD_MAX_OBJS) return 0;

    int fd = ld_open(path, LD_O_RDONLY);
    if (fd < 0) return 0;

    /* Read just the file header so we can locate phdrs. */
    Elf32_Ehdr eh;
    if (ld_read(fd, &eh, sizeof(eh)) != (long)sizeof(eh)) {
        ld_close(fd);
        return 0;
    }
    if (eh.e_ident[0] != 0x7f || eh.e_ident[1] != 'E' ||
        eh.e_ident[2] != 'L'  || eh.e_ident[3] != 'F') {
        ld_close(fd);
        return 0;
    }
    if (eh.e_type != 3 /* ET_DYN */ || eh.e_machine != 3 /* EM_386 */) {
        ld_close(fd);
        return 0;
    }
    if (eh.e_phnum == 0 || eh.e_phentsize != sizeof(Elf32_Phdr)) {
        ld_close(fd);
        return 0;
    }

    /* Read the program header table.  Bound it so a malicious
     * file can't ask for arbitrary memory. */
    if (eh.e_phnum > 64) {
        ld_close(fd);
        return 0;
    }
    Elf32_Phdr ph[64];
    if (ld_lseek(fd, (long)eh.e_phoff, 0) < 0) { ld_close(fd); return 0; }
    ld_size ph_bytes = (ld_size)eh.e_phnum * sizeof(Elf32_Phdr);
    if (ld_read(fd, ph, ph_bytes) != (long)ph_bytes) {
        ld_close(fd);
        return 0;
    }

    /* Compute the spanning range of all PT_LOAD segments so we can
     * reserve a contiguous run of address space and let the kernel
     * pick the base, then fix up subsequent segments at MAP_FIXED
     * offsets relative to that base. */
    ld_u32 lo = 0xFFFFFFFFu, hi = 0;
    int    have_load = 0;
    for (int i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        have_load = 1;
        ld_u32 vstart = ph[i].p_vaddr & ~(PAGE_SIZE - 1);
        ld_u32 vend   = (ph[i].p_vaddr + ph[i].p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        if (vstart < lo) lo = vstart;
        if (vend   > hi) hi = vend;
    }
    if (!have_load) { ld_close(fd); return 0; }

    /* Anonymous reserve for the whole span — we'll overwrite each
     * PT_LOAD with MAP_FIXED + file backing.  This guarantees we
     * get a contiguous address range. */
    ld_size span = (ld_size)(hi - lo);
    void *base_v = ld_mmap(0, span, LD_PROT_READ,
                           LD_MAP_PRIVATE | LD_MAP_ANON, -1, 0);
    if (ld_mmap_failed(base_v)) { ld_close(fd); return 0; }
    ld_u32 base = (ld_u32)(unsigned long)base_v - lo;

    /* Map each PT_LOAD over the reserved range. */
    for (int i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        int prot = 0;
        if (ph[i].p_flags & 0x1) prot |= LD_PROT_EXEC;
        if (ph[i].p_flags & 0x2) prot |= LD_PROT_WRITE;
        if (ph[i].p_flags & 0x4) prot |= LD_PROT_READ;

        ld_u32 vaddr = (ph[i].p_vaddr + base) & ~(PAGE_SIZE - 1);
        ld_u32 voff  = ph[i].p_vaddr - (ph[i].p_vaddr & ~(PAGE_SIZE - 1));
        ld_u32 fileoff_pages = (ph[i].p_offset & ~(PAGE_SIZE - 1)) / PAGE_SIZE;
        ld_size mapsz = (voff + ph[i].p_filesz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        if (ph[i].p_filesz > 0) {
            void *r = ld_mmap((void *)vaddr, mapsz,
                              prot | LD_PROT_WRITE,
                              LD_MAP_PRIVATE | LD_MAP_FIXED,
                              fd, fileoff_pages);
            if (ld_mmap_failed(r) || r != (void *)vaddr) { ld_close(fd); return 0; }
        }
        /* BSS handling.  Two pieces:
         *   1. Bytes from (p_vaddr + p_filesz) up to the next page
         *      boundary are part of the LAST file page we mapped.
         *      The file mmap brought in whatever happened to live
         *      in the file at that offset (typically the next
         *      segment's contents) — definitely not zero.  Memset
         *      them to 0 so BSS data starts blank.
         *   2. Pages beyond that (vaddr + mapsz onward, up to the
         *      page-aligned end of memsz) get an anonymous
         *      MAP_FIXED mapping — anon pages are kernel-zeroed. */
        if (ph[i].p_memsz > ph[i].p_filesz) {
            ld_u32 bss_start  = ph[i].p_vaddr + base + ph[i].p_filesz;
            ld_u32 bss_actual_end = ph[i].p_vaddr + base + ph[i].p_memsz;
            ld_u32 file_page_end  = vaddr + mapsz;
            if (bss_start < file_page_end) {
                ld_u32 zlen = (bss_actual_end < file_page_end ? bss_actual_end : file_page_end) - bss_start;
                unsigned char *z = (unsigned char *)(unsigned long)bss_start;
                for (ld_u32 k = 0; k < zlen; k++) z[k] = 0;
            }
            ld_u32 anon_start = file_page_end;
            ld_u32 mem_end    = (bss_actual_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
            if (anon_start < mem_end) {
                void *a = ld_mmap((void *)anon_start, mem_end - anon_start,
                                  prot | LD_PROT_WRITE,
                                  LD_MAP_PRIVATE | LD_MAP_ANON | LD_MAP_FIXED,
                                  -1, 0);
                (void)a;
            }
        }
    }

    ld_close(fd);

    /* Locate PT_DYNAMIC and PT_TLS. */
    Elf32_Dyn *dyn = 0;
    const void *tls_image = 0;
    ld_u32 tls_filesz = 0, tls_memsz = 0, tls_align = 1;
    for (int i = 0; i < eh.e_phnum; i++) {
        if (ph[i].p_type == PT_DYNAMIC) {
            dyn = (Elf32_Dyn *)(ph[i].p_vaddr + base);
        } else if (ph[i].p_type == PT_TLS) {
            tls_image  = (const void *)(ph[i].p_vaddr + base);
            tls_filesz = ph[i].p_filesz;
            tls_memsz  = ph[i].p_memsz;
            tls_align  = ph[i].p_align ? ph[i].p_align : 1;
        }
    }
    if (!dyn) return 0;

    ld_obj_t *o = &ld_obj_pool[ld_obj_count++];
    o->base       = base;
    /* Runtime program-header address + count, for dl_iterate_phdr(3).
     * The phdrs sit at e_phoff inside the first PT_LOAD (which maps file
     * offset 0), so their mapped address is base + e_phoff. */
    o->phdr       = (const void *)(unsigned long)(base + eh.e_phoff);
    o->phnum      = eh.e_phnum;
    /* [load_start, load_end) covers every PT_LOAD page-aligned to
     * the span we just mmap'd.  Used by __ldso_dladdr to figure out
     * which DSO owns an address and by RTLD_NEXT-style scope walks
     * to identify the caller's object. */
    o->load_start = base + lo;
    o->load_end   = base + hi;
    o->dynamic    = dyn;
    o->tls_image  = tls_image;
    o->tls_filesz = tls_filesz;
    o->tls_memsz  = tls_memsz;
    o->tls_align  = tls_align;
    o->tls_offset = 0;          /* assigned by ld_setup_tls */
    /* Take basename from path as a placeholder; SONAME will
     * overwrite it inside ld_cache_dynamic. */
    {
        const char *bn = path;
        for (const char *p = path; *p; p++) if (*p == '/') bn = p + 1;
        ld_strncpy(o->name, bn, sizeof(o->name));
    }
    ld_cache_dynamic(o);
    ld_obj_append(o);
    return o;
}

/* Parse /etc/ld.so.conf into ld_conf_dirs (once).  Best effort: a
 * missing or unreadable file simply yields no extra directories. */
static void ld_conf_load(void) {
    ld_conf_loaded = 1;                 /* attempt only once */

    int fd = ld_open("/etc/ld.so.conf", LD_O_RDONLY);
    if (fd < 0) return;

    long total = 0;
    for (;;) {
        long n = ld_read(fd, ld_conf_buf + total,
                         (ld_size)(LD_CONF_BUF - 1 - total));
        if (n <= 0) break;
        total += n;
        if (total >= LD_CONF_BUF - 1) break;
    }
    ld_close(fd);
    ld_conf_buf[total] = '\0';

    int ndirs = 0;
    char *p = ld_conf_buf;
    while (*p && ndirs < LD_CONF_MAX_DIRS) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')
            p++;
        if (!*p) break;
        char *line = p;
        while (*p && *p != '\n' && *p != '\r')
            p++;
        char *lineend = p;
        if (*p) p++;                    /* step past the line terminator */
        while (lineend > line &&
               (lineend[-1] == ' ' || lineend[-1] == '\t'))
            lineend--;
        *lineend = '\0';

        if (*line == '\0' || *line == '#')
            continue;                   /* blank or comment */
        if (ld_strncmp(line, "include", 7) == 0 &&
            (line[7] == ' ' || line[7] == '\t' || line[7] == '\0'))
            continue;                   /* globbed includes unsupported */
        ld_conf_dirs[ndirs++] = line;
    }
    ld_conf_dirs[ndirs] = 0;
}

/* Resolve a soname against the search paths and load it. */
ld_obj_t *ld_load_object(const char *soname) {
    ld_obj_t *cached = find_loaded(soname);
    if (cached) return cached;

    char path[256];
    /* Absolute paths are tried verbatim. */
    if (soname[0] == '/') {
        return load_from_path(soname);
    }
    /* Built-in trusted directories first. */
    for (int i = 0; ld_search_paths[i]; i++) {
        if (ld_path_join(path, sizeof(path), ld_search_paths[i], soname) < 0)
            continue;
        ld_obj_t *o = load_from_path(path);
        if (o) return o;
    }
    /* Then the system-default directories from /etc/ld.so.conf. */
    if (!ld_conf_loaded) ld_conf_load();
    for (int i = 0; ld_conf_dirs[i]; i++) {
        if (ld_path_join(path, sizeof(path), ld_conf_dirs[i], soname) < 0)
            continue;
        ld_obj_t *o = load_from_path(path);
        if (o) return o;
    }
    return 0;
}

/* Suppress unused-warning for ld_strncmp until the resolver
 * actually uses prefix matches. */
__attribute__((unused))
static void ld_keep_helpers(void) {
    (void)ld_strncmp;
}

/* Public accessor for the loaded-object list — used by the
 * resolver and relocator. */
ld_obj_t *ld_obj_list(void) { return ld_obj_head; }

/* Prepend an externally-allocated descriptor (the program itself)
 * onto the list so the resolver scans the program first per the
 * standard ELF lookup order. */
void ld_obj_prepend(ld_obj_t *o) {
    o->next = ld_obj_head;
    ld_obj_head = o;
    if (!ld_obj_tail) ld_obj_tail = o;
}
