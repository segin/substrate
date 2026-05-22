/*
 * ld.h — internal types for /sbin/ld.so.
 *
 * Phase 2 surface: enough of the ELF / auxv types to walk the
 * program's PT_DYNAMIC.  We deliberately do NOT include any
 * Substrate libc headers; the linker is freestanding.
 */

#ifndef _LD_SO_LD_H
#define _LD_SO_LD_H

typedef unsigned int   ld_u32;
typedef int            ld_i32;
typedef unsigned short ld_u16;
typedef unsigned long  ld_size;

/* ELF32 program header — System V ELF spec Fig. 2-1 */
typedef struct {
    ld_u32 p_type;
    ld_u32 p_offset;
    ld_u32 p_vaddr;
    ld_u32 p_paddr;
    ld_u32 p_filesz;
    ld_u32 p_memsz;
    ld_u32 p_flags;
    ld_u32 p_align;
} Elf32_Phdr;

/* ELF32 dynamic entry */
typedef struct {
    ld_i32 d_tag;
    union { ld_u32 d_val; ld_u32 d_ptr; } d_un;
} Elf32_Dyn;

/* ELF32 REL relocation */
typedef struct {
    ld_u32 r_offset;
    ld_u32 r_info;
} Elf32_Rel;

#define ELF32_R_TYPE(i) ((i) & 0xff)
#define ELF32_R_SYM(i)  ((i) >> 8)

/* Dynamic tags we use in Phase 2.  Full list lands in Phase 3. */
#define DT_NULL     0
#define DT_NEEDED   1
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_STRSZ   10
#define DT_SYMENT  11
#define DT_REL     17
#define DT_RELSZ   18
#define DT_RELENT  19
#define DT_PLTREL  20
#define DT_DEBUG   21
#define DT_TEXTREL 22
#define DT_JMPREL  23
#define DT_GNU_HASH 0x6ffffef5
#define DT_PLTRELSZ 2
#define DT_PLTGOT   3
#define DT_SONAME  14
#define DT_RPATH   15
#define DT_RUNPATH 29
#define DT_INIT    12
#define DT_FINI    13
#define DT_INIT_ARRAY    25
/* GNU symbol versioning — required to load libstdc++.so.6 and any
 * other DSO that uses versioned symbols (GLIBCXX_3.4 etc.). */
#define DT_VERSYM      0x6ffffff0   /* Per-symbol version index table */
#define DT_VERDEF      0x6ffffffc   /* Verdef array (what we provide) */
#define DT_VERDEFNUM   0x6ffffffd
#define DT_VERNEED     0x6ffffffe   /* Verneed array (what we require) */
#define DT_VERNEEDNUM  0x6fffffff
#define DT_FINI_ARRAY    26
#define DT_INIT_ARRAYSZ  27
#define DT_FINI_ARRAYSZ  28

/* Program-header types */
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_PHDR     6
#define PT_TLS      7

/* i386 relocation types — only R_386_RELATIVE used by self-reloc */
#define R_386_NONE     0
#define R_386_32       1
#define R_386_PC32     2
#define R_386_GOT32    3
#define R_386_PLT32    4
#define R_386_COPY     5
#define R_386_GLOB_DAT 6
#define R_386_JMP_SLOT 7
#define R_386_RELATIVE 8

/* i386 TLS relocations.  Phase 4c supports the "local exec" model
 * (initial-exec / TPOFF) which is what static-PIE programs and
 * non-dlopen shared libs emit for `__thread` accesses. */
#define R_386_TLS_TPOFF   14   /* offset from thread pointer */
#define R_386_TLS_IE      15   /* offset via GOT */
#define R_386_TLS_GOTIE   16
#define R_386_TLS_LE      17
#define R_386_TLS_GD      18
#define R_386_TLS_LDM     19
#define R_386_TLS_DTPMOD32 35  /* module id of GD/LD tls_index slot */
#define R_386_TLS_DTPOFF32 36  /* offset within module for GD/LD tls_index */

/* Auxv entries used in Phase 2 */
#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_FLAGS   8
#define AT_ENTRY   9
#define AT_PLATFORM 15
#define AT_EXECFN  31

/* Native syscall numbers we actually issue from the linker. */
#define SYS_exit   1
#define SYS_read   3
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_lseek 19
#define SYS_mmap        90
#define SYS_fstat      108
#define SYS_set_gsbase 274

/* mmap flags / prot bits we use. */
#define LD_PROT_READ   1
#define LD_PROT_WRITE  2
#define LD_PROT_EXEC   4
#define LD_MAP_PRIVATE 0x002
#define LD_MAP_FIXED   0x010
#define LD_MAP_ANON    0x020

/* open() flags */
#define LD_O_RDONLY 0

/* ELF32 file header — only the fields ld.so reads at load time. */
typedef struct {
    unsigned char e_ident[16];
    ld_u16 e_type;
    ld_u16 e_machine;
    ld_u32 e_version;
    ld_u32 e_entry;
    ld_u32 e_phoff;
    ld_u32 e_shoff;
    ld_u32 e_flags;
    ld_u16 e_ehsize;
    ld_u16 e_phentsize;
    ld_u16 e_phnum;
    ld_u16 e_shentsize;
    ld_u16 e_shnum;
    ld_u16 e_shstrndx;
} Elf32_Ehdr;

/* ELF32 symbol */
typedef struct {
    ld_u32 st_name;
    ld_u32 st_value;
    ld_u32 st_size;
    unsigned char st_info;
    unsigned char st_other;
    ld_u16 st_shndx;
} Elf32_Sym;

#define ELF32_ST_BIND(i) ((i) >> 4)
#define ELF32_ST_TYPE(i) ((i) & 0xf)
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2
#define STN_UNDEF  0
#define SHN_UNDEF  0

/* GNU symbol versioning — DT_VERDEF / DT_VERNEED / DT_VERSYM blocks.
 * Layout per glibc / binutils elf/external.h.  Strings live in the
 * dynamic strtab; the version-name HASH is computed via the standard
 * ELF hash function (NOT the GNU hash). */
typedef ld_u16 Elf32_Half;

typedef struct {
    Elf32_Half vd_version;  /* always 1 */
    Elf32_Half vd_flags;    /* VER_FLG_BASE (1) for the base def slot */
    Elf32_Half vd_ndx;      /* version index (1..N), matches VERSYM */
    Elf32_Half vd_cnt;      /* number of vd_aux entries (>=1) */
    ld_u32     vd_hash;     /* ELF hash of the version name */
    ld_u32     vd_aux;      /* byte offset to first Elf32_Verdaux */
    ld_u32     vd_next;     /* byte offset to next Elf32_Verdef (0=end) */
} Elf32_Verdef;

typedef struct {
    ld_u32     vda_name;    /* offset into strtab — version name */
    ld_u32     vda_next;    /* byte offset to next aux (0=end) */
} Elf32_Verdaux;

typedef struct {
    Elf32_Half vn_version;  /* always 1 */
    Elf32_Half vn_cnt;      /* number of vn_aux entries */
    ld_u32     vn_file;     /* offset into strtab — providing soname */
    ld_u32     vn_aux;      /* byte offset to first Elf32_Vernaux */
    ld_u32     vn_next;     /* byte offset to next Elf32_Verneed (0=end) */
} Elf32_Verneed;

typedef struct {
    ld_u32     vna_hash;    /* ELF hash of the required version name */
    Elf32_Half vna_flags;
    Elf32_Half vna_other;   /* version index — matches VERSYM in this DSO */
    ld_u32     vna_name;    /* offset into strtab — version name */
    ld_u32     vna_next;    /* byte offset to next aux (0=end) */
} Elf32_Vernaux;

/* VERSYM is a Half[] parallel to .dynsym.  Index meanings:
 *   0 = VER_NDX_LOCAL    (symbol not exported)
 *   1 = VER_NDX_GLOBAL   (no version assigned — base def)
 *   N = a verdef vd_ndx (exporter) or vernaux vna_other (importer)
 * The high bit 0x8000 is the HIDDEN flag: when set on a defined
 * symbol, the symbol is NOT eligible to satisfy an unversioned
 * (or differently-versioned) lookup.  Strip with VER_NDX(). */
#define VER_NDX_LOCAL    0
#define VER_NDX_GLOBAL   1
#define VER_NDX_HIDDEN   0x8000
#define VER_NDX(v)       ((v) & 0x7fff)
#define VER_IS_HIDDEN(v) (((v) & VER_NDX_HIDDEN) != 0)
#define VER_FLG_BASE     1
#define VER_FLG_WEAK     2

/* TLS module descriptor passed to __tls_get_addr() for GD/LD models.
 * On i386 the call is regparm(1) — pointer in %eax — but our exported
 * symbol is plain cdecl since GCC emits a normal call instruction
 * with the pointer pushed on the stack for the GD sequence. */
typedef struct {
    ld_u32 ti_module;
    ld_u32 ti_offset;
} tls_index;

/* Tiny IO helpers — implemented in ld_io.c. */
void  ld_write(int fd, const char *buf, ld_size len);
void  ld_puts(const char *s);
void  ld_putx(ld_u32 v);
void  ld_putd(ld_u32 v);
void  ld_die(const char *msg) __attribute__((noreturn));

/* When non-zero, emit the verbose loading / relocating / TLS / etc.
 * trace.  Set from LD_DEBUG=<anything> in envp.  Errors and the
 * LD_TRACE_LOADED_OBJECTS dump are NOT gated by this — they go to
 * stderr / stdout regardless. */
extern int ld_debug;
#define LD_DBG(stmt) do { if (ld_debug) { stmt; } } while (0)

/* Filesystem + mapping syscalls.  Return -errno on failure. */
int   ld_open(const char *path, int flags);
int   ld_close(int fd);
long  ld_read(int fd, void *buf, ld_size n);
long  ld_lseek(int fd, long off, int whence);
void *ld_mmap(void *addr, ld_size len, int prot, int flags,
              int fd, ld_u32 page_off);

/* Phase-3 surface: per-loaded-object descriptor.  Both the program
 * itself and every loaded .so get one of these.  The list is kept
 * in load order for deterministic symbol resolution. */
typedef struct ld_obj {
    char            name[64];   /* SONAME or basename, for diagnostics */
    ld_u32          base;       /* load bias */
    ld_u32          load_start; /* low end of PT_LOAD span (absolute) */
    ld_u32          load_end;   /* high end of PT_LOAD span (absolute) */
    Elf32_Dyn      *dynamic;    /* PT_DYNAMIC pointer (already biased) */

    /* Cached dynamic-table pointers (all already biased). */
    const char     *strtab;
    Elf32_Sym      *symtab;
    ld_u32          strsz;

    ld_u32         *gnu_hash;   /* DT_GNU_HASH (preferred) */
    ld_u32         *hash;       /* DT_HASH (fallback) */

    Elf32_Rel      *rel;        /* DT_REL */
    ld_u32          relsz;      /* bytes */
    Elf32_Rel      *jmprel;     /* DT_JMPREL */
    ld_u32          pltrelsz;   /* bytes */

    /* Initializers / finalizers (DT_INIT, DT_INIT_ARRAY, ...). */
    void          (*init)(void);
    void          (**init_array)(void);
    ld_u32          init_arraysz;   /* bytes — count = sz / sizeof(fn ptr) */
    void          (*fini)(void);
    void          (**fini_array)(void);
    ld_u32          fini_arraysz;

    /* PT_TLS metadata, populated when an object carries a thread-
     * local segment.  `tls_offset` is the negative offset from the
     * thread pointer at which this module's TLS image lives in the
     * combined per-thread block — assigned by ld_setup_tls(). */
    const void     *tls_image;      /* file-image (PT_TLS at p_offset+base) */
    ld_u32          tls_filesz;
    ld_u32          tls_memsz;
    ld_u32          tls_align;
    ld_u32          tls_offset;     /* abs(offset) below thread pointer */

    /* Per-object guards.  R_386_RELATIVE is `*p += base` —
     * non-idempotent — so re-running ld_relocate on an already-
     * relocated object would double the bias and silently corrupt
     * every relative pointer (notably DT_FINI_ARRAY entries).
     * Same concern for init/fini arrays which must fire exactly
     * once. */
    int             relocated;
    int             copy_relocated; /* R_386_COPY final pass done */
    int             initialized;
    int             finalized;

    /* Phase 5 (C++ linkage): GNU symbol-versioning sections.  All
     * three are biased pointers into the loaded image.  NULL when
     * the DSO doesn't carry versioning (substrate libc, libm, etc.
     * currently don't — libstdc++.so.6 does). */
    Elf32_Half     *versym;     /* DT_VERSYM — parallel to symtab */
    Elf32_Verdef   *verdef;     /* DT_VERDEF — what we EXPORT */
    Elf32_Verneed  *verneed;    /* DT_VERNEED — what we IMPORT */
    ld_u32          verdefnum;  /* count of verdef entries */
    ld_u32          verneednum; /* count of verneed entries */

    /* Phase 5 (TLS GD/LD): per-DSO module index.  Assigned when the
     * object loads, used as the ti_module field passed to
     * __tls_get_addr() by GD/LD-model relocations.  0 = "no TLS in
     * this object". */
    ld_u32          tls_modid;

    struct ld_obj  *next;
} ld_obj_t;

/* Load a shared object by absolute path.  Returns NULL on failure
 * with a diagnostic via ld_die.  Already-loaded SONAMEs are
 * deduplicated; the cached descriptor is returned. */
ld_obj_t *ld_load_object(const char *path);

/* Walk the loaded-object list looking for `name`.  Returns the
 * symbol's runtime address, or 0 if undefined / weak-undef. */
ld_u32 ld_resolve(const char *name);

/* Same, but skip `skip` while searching.  Used by R_386_COPY which
 * must find the source-of-truth in a SHARED library, not in the
 * executable that's about to receive the copy. */
ld_u32 ld_resolve_skip(const char *name, const ld_obj_t *skip);

/* Version-aware resolution.  When the IMPORTER's VERSYM marks a
 * reference with a non-default version index, the resolver only
 * matches symbols whose VERSYM in the candidate object carries the
 * matching vd_hash (or whose vd_flags has VER_FLG_BASE set,
 * indicating a default version).  vh_hash is the ELF hash of the
 * importer's required-version-name; pass 0 for unversioned
 * lookups (caller wants the default version). */
ld_u32 ld_resolve_versioned(const char *name, ld_u32 vh_hash,
                            const ld_obj_t *skip, ld_u32 *size_out);

/* Standard ELF hash function — re-used for version-name hashing
 * (the SAME function ELF uses for the SysV symbol-name hash table). */
ld_u32 ld_elf_hash(const char *s);

/* Same, but also returns the symbol size (for R_386_COPY).
 * Returns 0 (and *size_out=0) if not found. */
ld_u32 ld_resolve_with_size(const char *name, const ld_obj_t *skip,
                            ld_u32 *size_out);

/* Apply DT_REL and DT_JMPREL on `obj`, EXCEPT R_386_COPY.  Returns
 * 0 on success. */
int ld_relocate(ld_obj_t *obj);

/* Apply the R_386_COPY relocations of `obj` only.  Must run as a
 * final pass after every object has been through ld_relocate(), so
 * the copy source already holds its relocated value. */
int ld_relocate_copy(ld_obj_t *obj);

/* Public head of the loaded-object list. */
ld_obj_t *ld_obj_list(void);

/* Run DT_INIT and DT_INIT_ARRAY for every loaded object in
 * dependency order (deepest deps first, program last).  Idempotent
 * — each object is initialized exactly once via a per-object guard
 * inside the function. */
void ld_run_init_arrays(void);

/* Run DT_FINI_ARRAY then DT_FINI for every loaded object in REVERSE
 * dependency order (program first, deepest deps last).  Called by
 * libc's exit() via the weak `__ldso_run_fini` pointer so that
 * destructors fire before the kernel reaps the process.  Static-
 * linked binaries don't have ld.so loaded, so libc's call resolves
 * to a NULL stub and is a harmless no-op. */
__attribute__((visibility("default")))
void __ldso_run_fini(void);

/* Allocate the per-thread TLS region, copy each loaded object's
 * PT_TLS image into it, install the GS base via the new native
 * sys_set_gsbase syscall.  Returns 0 on success or a negative
 * errno.  Called once after relocations and before init arrays. */
int ld_setup_tls(void);

/* Native syscall: install a TLS base for the current thread.
 * Returns 0 on success or -errno. */
int ld_sys_set_gsbase(ld_u32 base);

/* Per-process upper bound on objects we'll iterate during init.
 * Sized to match LD_MAX_OBJS in ld_load.c so it's effectively the
 * same limit, kept here so ld_main can size a stack array without
 * pulling in ld_load.c's private constants. */
#define LD_MAX_OBJS_INIT_LIMIT 32

/* Phase 2 entry point from asm. */
ld_u32 ld_main(ld_u32 *initial_stack);

#endif /* _LD_SO_LD_H */
