/*
 * ld.h - internal types for the Substrate dynamic linker.
 *
 * One source tree builds two linkers: /sbin/ld.so, the 32-bit one for
 * i386 programs, and /sbin/ld64.so, the 64-bit one for amd64 programs.
 * Everything that depends on the word size is selected here: the ELF
 * structures (Elf_Ehdr, Elf_Phdr, Elf_Dyn, Elf_Sym, Elf_Reloc), the
 * address type (ld_addr), the relocation table the architecture uses
 * (REL on i386, RELA on amd64) and the library search directories.
 *
 * We deliberately do NOT include any Substrate libc headers; the linker
 * is freestanding.
 */

#ifndef _LD_SO_LD_H
#define _LD_SO_LD_H

typedef unsigned int       ld_u32;
typedef int                ld_i32;
typedef unsigned short     ld_u16;
typedef unsigned long long ld_u64;
typedef long long          ld_i64;
typedef unsigned long      ld_size;

/*
 * ld_addr: an address, a load bias, or a size read from an ELF structure
 * of the native class.  On i386 it is the same `unsigned int` the linker
 * has always used, so the 32-bit build is unchanged.
 */
#if defined(__x86_64__)
#define LD_ARCH_AMD64   1
typedef unsigned long      ld_addr;
#elif defined(__i386__)
#define LD_ARCH_I386    1
typedef ld_u32             ld_addr;
#else
#error "ld.so: unsupported architecture"
#endif

#define LD_ADDR_MAX     ((ld_addr)-1)
#define LD_PAGE_SIZE    ((ld_addr)0x1000)
#define LD_PAGE_MASK    (LD_PAGE_SIZE - 1)

/* e_ident[] indices and the values an object must carry to be loaded. */
#define EI_CLASS        4
#define ELFCLASS32      1
#define ELFCLASS64      2
#define ET_DYN          3
#define EM_386          3
#define EM_X86_64       62

#ifdef LD_ARCH_AMD64

#define LD_ELFCLASS     ELFCLASS64
#define LD_EM           EM_X86_64
#define LD_SELF_NAME    "ld64.so"       /* how the linker lists itself */
#define LD_DEFAULT_LIBDIR "/lib64"

/* ELF64 file header. */
typedef struct {
    unsigned char e_ident[16];
    ld_u16 e_type;
    ld_u16 e_machine;
    ld_u32 e_version;
    ld_u64 e_entry;
    ld_u64 e_phoff;
    ld_u64 e_shoff;
    ld_u32 e_flags;
    ld_u16 e_ehsize;
    ld_u16 e_phentsize;
    ld_u16 e_phnum;
    ld_u16 e_shentsize;
    ld_u16 e_shnum;
    ld_u16 e_shstrndx;
} Elf_Ehdr;

/* ELF64 program header - note p_flags moves up next to p_type. */
typedef struct {
    ld_u32 p_type;
    ld_u32 p_flags;
    ld_u64 p_offset;
    ld_u64 p_vaddr;
    ld_u64 p_paddr;
    ld_u64 p_filesz;
    ld_u64 p_memsz;
    ld_u64 p_align;
} Elf_Phdr;

/* ELF64 dynamic entry (16 bytes). */
typedef struct {
    ld_i64 d_tag;
    union { ld_u64 d_val; ld_u64 d_ptr; } d_un;
} Elf_Dyn;

/* ELF64 symbol (24 bytes). */
typedef struct {
    ld_u32 st_name;
    unsigned char st_info;
    unsigned char st_other;
    ld_u16 st_shndx;
    ld_u64 st_value;
    ld_u64 st_size;
} Elf_Sym;

/* ELF64 RELA relocation: the addend is explicit, not stored in place. */
typedef struct {
    ld_u64 r_offset;
    ld_u64 r_info;
    ld_i64 r_addend;
} Elf_Rela;

#define ELF_R_TYPE(i)   ((ld_u32)(i))
#define ELF_R_SYM(i)    ((ld_u32)((i) >> 32))

/* amd64 objects carry DT_RELA tables (and DT_PLTREL = DT_RELA). */
typedef Elf_Rela Elf_Reloc;
#define LD_DT_REL       DT_RELA
#define LD_DT_RELSZ     DT_RELASZ

/* The 64-bit getdents record (struct amd64_dirent in the kernel's
 * <sys/amd64_abi.h>): d_fileno(8) d_off(8) d_reclen(2) d_type(1) pad(1)
 * d_namlen(2) pad(2) d_name[]. */
#define LD_DIRENT_RECLEN_OFF 16
#define LD_DIRENT_NAME_OFF   24

#else /* LD_ARCH_I386 */

#define LD_ELFCLASS     ELFCLASS32
#define LD_EM           EM_386
#define LD_SELF_NAME    "ld.so"
#define LD_DEFAULT_LIBDIR "/lib"

/* ELF32 file header - only the fields ld.so reads at load time. */
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
} Elf_Ehdr;

/* ELF32 program header - System V ELF spec Fig. 2-1 */
typedef struct {
    ld_u32 p_type;
    ld_u32 p_offset;
    ld_u32 p_vaddr;
    ld_u32 p_paddr;
    ld_u32 p_filesz;
    ld_u32 p_memsz;
    ld_u32 p_flags;
    ld_u32 p_align;
} Elf_Phdr;

/* ELF32 dynamic entry */
typedef struct {
    ld_i32 d_tag;
    union { ld_u32 d_val; ld_u32 d_ptr; } d_un;
} Elf_Dyn;

/* ELF32 symbol */
typedef struct {
    ld_u32 st_name;
    ld_u32 st_value;
    ld_u32 st_size;
    unsigned char st_info;
    unsigned char st_other;
    ld_u16 st_shndx;
} Elf_Sym;

/* ELF32 REL relocation: the addend is the word being relocated. */
typedef struct {
    ld_u32 r_offset;
    ld_u32 r_info;
} Elf_Rel;

#define ELF_R_TYPE(i)   ((i) & 0xff)
#define ELF_R_SYM(i)    ((i) >> 8)

/* i386 objects carry DT_REL tables; there is no DT_RELA by spec. */
typedef Elf_Rel Elf_Reloc;
#define LD_DT_REL       DT_REL
#define LD_DT_RELSZ     DT_RELSZ

/* The 32-bit getdents record: d_ino(4) d_off(4) d_reclen(2) d_name[]. */
#define LD_DIRENT_RECLEN_OFF 8
#define LD_DIRENT_NAME_OFF   10

#endif /* LD_ARCH_* */

/* Dynamic tags. */
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
/* GNU symbol versioning - required to load libstdc++.so.6 and any
 * other DSO that uses versioned symbols (GLIBCXX_3.4 etc.). */
#define DT_VERSYM      0x6ffffff0   /* Per-symbol version index table */
#define DT_VERDEF      0x6ffffffc   /* Verdef array (what we provide) */
#define DT_VERDEFNUM   0x6ffffffd
#define DT_VERNEED     0x6ffffffe   /* Verneed array (what we require) */
#define DT_VERNEEDNUM  0x6fffffff
#define DT_FINI_ARRAY    26
#define DT_INIT_ARRAYSZ  27
#define DT_FINI_ARRAYSZ  28
/* Binding-time requests.  An object carrying any of them wants every PLT
 * slot bound before it runs (ld -z now). */
#define DT_BIND_NOW      24
#define DT_FLAGS         30
#define DT_FLAGS_1       0x6ffffffb
#define DF_BIND_NOW      0x8        /* in DT_FLAGS */
#define DF_1_NOW         0x1        /* in DT_FLAGS_1 */

/* Program-header types */
#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3
#define PT_PHDR     6
#define PT_TLS      7
#define PT_GNU_RELRO 0x6474e552   /* segment to make read-only after reloc */

/* i386 relocation types */
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
#define R_386_IRELATIVE    42  /* indirect function: resolver at B + A */

/* amd64 relocation types (psABI Table 4.10). */
#define R_X86_64_NONE      0
#define R_X86_64_64        1   /* S + A */
#define R_X86_64_PC32      2   /* S + A - P, 32-bit */
#define R_X86_64_COPY      5
#define R_X86_64_GLOB_DAT  6   /* S */
#define R_X86_64_JUMP_SLOT 7   /* S */
#define R_X86_64_RELATIVE  8   /* B + A */
#define R_X86_64_DTPMOD64  16  /* module id of a tls_index */
#define R_X86_64_DTPOFF64  17  /* offset within module of a tls_index */
#define R_X86_64_TPOFF64   18  /* offset from the thread pointer */
#define R_X86_64_IRELATIVE 37  /* indirect function: resolver at B + A */

/* The relocation types of the architecture being built that the common
 * code in ld_reloc.c has to recognise: the copy relocation, which it
 * defers to a final pass, and the two that may sit in DT_JMPREL, which
 * it can leave to be bound on first call. */
#ifdef LD_ARCH_AMD64
#define LD_R_COPY       R_X86_64_COPY
#define LD_R_COPY_NAME  "R_X86_64_COPY"
#define LD_R_JMP_SLOT   R_X86_64_JUMP_SLOT
#define LD_R_IRELATIVE  R_X86_64_IRELATIVE
#else
#define LD_R_COPY       R_386_COPY
#define LD_R_COPY_NAME  "R_386_COPY"
#define LD_R_JMP_SLOT   R_386_JMP_SLOT
#define LD_R_IRELATIVE  R_386_IRELATIVE
#endif

/* Auxv entries */
#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_FLAGS   8
#define AT_ENTRY   9
#define AT_PLATFORM 15
#define AT_SECURE  23   /* nonzero => secure exec (setuid/setgid): ignore LD_* */
#define AT_EXECFN  31

/* Native syscall numbers we actually issue from the linker.  The 64-bit
 * native personality uses the same table as i386. */
#define SYS_exit   1
#define SYS_read   3
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_lseek 19
#define SYS_mmap        90
#define SYS_munmap      91
#define SYS_fstat      108
#define SYS_getdents   141
#define SYS_mprotect   125
#define SYS_futex      240
#define SYS_set_gsbase 274
#define SYS_thr_self   432

/* mmap flags / prot bits we use. */
#define LD_PROT_READ   1
#define LD_PROT_WRITE  2
#define LD_PROT_EXEC   4
#define LD_MAP_PRIVATE 0x002
#define LD_MAP_FIXED   0x010
#define LD_MAP_ANON    0x020

/* open() flags */
#define LD_O_RDONLY 0

/* st_info is laid out the same way in both ELF classes. */
#define ELF_ST_BIND(i) ((i) >> 4)
#define ELF_ST_TYPE(i) ((i) & 0xf)
#define STB_LOCAL  0
#define STB_GLOBAL 1
#define STB_WEAK   2
#define STT_FUNC   2   /* ELF_ST_TYPE: symbol names a function */
#define STT_GNU_IFUNC 10 /* st_value is a resolver returning the function */
#define STN_UNDEF  0
#define SHN_UNDEF  0

/* GNU symbol versioning - DT_VERDEF / DT_VERNEED / DT_VERSYM blocks.
 * Layout per glibc / binutils elf/external.h; it is built from 16- and
 * 32-bit fields only, so it is identical in ELF32 and ELF64.  Strings
 * live in the dynamic strtab; the version-name HASH is computed via the
 * standard ELF hash function (NOT the GNU hash). */
typedef ld_u16 Elf_Half;

typedef struct {
    Elf_Half   vd_version;  /* always 1 */
    Elf_Half   vd_flags;    /* VER_FLG_BASE (1) for the base def slot */
    Elf_Half   vd_ndx;      /* version index (1..N), matches VERSYM */
    Elf_Half   vd_cnt;      /* number of vd_aux entries (>=1) */
    ld_u32     vd_hash;     /* ELF hash of the version name */
    ld_u32     vd_aux;      /* byte offset to first Elf_Verdaux */
    ld_u32     vd_next;     /* byte offset to next Elf_Verdef (0=end) */
} Elf_Verdef;

typedef struct {
    ld_u32     vda_name;    /* offset into strtab - version name */
    ld_u32     vda_next;    /* byte offset to next aux (0=end) */
} Elf_Verdaux;

typedef struct {
    Elf_Half   vn_version;  /* always 1 */
    Elf_Half   vn_cnt;      /* number of vn_aux entries */
    ld_u32     vn_file;     /* offset into strtab - providing soname */
    ld_u32     vn_aux;      /* byte offset to first Elf_Vernaux */
    ld_u32     vn_next;     /* byte offset to next Elf_Verneed (0=end) */
} Elf_Verneed;

typedef struct {
    ld_u32     vna_hash;    /* ELF hash of the required version name */
    Elf_Half   vna_flags;
    Elf_Half   vna_other;   /* version index - matches VERSYM in this DSO */
    ld_u32     vna_name;    /* offset into strtab - version name */
    ld_u32     vna_next;    /* byte offset to next aux (0=end) */
} Elf_Vernaux;

/* VERSYM is a Half[] parallel to .dynsym.  Index meanings:
 *   0 = VER_NDX_LOCAL    (symbol not exported)
 *   1 = VER_NDX_GLOBAL   (no version assigned - base def)
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

/* DT_GNU_HASH: the header and the bucket/chain arrays are 32-bit words in
 * both classes, but the bloom filter is made of native words. */
#define LD_BLOOM_BITS    ((ld_u32)(sizeof(ld_addr) * 8))

/* TLS module descriptor passed to __tls_get_addr() for GD/LD models: two
 * native words (8 bytes on i386, 16 on amd64).  On i386 the GD sequence
 * calls ___tls_get_addr with the pointer in %eax (regparm(1)); the
 * two-underscore symbol takes it as an ordinary argument on both. */
typedef struct {
    ld_addr ti_module;
    ld_addr ti_offset;
} tls_index;

/*
 * Thread control block, at the thread pointer (%gs base on i386, %fs base
 * on amd64).  Word 0 is the thread pointer itself and word 1 the DTV
 * pointer -- where libc's __tls_get_addr reads it (lib/c/src/tls.c).  The
 * DTV is laid out immediately after the TCB.  On amd64 the TCB is 64
 * bytes, the size libc's static-TLS setup also uses, which keeps the
 * psABI's stack-protector slot (%fs:0x28) clear of the DTV.
 */
#ifdef LD_ARCH_AMD64
#define LD_TLS_TCB_SIZE 64
#else
#define LD_TLS_TCB_SIZE 8
#endif

/* Tiny IO helpers - implemented in ld_io.c. */
void  ld_write(int fd, const char *buf, ld_size len);
void  ld_puts(const char *s);
void  ld_putx(ld_addr v);
void  ld_putd(ld_u32 v);
void  ld_die(const char *msg) __attribute__((noreturn));
void  ld_exit(int status) __attribute__((noreturn));

/* When non-zero, emit the verbose loading / relocating / TLS / etc.
 * trace.  Set from LD_DEBUG=<anything> in envp.  Errors and the
 * LD_TRACE_LOADED_OBJECTS dump are NOT gated by this - they go to
 * stderr / stdout regardless. */
extern int ld_debug;
#define LD_DBG(stmt) do { if (ld_debug) { stmt; } } while (0)

/* Filesystem + mapping syscalls.  Return -errno on failure, on both
 * architectures: the amd64 system-call path reports an error in the carry
 * flag with a positive errno, which the raw wrappers in ld_io.c negate. */
int   ld_open(const char *path, int flags);
int   ld_close(int fd);
long  ld_read(int fd, void *buf, ld_size n);
long  ld_lseek(int fd, long off, int whence);
long  ld_getdents(int fd, void *buf, ld_size n);
void *ld_mmap(void *addr, ld_size len, int prot, int flags,
              int fd, ld_addr page_off);
long  ld_munmap(void *addr, ld_size len);
long  ld_mprotect(void *addr, ld_size len, int prot);

/* True iff an mmap/syscall pointer return is an error.  The kernel returns
 * a -errno in [-4095, -1] on failure; everything else is a valid pointer.
 * A plain `(long)p < 0` test is WRONG on 32-bit: a successful mmap can land
 * at a high user address (>= 0x80000000) whose sign bit is set, and would
 * be mis-rejected as an error.  This surfaced once a large dependency graph
 * (GTK+ 2.x: ~40 DSOs) pushed later libraries past the 2 GiB line. */
static inline int ld_mmap_failed(void *p) {
    return (ld_addr)(unsigned long)p >= (ld_addr)-4095;
}

/* Phase-3 surface: per-loaded-object descriptor.  Both the program
 * itself and every loaded .so get one of these.  The list is kept
 * in load order for deterministic symbol resolution. */
typedef struct ld_obj {
    char            name[64];   /* SONAME or basename, for diagnostics */
    char            path[256];  /* full filesystem path it resolved from (ldd) */
    ld_addr         base;       /* load bias */
    ld_addr         load_start; /* low end of PT_LOAD span (absolute) */
    ld_addr         load_end;   /* high end of PT_LOAD span (absolute) */
    Elf_Dyn        *dynamic;    /* PT_DYNAMIC pointer (already biased) */

    /* In-memory program-header table, for dl_iterate_phdr(3) - which
     * libgcc's DWARF unwinder uses (USE_PT_GNU_EH_FRAME) to locate each
     * loaded object's PT_GNU_EH_FRAME / .eh_frame_hdr.  Without this,
     * C++ exceptions can't unwind across DSO boundaries. */
    const void     *phdr;       /* runtime address of the Elf_Phdr[] */
    ld_u32          phnum;       /* number of program headers */

    /* Cached dynamic-table pointers (all already biased). */
    const char     *strtab;
    Elf_Sym        *symtab;
    ld_addr         strsz;

    ld_u32         *gnu_hash;   /* DT_GNU_HASH (preferred) */
    ld_u32         *hash;       /* DT_HASH (fallback) */

    Elf_Reloc      *rel;        /* DT_REL (i386) / DT_RELA (amd64) */
    ld_addr         relsz;      /* bytes */
    Elf_Reloc      *jmprel;     /* DT_JMPREL */
    ld_addr         pltrelsz;   /* bytes */

    /* Initializers / finalizers (DT_INIT, DT_INIT_ARRAY, ...). */
    void          (*init)(void);
    void          (**init_array)(void);
    ld_addr         init_arraysz;   /* bytes - count = sz / sizeof(fn ptr) */
    void          (*fini)(void);
    void          (**fini_array)(void);
    ld_addr         fini_arraysz;

    /* PT_TLS metadata, populated when an object carries a thread-
     * local segment.  `tls_offset` is the negative offset from the
     * thread pointer at which this module's TLS image lives in the
     * combined per-thread block - assigned by ld_setup_tls(). */
    const void     *tls_image;      /* file-image (PT_TLS at p_offset+base) */
    ld_addr         tls_filesz;
    ld_addr         tls_memsz;
    ld_addr         tls_align;
    ld_addr         tls_offset;     /* abs(offset) below thread pointer */

    /* Per-object guards.  A RELATIVE relocation on i386 is `*p += base` -
     * non-idempotent - so re-running ld_relocate on an already-
     * relocated object would double the bias and silently corrupt
     * every relative pointer (notably DT_FINI_ARRAY entries).
     * Same concern for init/fini arrays which must fire exactly
     * once. */
    int             relocated;
    int             copy_relocated; /* COPY final pass done */
    int             initialized;
    int             finalized;
    int             refcount;       /* dlopen refs; fini at last close */
    int             protected;      /* W^X + RELRO applied */

    /* Lazy binding and indirect functions (see ld_reloc.c).
     * `pltgot` is DT_PLTGOT: words 1 and 2 are the linker's, and the PLT
     * slots follow.  `bind_now` records DT_BIND_NOW / DF_BIND_NOW /
     * DF_1_NOW.  `lazy` is set once the PLT slots have been left for
     * ld_plt_fixup to bind on first call.  `has_ifunc` is set by the
     * main relocation pass when it defers an entry to ld_relocate_ifunc,
     * and `ifunc_pass` is non-zero while that pass is running. */
    ld_addr        *pltgot;
    int             bind_now;
    int             lazy;
    int             has_ifunc;
    int             ifunc_pass;
    int             ifunc_relocated;

    /* Phase 5 (C++ linkage): GNU symbol-versioning sections.  All
     * three are biased pointers into the loaded image.  NULL when
     * the DSO doesn't carry versioning (substrate libc, libm, etc.
     * currently don't - libstdc++.so.6 does). */
    Elf_Half       *versym;     /* DT_VERSYM - parallel to symtab */
    Elf_Verdef     *verdef;     /* DT_VERDEF - what we EXPORT */
    Elf_Verneed    *verneed;    /* DT_VERNEED - what we IMPORT */
    ld_u32          verdefnum;  /* count of verdef entries */
    ld_u32          verneednum; /* count of verneed entries */

    /* Phase 5 (TLS GD/LD): per-DSO module index.  Assigned when the
     * object loads, used as the ti_module field passed to
     * __tls_get_addr() by GD/LD-model relocations.  0 = "no TLS in
     * this object". */
    ld_u32          tls_modid;

    struct ld_obj  *next;
} ld_obj_t;

/* The linker's own dynamic section (defined by the link editor). */
extern Elf_Dyn _DYNAMIC[];

/* Load a shared object by absolute path.  Returns NULL on failure
 * with a diagnostic via ld_die.  Already-loaded SONAMEs are
 * deduplicated; the cached descriptor is returned. */
ld_obj_t *ld_load_object(const char *path);

/* Walk the loaded-object list looking for `name`.  Returns the
 * symbol's runtime address, or 0 if undefined / weak-undef. */
ld_addr ld_resolve(const char *name);

/* Requester-aware resolve for the relocation processor: `requester` is
 * the object being relocated, so a program's own PLT slot isn't bound to
 * its own canonical-PLT entry (function-address equality - see
 * ld_resolve.c resolve_pred). */
ld_addr ld_resolve_req(const char *name, ld_u32 vh_hash,
                       const ld_obj_t *requester);

/* ld_resolve_req for a caller that handles indirect functions itself:
 * see ld_reloc_resolve_ifunc.  Every other resolve function calls the
 * resolver of an indirect function and returns the implementation. */
ld_addr ld_resolve_req_ifunc(const char *name, ld_u32 vh_hash,
                             const ld_obj_t *requester, int *ifunc_out);

/* Same, but skip `skip` while searching.  Used by the COPY relocation,
 * which must find the source-of-truth in a SHARED library, not in the
 * executable that's about to receive the copy. */
ld_addr ld_resolve_skip(const char *name, const ld_obj_t *skip);

/* Version-aware resolution.  When the IMPORTER's VERSYM marks a
 * reference with a non-default version index, the resolver only
 * matches symbols whose VERSYM in the candidate object carries the
 * matching vd_hash (or whose vd_flags has VER_FLG_BASE set,
 * indicating a default version).  vh_hash is the ELF hash of the
 * importer's required-version-name; pass 0 for unversioned
 * lookups (caller wants the default version). */
ld_addr ld_resolve_versioned(const char *name, ld_u32 vh_hash,
                             const ld_obj_t *skip, ld_addr *size_out);

/* Standard ELF hash function - re-used for version-name hashing
 * (the SAME function ELF uses for the SysV symbol-name hash table). */
ld_u32 ld_elf_hash(const char *s);

/* Same, but also returns the symbol size (for the COPY relocation).
 * Returns 0 (and *size_out=0) if not found. */
ld_addr ld_resolve_with_size(const char *name, const ld_obj_t *skip,
                             ld_addr *size_out);

/* Resolve an imported TLS symbol (initial-exec): returns the symbol's raw
 * st_value (offset within its defining module's PT_TLS image) and the
 * defining module via *def_out (NULL if unresolved). */
ld_addr ld_resolve_tls(const char *name, const ld_obj_t *requester,
                       const ld_obj_t **def_out);

/* Look `name` up in one object's own symbol table only (dlsym with an
 * object handle, RTLD_NEXT).  Returns its runtime address or 0. */
ld_addr ld_lookup_in_obj(const ld_obj_t *o, const char *name);

/* Apply the object's relocation table and DT_JMPREL on `obj`, EXCEPT
 * the COPY relocations.  Returns 0 on success. */
int ld_relocate(ld_obj_t *obj);

/* Apply the COPY relocations of `obj` only.  Must run as a
 * final pass after every object has been through ld_relocate(), so
 * the copy source already holds its relocated value. */
int ld_relocate_copy(ld_obj_t *obj);

/* Final pass for indirect functions: apply the entries of `obj` that
 * ld_relocate() deferred because they need a resolver function called -
 * IRELATIVE, and references to STT_GNU_IFUNC symbols.  Must run after
 * every object has been through ld_relocate() and ld_relocate_copy(), so
 * the resolver runs in fully relocated code.  A no-op for an object with
 * no such entries. */
int ld_relocate_ifunc(ld_obj_t *obj);

/* Non-zero when every PLT slot must be bound at load time: LD_BIND_NOW in
 * the environment, or while a dlopen(RTLD_NOW) relocates what it loaded. */
extern int ld_bind_now;

/* Arrange for the DT_JMPREL slots of `obj` to be bound on first call
 * instead of now.  Returns 1 if it did (the caller then skips DT_JMPREL),
 * 0 if the object must be bound eagerly. */
int ld_reloc_lazy_setup(ld_obj_t *obj);

/* Call an indirect function's resolver; it returns the implementation. */
static inline ld_addr ld_ifunc_call(ld_addr resolver) {
    return ((ld_addr (*)(void))(unsigned long)resolver)();
}

/* Lazy binding.  ld_plt_trampoline (ld_plt_i386.S, ld_plt_amd64.S) is
 * what PLT0 jumps to; it preserves the argument registers around
 * ld_plt_fixup(), which binds one entry of obj's DT_JMPREL and returns
 * the function.  `arg` is what the PLT stub pushed to name the entry:
 * its index on amd64, its byte offset into the table on i386. */
void    ld_plt_trampoline(void);
ld_addr ld_plt_fixup(ld_obj_t *obj, ld_addr arg);

/* The architecture's relocation processor (ld_reloc_i386.c,
 * ld_reloc_amd64.c): apply one entry of `obj`.  Returns 0 on success,
 * -1 after printing a diagnostic. */
int ld_reloc_apply(ld_obj_t *obj, Elf_Reloc *r);

/* Version-aware symbol lookup on behalf of a relocation of `obj` against
 * its symbol `sym_idx` (named `name`).  Shared by the per-architecture
 * relocation processors. */
ld_addr ld_reloc_resolve(const ld_obj_t *obj, ld_u32 sym_idx,
                         const char *name);

/* The same, reporting an indirect function: when the definition found is
 * STT_GNU_IFUNC, *ifunc_out is set to 1 and the value returned is the
 * resolver's address, which the caller calls when it is safe to. */
ld_addr ld_reloc_resolve_ifunc(const ld_obj_t *obj, ld_u32 sym_idx,
                               const char *name, int *ifunc_out);

/* Public head of the loaded-object list. */
ld_obj_t *ld_obj_list(void);

/* Put an externally allocated descriptor at the head (the program) or the
 * tail (the linker itself) of the loaded-object list. */
void ld_obj_prepend(ld_obj_t *o);
void ld_obj_append(ld_obj_t *o);

/* Look up an already-loaded object by SONAME/basename without loading
 * anything.  Returns NULL if it is not present.  Backs dlopen(RTLD_NOLOAD). */
ld_obj_t *ld_obj_find_loaded(const char *name);

/*
 * dlopen() flag bits ld.so acts on.  ld.so is freestanding and does not
 * include libc's <dlfcn.h>, so this must be kept in step with it.
 */
#define RTLD_NOW    0x0002
#define RTLD_NOLOAD 0x0004

/* Re-protect an ld.so-mapped object after relocation: each PT_LOAD to
 * its real ELF flags (dropping the write bit ld.so mapped it with) and
 * PT_GNU_RELRO to read-only.  No-op if already applied or if the object
 * wasn't mapped by ld.so (the kernel-mapped exe / ld.so itself). */
void ld_protect_object(ld_obj_t *o);

/* Save / restore the end of the loaded-object list so a failed dlopen
 * can roll back every object it appended (see ld_load.c). */
void ld_obj_savepoint(ld_obj_t **tail_out, ld_size *count_out);
void ld_obj_restore(ld_obj_t *tail, ld_size count);

/* Run DT_INIT and DT_INIT_ARRAY for every loaded object in
 * dependency order (deepest deps first, program last).  Idempotent
 * - each object is initialized exactly once via a per-object guard
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
 * PT_TLS image into it, install the thread pointer via the native
 * sys_set_gsbase syscall (which sets the %gs base of a 32-bit process
 * and the %fs base of a 64-bit one).  Returns 0 on success or a negative
 * errno.  Called once after relocations and before init arrays. */
int ld_setup_tls(void);
/* Lay out a PT_TLS module that arrived after ld_setup_tls() (i.e. via dlopen)
 * in the surplus static TLS reserved by that pass.  Must run before the
 * object is relocated.  Returns 0 on success, -1 if the surplus is used up. */
int ld_tls_add_module(ld_obj_t *o);

/*
 * The dlopen/dlsym/dlclose lock.  Recursive, so a constructor running under
 * dlopen may take it again.  Exposed because the TLS code mutates the
 * per-thread block registry, which dlopen also walks.
 */
void ld_dl_lock(void);
void ld_dl_unlock(void);

/* Native syscall: install a TLS base for the current thread.
 * Returns 0 on success or -errno. */
int ld_sys_set_gsbase(ld_addr base);

/* futex(2) - op is FUTEX_WAIT (0) or FUTEX_WAKE (1); timeout/uaddr2/
 * val3 are always 0 for ld.so's mutex.  Returns the raw kernel result.
 * Used by the dlopen/dlsym/dlclose serialization lock (ld_dl.c). */
#define LD_FUTEX_WAIT 0
#define LD_FUTEX_WAKE 1
long ld_futex(int *uaddr, int op, int val);

/* thr_self(2) - current kernel thread id, used as the recursive-lock
 * owner token in ld_dl.c (always valid, unlike the thread pointer for a
 * no-TLS program). */
int ld_thr_self(void);

/* Per-process upper bound on objects we'll iterate during init.
 * Sized to match LD_MAX_OBJS in ld_load.c so it's effectively the
 * same limit, kept here so ld_main can size a stack array without
 * pulling in ld_load.c's private constants. */
#define LD_MAX_OBJS_INIT_LIMIT 192

/* Entry point from the start code (ld_start.S, ld_start_amd64.S): takes
 * the kernel-built initial stack pointer (where argc lives, in native
 * words) and returns the program's entry address. */
ld_addr ld_main(ld_addr *initial_stack);

#endif /* _LD_SO_LD_H */
