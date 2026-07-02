#include <exec/formats/elf.h>
#include <vfs/vfs.h>
#include <kern/console.h>
#include <sys/sysinfo.h> // For BITNESS_*
#include <sys/proc.h>
#include <arch/i386/idt.h>   /* registers_t for the ptrace exec-stop frame */
#include <kern/panic.h>
#include <string.h>
#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_kmem.h>
#include <vm/vm_page.h>
#include <vm/vm_commit.h>
#include <vm/phys_mem.h>
#include <exec/perso/personality.h>
#include <exec/perso/linux/linux_exec.h>
#include <sys/exec.h>
#include <sys/random.h>
#include <sys/signal.h> // For copyin/copyout
#include <kern/time.h>  // proc_ptimers_clear (timers deleted across exec)
#include <sys/kern_syscalls.h>
#include <sys/file.h>
#include <sys/errno.h>
#include <sys/stat.h>
#include <arch/i386/pmm.h>
#if defined(__i386__) || defined(HOST_TEST)
#include <arch/i386/pmap.h>
#include <arch/i386/gdt.h>
#elif defined(__x86_64__)
#include <arch/x86_64/pmap.h>
#endif
#include <sys/ldt.h>
#include <pm/pm.h>
#include <kern/cmdline.h>
#include <stdio.h>

typedef struct elf_image_info {
    Elf32_Ehdr ehdr;
    Elf32_Phdr phdrs[256];
    uint16_t phnum;
    int detected_os;
    uint32_t at_phdr;
    char interp_path[256];
    uint32_t interp_len;
} elf_image_info_t;

typedef struct {
    uint32_t va;
    void *pa;
} elf_page_map_t;

#define ELF_ET_DYN_LOAD_BASE_I386 0x08048000u

typedef struct elf_image_cache_entry {
    uint8_t valid;
    uintptr_t fsid;
    uint64_t ino;
    off_t length;
    int64_t mtime;
    int64_t ctime;
    elf_image_info_t image;
} elf_image_cache_entry_t;

#define ELF_IMAGE_CACHE_SIZE 16
static elf_image_cache_entry_t elf_image_cache[ELF_IMAGE_CACHE_SIZE];
static uint32_t elf_image_cache_hand;

static int elf_debug_enabled(void) {
    return cmdline_debug_enabled("elf");
}

static uint32_t elf_note_align4(uint32_t value) {
    return (value + 3U) & ~3U;
}

static int elf_note_detect_os(const uint8_t *buf, uint32_t len) {
    uint32_t off = 0;

    while (off + sizeof(Elf32_Nhdr) <= len) {
        const Elf32_Nhdr *nhdr = (const Elf32_Nhdr *)(const void *)(buf + off);
        uint32_t namesz = nhdr->n_namesz;
        uint32_t descsz = nhdr->n_descsz;
        uint32_t name_off = off + sizeof(*nhdr);
        uint32_t desc_off = name_off + elf_note_align4(namesz);
        uint32_t next_off = desc_off + elf_note_align4(descsz);

        if (next_off > len) {
            break;
        }

        if (namesz >= 4 && memcmp(buf + name_off, "GNU", 4) == 0 &&
            nhdr->n_type == NT_GNU_ABI_TAG && descsz >= sizeof(uint32_t)) {
            const uint32_t *abi = (const uint32_t *)(const void *)(buf + desc_off);
            if (*abi == 0) {
                return ELFOSABI_LINUX;
            }
        }

        if (namesz >= 8 && memcmp(buf + name_off, "FreeBSD", 8) == 0) {
            return ELFOSABI_FREEBSD;
        }

        /* NetBSD/OpenBSD ship with EI_OSABI = SYSV (0) and announce
         * the OS via PT_NOTE with owner "NetBSD\0" / "OpenBSD\0".
         * NetBSD uses .note.netbsd.ident (n_type = NT_NETBSD_IDENT,
         * n_namesz = 7); OpenBSD uses .note.openbsd.ident similarly. */
        if (namesz >= 7 && memcmp(buf + name_off, "NetBSD", 7) == 0) {
            return ELFOSABI_NETBSD;
        }
        if (namesz >= 8 && memcmp(buf + name_off, "OpenBSD", 8) == 0) {
            return ELFOSABI_OPENBSD;
        }

        off = next_off;
    }

    return ELFOSABI_SUBSTRATE;
}

static int elf_personality_debug_enabled(int detected_os) {
    if (detected_os == ELFOSABI_LINUX) {
        return cmdline_debug_enabled("perso:linux");
    }
    if (detected_os == ELFOSABI_FREEBSD) {
        return cmdline_debug_enabled("perso:freebsd");
    }
    return cmdline_debug_enabled("perso:native");
}

static void elf_cache_identity(fs_node_t *file, uintptr_t *fsid, uint64_t *ino) {
    if (file->mp) {
        *fsid = (uintptr_t)file->mp;
    } else {
        *fsid = (uintptr_t)file;
    }
    *ino = file->inode ? file->inode : (uint64_t)(uintptr_t)file;
}

static int elf_detect_osabi(const Elf32_Ehdr *ehdr) {
    if (!ehdr) {
        return ELFOSABI_SUBSTRATE;
    }

    /* ELFOSABI_LINUX (3) or ELFOSABI_GNU (3) */
    if (ehdr->e_ident[EI_OSABI] == 3) {
        return ELFOSABI_LINUX;
    }
    /* ELFOSABI_FREEBSD (9) */
    if (ehdr->e_ident[EI_OSABI] == 9) {
        return ELFOSABI_FREEBSD;
    }
    /* ELFOSABI_NETBSD (2) */
    if (ehdr->e_ident[EI_OSABI] == 2) {
        return ELFOSABI_NETBSD;
    }
    /* ELFOSABI_OPENBSD (12) */
    if (ehdr->e_ident[EI_OSABI] == 12) {
        return ELFOSABI_OPENBSD;
    }

    return ELFOSABI_SUBSTRATE;
}

static int elf_cache_matches(const elf_image_cache_entry_t *entry, fs_node_t *file,
                             uintptr_t fsid, uint64_t ino) {
    return entry &&
           entry->valid &&
           entry->fsid == fsid &&
           entry->ino == ino &&
           entry->length == file->length &&
           entry->mtime == file->mtime &&
           entry->ctime == file->ctime;
}

static int elf_read_image_info(fs_node_t *file, elf_image_info_t *image) {
    if (!file || !file->read || !image) {
        return -ENOEXEC;
    }

    memset(image, 0, sizeof(*image));

    if (file->read(file, 0, sizeof(Elf32_Ehdr), (uint8_t *)&image->ehdr) != sizeof(Elf32_Ehdr)) {
        return -ENOEXEC;
    }

    if (!elf_check_file(&image->ehdr)) {
        return -ENOEXEC;
    }

    if (image->ehdr.e_phnum > (sizeof(image->phdrs) / sizeof(image->phdrs[0]))) {
        return -ENOEXEC;
    }

    image->phnum = image->ehdr.e_phnum;
    image->detected_os = elf_detect_osabi(&image->ehdr);

    /* e_phentsize must at least span Elf32_Phdr; reject pathological
     * values up front so the multiply below stays bounded. */
    if (image->ehdr.e_phentsize < sizeof(Elf32_Phdr) ||
        image->ehdr.e_phentsize > 0x1000) {
        return -ENOEXEC;
    }

    for (uint16_t i = 0; i < image->phnum; i++) {
        /* 64-bit math + explicit overflow rejection — a hostile ELF can
         * set e_phoff close to UINT32_MAX and pick e_phentsize so that
         * uint32_t addition wraps and we end up reading the ELF header
         * itself as a phdr. */
        uint64_t ph_offset64 = (uint64_t)image->ehdr.e_phoff +
                               (uint64_t)i * (uint64_t)image->ehdr.e_phentsize;
        if (ph_offset64 + sizeof(Elf32_Phdr) > 0xFFFFFFFFULL) {
            return -ENOEXEC;
        }
        uint32_t ph_offset = (uint32_t)ph_offset64;
        Elf32_Phdr *phdr = &image->phdrs[i];

        if (file->read(file, ph_offset, sizeof(Elf32_Phdr), (uint8_t *)phdr) != sizeof(Elf32_Phdr)) {
            return -ENOEXEC;
        }

        /* Reject overflow of p_offset + p_filesz (used in arithmetic
         * below and in the segment loader) before any caller observes
         * a wrapped value. */
        if (phdr->p_filesz > 0xFFFFFFFFU - phdr->p_offset) {
            return -ENOEXEC;
        }

        if (phdr->p_type == PT_INTERP && phdr->p_filesz > 0) {
            uint32_t max_read = sizeof(image->interp_path) - 1;
            uint32_t to_read = (phdr->p_filesz < max_read) ? phdr->p_filesz : max_read;
            if (file->read(file, phdr->p_offset, to_read, (uint8_t *)image->interp_path) != to_read) {
                return -ENOEXEC;
            }
            image->interp_path[to_read] = '\0';
            image->interp_len = to_read;
        }

        if (phdr->p_type == PT_PHDR) {
            image->at_phdr = phdr->p_vaddr;
        } else if (image->at_phdr == 0 &&
                   phdr->p_type == PT_LOAD &&
                   image->ehdr.e_phoff >= phdr->p_offset &&
                   image->ehdr.e_phoff < (phdr->p_offset + phdr->p_filesz)) {
            image->at_phdr = phdr->p_vaddr + (image->ehdr.e_phoff - phdr->p_offset);
        }

        /* Check for personality notes if OSABI is NONE/SUBSTRATE */
        if (phdr->p_type == PT_NOTE && image->detected_os == ELFOSABI_SUBSTRATE) {
            char note_buf[256];
            uint32_t to_read = (phdr->p_filesz < sizeof(note_buf)) ? phdr->p_filesz : sizeof(note_buf);
            if (file->read(file, phdr->p_offset, to_read, (uint8_t *)note_buf) == to_read) {
                image->detected_os = elf_note_detect_os((const uint8_t *)note_buf, to_read);
            }
        }
    }

    if (image->at_phdr == 0) {
        image->at_phdr = image->ehdr.e_phoff;
    }

    return 0;
}

static int elf_get_image_info(fs_node_t *file, elf_image_info_t *image) {
    uintptr_t fsid;
    uint64_t ino;

    elf_cache_identity(file, &fsid, &ino);

    for (int i = 0; i < ELF_IMAGE_CACHE_SIZE; i++) {
        if (elf_cache_matches(&elf_image_cache[i], file, fsid, ino)) {
            *image = elf_image_cache[i].image;
            return 0;
        }
    }

    if (elf_read_image_info(file, image) != 0) {
        return -ENOEXEC;
    }

    {
        elf_image_cache_entry_t *entry = &elf_image_cache[elf_image_cache_hand++ % ELF_IMAGE_CACHE_SIZE];
        entry->valid = 1;
        entry->fsid = fsid;
        entry->ino = ino;
        entry->length = file->length;
        entry->mtime = file->mtime;
        entry->ctime = file->ctime;
        entry->image = *image;
    }

    return 0;
}

static elf_image_info_t *elf_image_alloc(void) {
    elf_image_info_t *image = kmalloc(sizeof(elf_image_info_t));
    if (image) {
        memset(image, 0, sizeof(*image));
    }
    return image;
}

/*
 * exec_reset_signals - Reset signal handlers on exec
 *
 * On exec(), all caught signals are reset to SIG_DFL.
 * Signals set to SIG_IGN remain ignored.
 * The thread signal mask is preserved across the image change.
 * Pending-thread signal state is not modified here.
 */
static void exec_reset_signals(void) {
    if (!current_process) return;
    
    for (int sig = 1; sig <= NSIG; sig++) {
        struct sigaction *act = &current_process->sig_actions[sig - 1];
        
        // If handler is a function pointer (caught signal), reset to default
        if (act->sa_handler != SIG_IGN && act->sa_handler != SIG_DFL) {
            act->sa_handler = SIG_DFL;
            memset(&act->sa_mask, 0, sizeof(act->sa_mask));
            act->sa_flags = 0;
        }
    }

    memset(current_process->linux_sig_restorer, 0,
           sizeof(current_process->linux_sig_restorer));

    // Clear sig_catch bitmask since all caught signals are now SIG_DFL
    current_process->sig_catch = 0;
    // sig_ignore remains unchanged - ignored signals stay ignored

    /* POSIX: per-process timers are disarmed and deleted across exec(). */
    proc_ptimers_clear(current_process);
}

static int is_linux_ldso_path(const char *interp_path) {
    if (!interp_path) return 0;
    return strcmp(interp_path, "/lib/ld-linux.so.2") == 0 ||
           strcmp(interp_path, "/usr/lib32/ld-linux.so.2") == 0 ||
           strcmp(interp_path, "/lib64/ld-linux-x86-64.so.2") == 0 ||
           strcmp(interp_path, "/usr/lib64/ld-linux-x86-64.so.2") == 0 ||
           strcmp(interp_path, "/lib/ld-linux-x86-64.so.2") == 0;
}

static fs_node_t *elf_lookup_interpreter(fs_node_t *root, const char *interp_path,
                                          const char *prefix) {
    static const struct {
        const char *requested;
        const char *fallback;
    } aliases[] = {
        { "/usr/lib32/ld-linux.so.2", "/lib/ld-linux.so.2" },
        { "/usr/lib64/ld-linux-x86-64.so.2", "/lib64/ld-linux-x86-64.so.2" },
        { "/lib/ld-linux-x86-64.so.2", "/lib64/ld-linux-x86-64.so.2" },
    };
    fs_node_t *node;

    if (!interp_path || !root) {
        return NULL;
    }

    /* Try personality prefix first: e.g. /perso/freebsd/libexec/ld-elf.so.1 */
    if (prefix && prefix[0]) {
        char prefixed[320];
        snprintf(prefixed, sizeof(prefixed), "%s%s", prefix, interp_path);
        node = vfs_lookup(root, prefixed);
        if (node) return node;
    }

    node = vfs_lookup(root, interp_path);
    if (node) {
        return node;
    }

    for (size_t i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
        if (strcmp(interp_path, aliases[i].requested) == 0) {
            node = vfs_lookup(root, aliases[i].fallback);
            if (node) return node;
        }
    }

    return NULL;
}

static int elf_machine_matches_kernel(const Elf32_Ehdr *ehdr) {
    if (!ehdr) return 0;

#if defined(__i386__)
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS32 || ehdr->e_machine != EM_386) {
        char buf[96];
        snprintf(buf, sizeof(buf), "ELF: Unsupported machine/class for i386 kernel (machine=%u class=%u)\n",
                (unsigned int)ehdr->e_machine, (unsigned int)ehdr->e_ident[EI_CLASS]);
        kprint(buf);
        return 0;
    }
#elif defined(__x86_64__)
    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 || ehdr->e_machine != EM_X86_64) {
        char buf[96];
        snprintf(buf, sizeof(buf), "ELF: Unsupported machine/class for x86_64 kernel (machine=%u class=%u)\n",
                (unsigned int)ehdr->e_machine, (unsigned int)ehdr->e_ident[EI_CLASS]);
        kprint(buf);
        return 0;
    }
#else
    #error "Unsupported architecture for ELF loader"
#endif

    return 1;
}

int elf_check_file(Elf32_Ehdr *hdr) {
    if (!hdr) return 0;
    if (hdr->e_ident[0] != ELFMAG0) return 0;
    if (hdr->e_ident[1] != ELFMAG1) return 0;
    if (hdr->e_ident[2] != ELFMAG2) return 0;
    if (hdr->e_ident[3] != ELFMAG3) return 0;
    return 1;
}

// Load ELF from file node and prepare for execution
// Returns entry point address on success, 0 on failure
static uint32_t elf_exec_main_load_base(const elf_image_info_t *image) {
    if (!image) {
        return 0;
    }

    if (image->ehdr.e_type == 3) {
        return ELF_ET_DYN_LOAD_BASE_I386;
    }

    return 0;
}

static uint32_t elf_runtime_phdr_addr(const elf_image_info_t *image, uint32_t load_base) {
    if (!image) {
        return 0;
    }

    return image->at_phdr + load_base;
}

uint32_t elf_load(fs_node_t *file, uint32_t load_base, int is_main_image,
                  char *interp_path, uint32_t *interp_len) {
    elf_image_info_t *image;
    const Elf32_Ehdr *ehdr;
    uint32_t entry;
    uint32_t max_vaddr = 0;
    int trace_elf;
    int trace_personality;
    uint32_t val;
    char hexbuf[16];
    void *pmap;
    uint32_t tls_vaddr = 0;
    uint32_t tls_filesz = 0;
    uint32_t tls_memsz = 0;
    uint32_t tls_align = 1;
    int has_tls = 0;

    if (interp_len) *interp_len = 0;

    if (!file || !file->read) {
        kprint("ELF: No file or read function\n");
        return 0;
    }

    image = elf_image_alloc();
    if (!image) {
        kprint("ELF: Failed to allocate image metadata\n");
        return 0;
    }

    if (elf_get_image_info(file, image) != 0) {
        kprint("ELF: Failed to read image metadata\n");
        kfree(image, sizeof(*image));
        return 0;
    }

    ehdr = &image->ehdr;

    if (ehdr->e_type != 2 && ehdr->e_type != 3) { // ET_EXEC or ET_DYN
        kprint("ELF: Not an executable or shared object\n");
        kfree(image, sizeof(*image));
        return 0;
    }
    
    if (!elf_machine_matches_kernel(ehdr)) {
        kfree(image, sizeof(*image));
        return 0;
    }

    entry = ehdr->e_entry + load_base;
    if (entry >= 0xC0000000U) {
        kprint("ELF: entry point in kernel space, rejecting\n");
        kfree(image, sizeof(*image));
        return 0;
    }
    val = entry;
    trace_elf = elf_debug_enabled();
    trace_personality = elf_personality_debug_enabled(image->detected_os);

    if (trace_elf) {
        kprint("ELF: Loading executable, entry=0x");
        for (int i = 7; i >= 0; i--) {
            int nib = (val >> (i * 4)) & 0xF;
            hexbuf[7 - i] = nib < 10 ? '0' + nib : 'A' + nib - 10;
        }
        hexbuf[8] = '\0';
        kprint(hexbuf);
        kprint("\n");
    }
    
    // Use pmap_t from vm_map.h/pmap.h
    pmap = pmap_kernel();
    if (current_process && current_process->pmap) {
        pmap = (void*)((uintptr_t)current_process->pmap);
    }

    (void)tls_vaddr;  // Will be used for debug output
    (void)tls_align;  // Will be used for proper alignment
    
    /* Track mapped VA ranges for overlap detection (finding #3) */
    struct { uint32_t start; uint32_t end; } mapped_ranges[256];
    int mapped_range_count = 0;

    for (int i = 0; i < image->phnum; i++) {
        Elf32_Phdr phdr = image->phdrs[i];

        if (phdr.p_type == PT_INTERP) {
            if (interp_path && interp_len && image->interp_len > 0) {
                uint32_t to_copy = (image->interp_len < (*interp_len - 1)) ? image->interp_len : (*interp_len - 1);
                memcpy(interp_path, image->interp_path, to_copy);
                interp_path[to_copy] = '\0';
                *interp_len = to_copy;
            }
        }
        
        // Detect TLS segment
        if (phdr.p_type == PT_TLS) {
            uint32_t tls_end = phdr.p_vaddr + load_base + phdr.p_memsz;
            // SECURITY CHECK: Validate TLS segment bounds (finding #2)
            if (tls_end < phdr.p_vaddr + load_base || tls_end >= 0xC0000000 ||
                phdr.p_vaddr + load_base >= 0xC0000000) {
                kprint("ELF: PT_TLS segment has invalid bounds\n");
                kfree(image, sizeof(*image));
                return 0;
            }
            tls_vaddr = phdr.p_vaddr;
            tls_filesz = phdr.p_filesz;
            tls_memsz = phdr.p_memsz;
            tls_align = phdr.p_align ? phdr.p_align : 1;
            has_tls = 1;
            if (trace_elf) {
                char tbuf[16];
                kprint("ELF: Found TLS segment, memsz=");
                for (int j = 7; j >= 0; j--) {
                    int nib = (tls_memsz >> (j * 4)) & 0xF;
                    tbuf[7 - j] = nib < 10 ? '0' + nib : 'A' + nib - 10;
                }
                tbuf[8] = '\0';
                kprint(tbuf);
                kprint("\n");
            }
        }
        
        if (phdr.p_type == PT_LOAD && phdr.p_memsz > 0) {
            if (trace_elf) {
                kprint("ELF: Mapping segment at 0x");
                val = phdr.p_vaddr;
                for (int j = 7; j >= 0; j--) {
                    int nib = (val >> (j * 4)) & 0xF;
                    hexbuf[7 - j] = nib < 10 ? '0' + nib : 'A' + nib - 10;
                }
                hexbuf[8] = '\0';
                kprint(hexbuf);
                kprint(", size=");
                val = phdr.p_memsz;
                for (int j = 7; j >= 0; j--) {
                    int nib = (val >> (j * 4)) & 0xF;
                    hexbuf[7 - j] = nib < 10 ? '0' + nib : 'A' + nib - 10;
                }
                hexbuf[8] = '\0';
                kprint(hexbuf);
                kprint("\n");
            }
            
            // Calculate page-aligned start and end
            uint32_t vaddr = phdr.p_vaddr + load_base;
            
            // SECURITY CHECK: Detect overflow in p_vaddr + load_base (finding #1)
            if (load_base != 0 && vaddr < phdr.p_vaddr) {
                kprint("ELF: Segment vaddr overflow (p_vaddr + load_base wraps)\n");
                kfree(image, sizeof(*image));
                return 0;
            }

            // SECURITY CHECK: Detect overflow in p_memsz + vaddr before performing addition
            if (phdr.p_memsz > 0xFFFFFFFFU - vaddr) {
                kprint("ELF: Segment size overflow\n");
                kfree(image, sizeof(*image));
                return 0;
            }

            // SECURITY/ROBUSTNESS CHECK: Disallow loading ELF segments into kernel space
            if (vaddr >= 0xC0000000 || (vaddr + phdr.p_memsz) >= 0xC0000000) {
                kprint("ELF: Refusing to load segment into kernel space\n");
                kfree(image, sizeof(*image));
                return 0;
            }
            uint32_t va_start = vaddr & 0xFFFFF000;
            uint32_t va_end = (vaddr + phdr.p_memsz + 0xFFF) & 0xFFFFF000;

            // SECURITY CHECK: Detect overlapping segments (finding #3)
            for (int j = 0; j < mapped_range_count; j++) {
                if (va_start < mapped_ranges[j].end && va_end > mapped_ranges[j].start) {
                    kprint("ELF: Overlapping PT_LOAD segments detected\n");
                    kfree(image, sizeof(*image));
                    return 0;
                }
            }
            if (mapped_range_count < 256) {
                mapped_ranges[mapped_range_count].start = va_start;
                mapped_ranges[mapped_range_count].end = va_end;
                mapped_range_count++;
            } else {
                kprint("ELF: Too many PT_LOAD segments (>256)\n");
                kfree(image, sizeof(*image));
                return 0;
            }

            
            // Allocate and map pages for this segment
            // Track PA for each VA so we can write to it via kernel mapping
            // Determine permissions from ELF segment flags
            int prot = 0;
            if (phdr.p_flags & 0x4) prot |= VM_PROT_READ;    // PF_R
            if (phdr.p_flags & 0x2) prot |= VM_PROT_WRITE;   // PF_W
            if (phdr.p_flags & 0x1) prot |= VM_PROT_EXEC;    // PF_X

            /*
             * SHARED FILE MAPPING FAST PATH.
             *
             * If the segment is fully file-backed (filesz == memsz, no BSS
             * tail) and we have a vm_map available, plumb it through a
             * shared vnode-backed vm_object instead of allocating fresh
             * physical pages and copying.  This is what lets every
             * process exec'ing the same binary share the same physical
             * `.text`/`.rodata` pages — the page cache does the
             * deduplication automatically the moment the second process
             * faults on a page already resident from the first.
             *
             * Page faults inside this region are resolved by vm_fault →
             * vnode pager, which reads the bytes from `file` on first
             * touch.  Writes (e.g. relocations into a writable
             * fully-file-backed segment, which is rare but possible)
             * trigger COW per the existing fault path.
             *
             * Falls back to the per-page pmm_alloc_block + read loop
             * below if the fast path can't be used.
             */
            if (current_process && current_process->vm_map &&
                phdr.p_filesz == phdr.p_memsz && phdr.p_filesz > 0) {
                /* p_offset and p_vaddr must be page-congruent (ELF spec
                 * §"Program Loading"); the file offset for va_start is
                 * therefore p_offset minus the same in-page slack. */
                uint64_t in_page_off = (uint64_t)(vaddr & 0xFFF);
                uint64_t file_off = (uint64_t)phdr.p_offset - in_page_off;
                size_t obj_len = (size_t)(va_end - va_start);

                vm_object_t *fobj = mmap_get_shared_backing_object(
                    file, obj_len, (uint32_t)prot, file_off);
                if (fobj) {
                    int rc = vm_map_insert(current_process->vm_map, fobj, 0,
                                           va_start, va_end,
                                           (uint8_t)prot,
                                           (uint8_t)VM_PROT_ALL,
                                           VM_INHERIT_COPY);
                    if (rc == 0) {
                        if (va_end > max_vaddr) max_vaddr = va_end;
                        /* Done: pages will lazy-fault through the pager. */
                        continue;
                    }
                    vm_object_deallocate(fobj);
                    kprint("ELF: vm_map_insert for shared segment failed; "
                           "falling back to per-page copy\n");
                }
                /* Fall through to legacy path on any allocation failure. */
            }

            // Legacy path: per-process anonymous pages + manual file copy.
            // Used for segments with BSS overflow (filesz != memsz), or
            // when the vm_map isn't available.
            //
            // We allocate an anonymous vm_object to *own* the populated
            // pages so that vm_map_destroy() / process exit will free them.
            // Pre-populating via pmap_enter alone (without an owning
            // vm_object) leaks the pages on exit AND leaves the address
            // range marked as free in the holes tree so that a later
            // anonymous mmap() can vm_map_find_space() right on top of our
            // .got/.data pages and (on first user touch) page-fault them
            // with zeros — silently corrupting the binary.
            uint32_t segment_pages = (va_end - va_start) / 0x1000;
            elf_page_map_t *page_maps;
            int num_pages = 0;

            page_maps = kmalloc(segment_pages * sizeof(*page_maps));
            if (!page_maps) {
                kprint("ELF: Failed to allocate page map array\n");
                kfree(image, sizeof(*image));
                return 0;
            }

            vm_object_t *seg_obj = NULL;
            int seg_obj_inserted = 0;
            if (current_process && current_process->vm_map) {
                seg_obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT,
                                             (size_t)(va_end - va_start));
                if (!seg_obj) {
                    kprint("ELF: Failed to allocate segment vm_object\n");
                    kfree(page_maps, segment_pages * sizeof(*page_maps));
                    kfree(image, sizeof(*image));
                    return 0;
                }
            }

            for (uint32_t va = va_start; va < va_end; va += 0x1000) {
                // Allocate physical page
                void *pa = pmm_alloc_block();
                if (!pa) {
                    kprint("ELF: Out of physical memory\n");
                    /* Free already-mapped pages for this segment (finding #11) */
                    for (int pi = 0; pi < num_pages; pi++) {
                        pmap_remove(pmap, page_maps[pi].va);
                        pmm_free_block(page_maps[pi].pa);
                    }
                    if (seg_obj) vm_object_deallocate(seg_obj);
                    kfree(page_maps, segment_pages * sizeof(*page_maps));
                    kfree(image, sizeof(*image));
                    return 0;
                }

                // Map with permissions from segment header
                // pmap_enter expects physical address, convert virtual to physical
                uint32_t pa_phys = (uint32_t)(uintptr_t)pa - 0xC0000000;
                if (pmap_enter(pmap, va, pa_phys, prot, 0) < 0) {
                    kprint("ELF: Failed to map page\n");
                    pmm_free_block(pa);
                    for (int pi = 0; pi < num_pages; pi++) {
                        pmap_remove(pmap, page_maps[pi].va);
                        pmm_free_block(page_maps[pi].pa);
                    }
                    if (seg_obj) vm_object_deallocate(seg_obj);
                    kfree(page_maps, segment_pages * sizeof(*page_maps));
                    kfree(image, sizeof(*image));
                    return 0;
                }

                /* Hand the underlying physical page to the segment's
                 * vm_object so process teardown frees it.  pmm_alloc_block
                 * already came from vm_phys_alloc_page_below, so the
                 * vm_page_t for this paddr exists in the global page array
                 * — we just relink it to seg_obj. */
                if (seg_obj) {
                    vm_page_t *vp = vm_phys_paddr_to_page(pa_phys);
                    if (vp) {
                        uint64_t pindex = (uint64_t)((va - va_start) / 0x1000);
                        vm_page_insert(vp, seg_obj, pindex);
                    }
                }

                // Save mapping for later access via kernel space
                page_maps[num_pages].va = va;
                page_maps[num_pages].pa = pa;
                num_pages++;

                // Zero the page - pa is already virtual from pmm_alloc_block
                memset(pa, 0, 0x1000);
            }


            // Now copy file data to the mapped segment via kernel space
            // We need to translate user VA to kernel VA via the physical address
            // Simple approach: read directly from the file into the mapped kernel pages
            if (phdr.p_filesz > 0) {
                uint32_t bytes_copied = 0;
                for (int pi = 0; pi < num_pages && bytes_copied < phdr.p_filesz; pi++) {
                    uint32_t page_va = page_maps[pi].va;
                    uint32_t segment_va = phdr.p_vaddr + load_base;


                    // Does this page overlap with the segment?
                    uint32_t page_end = page_va + 0x1000;
                    uint32_t segment_end = segment_va + phdr.p_filesz;

                    if (page_va < segment_end && page_end > segment_va) {
                        // Calculate overlap
                        uint32_t copy_start_va = (page_va > segment_va) ? page_va : segment_va;
                        uint32_t copy_end_va = (page_end < segment_end) ? page_end : segment_end;
                        uint32_t copy_size = copy_end_va - copy_start_va;

                        // Offset into page
                        uint32_t offset_in_page = copy_start_va - page_va;
                        // Offset into segment data
                        uint32_t offset_in_segment = copy_start_va - segment_va;

                        // Read directly to kernel-mapped page (pa is already virtual)
                        uint8_t *dest = (uint8_t *)page_maps[pi].pa + offset_in_page;
                        /* p_offset+p_filesz was already overflow-checked
                         * at parse time; offset_in_segment is bounded by
                         * the segment so this addition is safe. */
                        if (file->read(file, phdr.p_offset + offset_in_segment, copy_size, dest) != copy_size) {
                            kprint("ELF: Failed to read segment data directly\n");
                            for (int ri = 0; ri < num_pages; ri++) {
                                pmap_remove(pmap, page_maps[ri].va);
                                pmm_free_block(page_maps[ri].pa);
                            }
                            if (seg_obj && !seg_obj_inserted) vm_object_deallocate(seg_obj);
                            kfree(page_maps, segment_pages * sizeof(*page_maps));
                            kfree(image, sizeof(*image));
                            return 0;
                        }
                        bytes_copied += copy_size;
                    }
                }
            }

            kfree(page_maps, segment_pages * sizeof(*page_maps));

            // BSS is already zeroed since we memset each page

            if (va_end > max_vaddr) max_vaddr = va_end;

            /*
             * Insert the populated vm_object into the vm_map.  This both
             * reserves the address range against vm_map_find_space() (so
             * later anonymous mmaps don't overlay the binary's .got/.data)
             * AND ties the underlying physical pages to the process so
             * that vm_map_destroy() at exit reclaims them.  The vm_fault
             * path will treat the pages as already-resident (each is on
             * seg_obj->pages with the right pindex) and won't re-page
             * them with zeros even on a spurious fault.
             */
            if (seg_obj) {
                int rc = vm_map_insert(current_process->vm_map, seg_obj, 0,
                                       va_start, va_end,
                                       (uint8_t)prot,
                                       (uint8_t)VM_PROT_ALL,
                                       VM_INHERIT_COPY);
                if (rc == 0) {
                    seg_obj_inserted = 1;
                } else {
                    if (trace_elf) {
                        kprintf("ELF: vm_map_insert failed for [0x%08x,0x%08x); leaking pages until exit\n",
                                va_start, va_end);
                    }
                    /* Last-resort: drop the object reference; vm_object_deallocate
                     * will free the pages we just inserted into it. */
                    vm_object_deallocate(seg_obj);
                }
            }
        }
    }
    
    // TLS setup is handled by uClibc via set_thread_area syscall.
    // We do NOT pre-allocate TLS or set GDT entry 6 base here.
    // uClibc's __libc_setup_tls will:
    //   1. Find PT_TLS via _dl_phdr (from AT_PHDR in auxv)
    //   2. Allocate TLS block via sbrk()
    //   3. Copy TLS init data
    //   4. Call set_thread_area to configure GDT entry
    //
    // We only suppress unused variable warnings here.
    (void)has_tls;
    (void)tls_memsz;
    (void)tls_filesz;
    
    // Detect personality based on OSABI
    int detected_os = image->detected_os;
    
    if (trace_elf || trace_personality) {
        kprint("ELF: Personality: ");
        if (detected_os == ELFOSABI_LINUX) kprint("Linux\n");
        else if (detected_os == ELFOSABI_FREEBSD) kprint("FreeBSD\n");
        else if (detected_os == ELFOSABI_NETBSD) kprint("NetBSD\n");
        else if (detected_os == ELFOSABI_OPENBSD) kprint("OpenBSD\n");
        else kprint("Native\n");
    }
    
    if (current_process) {
        switch (detected_os) {
            case ELFOSABI_FREEBSD:
                current_process->perso_id = PERS_FREEBSD;
                break;
            case ELFOSABI_LINUX:
                current_process->perso_id = PERS_LINUX;
                break;
            case ELFOSABI_NETBSD:
                current_process->perso_id = PERS_NETBSD;
                break;
            case ELFOSABI_OPENBSD:
                current_process->perso_id = PERS_OPENBSD;
                break;
            default:
                current_process->perso_id = PERS_NATIVE;
                break;
        }
        
        // Set Bitness
        if (ehdr->e_ident[EI_CLASS] == ELFCLASS64) {
             current_process->bitness = BITNESS_64;
        } else {
             current_process->bitness = BITNESS_32;
        }

        
        if (is_main_image) {
            current_process->brk_start = max_vaddr;
            current_process->brk = max_vaddr;
        }
    }

    kfree(image, sizeof(*image));
    return entry;
}

static int is_user_ptr(const void *ptr) {
    return (uintptr_t)ptr < 0xC0000000;
}

static int capture_ptr(char *const array[], int index, char **out) {
    if (is_user_ptr(array)) {
        return copyin(&array[index], out, sizeof(char*));
    } else {
        *out = array[index];
        return 0;
    }
}

#define ARG_MAX_BYTES (32 * 1024)
#define ARG_MAX_COUNT 4096

static int exec_count_args(char *const array[], int *count_out, const char *err_msg) {
    int count = 0;
    if (array) {
        while (count < ARG_MAX_COUNT) {
            char *uarg;
            if (capture_ptr(array, count, &uarg) != 0 || uarg == NULL) break;
            count++;
            if (count > (int)(ARG_MAX_BYTES / 4)) {
                kprint(err_msg);
                return -7; // E2BIG
            }
        }
    }
    *count_out = count;
    return 0;
}

static int exec_copy_args(char *const array[], int count, char **k_array, char **p_buf, size_t *remaining, const char *err_msg) {
    int from_user = is_user_ptr(array);

    for (int i = 0; i < count; i++) {
        char *uarg;
        int ret;
        if ((ret = capture_ptr(array, i, &uarg)) != 0) {
            return (ret == -1) ? -14 : -1; // Map EFAULT
        }
        k_array[i] = *p_buf;

        size_t copied_len = 0;
        if (from_user) {
            /* Full userspace call: all string pointers MUST be user pointers */
            if (!is_user_ptr(uarg)) return -14; 
            ret = copyinstr(uarg, *p_buf, *remaining, &copied_len);
        } else {
            /* 
             * Internal kernel call (e.g. init or #! script interpreter).
             * Pointers in the array can be either kernel or user (from original argv).
             */
            if (!uarg) {
                ret = -14;
            } else if (is_user_ptr(uarg)) {
                ret = copyinstr(uarg, *p_buf, *remaining, &copied_len);
            } else {
                size_t len = strnlen(uarg, *remaining);
                if (len == *remaining) {
                    ret = -7; // E2BIG
                } else {
                    memcpy(*p_buf, uarg, len);
                    (*p_buf)[len] = '\0';
                    copied_len = len + 1;
                    ret = 0;
                }
            }
        }

        if (ret != 0) {
            kprint(err_msg);
            if (ret == -2) return -7; // E2BIG
            if (ret == -1 || ret == -14) return -14; // EFAULT
            return (ret < 0) ? ret : -1;
        }

        *p_buf += copied_len;
        *remaining -= copied_len;
    }
    return 0;
}

// Helper to set up the user stack
static int exec_setup_stack(pmap_t pmap, uint32_t *sp_out, char **k_argv, int argc, char **k_envp, int envc,
                            uint32_t at_entry, uint32_t at_base, uint32_t at_phdr,
                            const elf_image_info_t *image,
                            uint32_t *ps_strings_out) {
    uint32_t user_stack_top = 0xC0000000;
    /*
     * Demand-paged user stack.  Only a small region at the top is
     * mapped up front — enough for argv/envp/auxv and the program's
     * first few frames; the page-fault handler grows the stack
     * downward one page at a time on access (see vm_grow_user_stack
     * in arch/i386/idt.c), so a process only ever costs the stack it
     * actually touches instead of a fixed multi-MiB reservation.
     * USER_STACK_MAX caps how far it may grow.
     */
    #define USER_STACK_EAGER_PAGES 32        /* 128 KiB mapped at exec */
    #define USER_STACK_MAX        0x800000   /* 8 MiB grow-down ceiling */
    uint32_t user_stack_size = USER_STACK_EAGER_PAGES;
    uint32_t user_stack_base = user_stack_top - (user_stack_size * 0x1000);

    /*
     * Track physical addresses for kernel-space access.  Heap-allocated
     * because the array sizes with user_stack_size and is too big to
     * keep on the 8 KiB kernel stack — at 8 B per entry × 1024 pages
     * that's the entire kstack frame, and exec_setup_stack's saved
     * return state gets clobbered (manifested as wild user-space EIP
     * after exec).
     */
    typedef struct { uint32_t va; void *pa; } stack_page_t;
    stack_page_t *stack_pages = kmalloc(sizeof(stack_page_t) * user_stack_size);
    if (!stack_pages) {
        kprint("execve: Out of memory tracking user stack pages\n");
        return -1;
    }
    uint32_t mapped_stack_pages = 0;
    
    for (uint32_t i = 0; i < user_stack_size; i++) {
        uint32_t va = user_stack_base + i * 0x1000;
        void *pa = pmm_alloc_block();
        if (!pa) {
            kprint("execve: Out of memory for user stack\n");
            for (uint32_t j = 0; j < mapped_stack_pages; j++) {
                pmap_remove(pmap, stack_pages[j].va);
                pmm_free_block(stack_pages[j].pa);
            }
            kfree(stack_pages, sizeof(stack_page_t) * user_stack_size);
            return -1;
        }
        uint32_t pa_phys = (uint32_t)(uintptr_t)pa - 0xC0000000;
        if (pmap_enter(pmap, va, pa_phys, VM_PROT_WRITE, 0) < 0) {
            kprint("execve: Failed to map user stack\n");
            pmm_free_block(pa);
            for (uint32_t j = 0; j < mapped_stack_pages; j++) {
                pmap_remove(pmap, stack_pages[j].va);
                pmm_free_block(stack_pages[j].pa);
            }
            kfree(stack_pages, sizeof(stack_page_t) * user_stack_size);
            return -1;
        }
        stack_pages[i].va = va;
        stack_pages[i].pa = pa;
        mapped_stack_pages++;
        memset(pa, 0, 0x1000);
    }

    /* Record the stack bounds so the page-fault handler can grow the
     * stack on demand below the eagerly-mapped region. */
    if (current_process) {
        current_process->ustack_top   = user_stack_top;
        current_process->ustack_limit = user_stack_top - USER_STACK_MAX;
        /* Reset the live argv window; it is filled in below once the argv
         * strings have been laid out at known user addresses. */
        current_process->arg_start = 0;
        current_process->arg_end   = 0;
    }
    
    /*
     * Reserve 16 bytes at the very top for `struct ps_strings` —
     * but ONLY for NetBSD/OpenBSD.  Their _start
     * (lib/csu/arch/i386/crt0.S) reads its argv/envp from this struct
     * via %ebx, not from argc-on-stack, so the kernel must construct
     * it and pass its address.  Linux/FreeBSD don't read here, and
     * shifting their sp by 16 bytes was triggering a regression in
     * FreeBSD's __stack_chk_fail path — so leave their layout
     * untouched (sp = user_stack_top - 4 as before).
     */
    int needs_ps_strings = current_process &&
        (current_process->perso_id == PERS_NETBSD ||
         current_process->perso_id == PERS_OPENBSD);
    uint32_t ps_strings_addr = 0;
    uint32_t sp = user_stack_top - 4;
    if (needs_ps_strings) {
        ps_strings_addr = user_stack_top - 16;
        sp = ps_strings_addr - 4;
    }
    
    // Helper to write to user stack via kernel mapping
    #define STACK_WRITE32(user_va, val) do { \
        uint32_t page_idx = ((user_va) - user_stack_base) / 0x1000; \
        uint32_t offset = ((user_va) - user_stack_base) % 0x1000; \
        if (page_idx < user_stack_size) { \
            uint32_t *kptr = (uint32_t*)((uint8_t*)stack_pages[page_idx].pa + offset); \
            *kptr = (val); \
        } \
    } while(0)
    
    /* Strings are packed back-to-back (no per-string alignment): byte strings
     * have no alignment requirement, and packing keeps the argv/envp regions
     * contiguous with single NUL separators, so /proc/<pid>/cmdline reads
     * exactly like Linux's.  The auxv and pointer arrays below are realigned
     * (sp &= ~15) independently. */
    #define PUSH_STRING(str, ptr_out) do { \
        const char *s = (str); \
        size_t len = strlen(s) + 1; \
        sp -= len; \
        ptr_out = sp; \
        for (size_t i = 0; i < len; i++) { \
            uint32_t addr = ptr_out + i; \
            uint32_t page_idx = (addr - user_stack_base) / 0x1000; \
            uint32_t offset = (addr - user_stack_base) % 0x1000; \
            if (page_idx < user_stack_size) { \
                uint8_t *kptr = (uint8_t*)stack_pages[page_idx].pa + offset; \
                *kptr = s[i]; \
            } \
        } \
    } while(0)

    if (k_envp) {
        for (int i = envc - 1; i >= 0; i--) {
            uint32_t user_ptr;
            PUSH_STRING(k_envp[i], user_ptr);
            k_envp[i] = (char*)(uintptr_t)user_ptr;
        }
    }

    if (k_argv) {
        uint32_t arg_hi = 0;
        for (int i = argc - 1; i >= 0; i--) {
            uint32_t user_ptr;
            size_t slen = strlen(k_argv[i]) + 1;
            PUSH_STRING(k_argv[i], user_ptr);
            /* argv[argc-1] is pushed first, so it lands at the highest argv
             * address; its end bounds the top of the live argv region. */
            if (i == argc - 1) arg_hi = user_ptr + (uint32_t)slen;
            k_argv[i] = (char*)(uintptr_t)user_ptr;
        }
        /* argv[0] is pushed last (lowest address).  Record [argv[0], end of
         * argv[argc-1]) as the live argv region for /proc/<pid>/cmdline. */
        if (current_process && argc > 0 &&
            arg_hi > (uint32_t)(uintptr_t)k_argv[0]) {
            current_process->arg_start = (uint32_t)(uintptr_t)k_argv[0];
            current_process->arg_end   = arg_hi;
        }
    }

    /* Place data blobs at the top of the stack, above the auxv array.
     * FreeBSD (and Linux) rtld walks auxv as a contiguous Elf_Auxinfo[]
     * array starting right after the envp NULL terminator.  Any raw bytes
     * embedded inside the array corrupt that walk and cause assertion
     * failures (e.g. rtld.c:565 assert(aux_info[AT_BASE] != NULL)).
     * Strategy: push all string/data blobs here first (highest addresses),
     * record their user-space pointers, then emit a clean auxv below. */
    uint32_t platform_ptr;
    PUSH_STRING("i686", platform_ptr);
    uint32_t pagesizes_ptr = 0;   /* AT_PAGESIZES blob, filled in below */

    /* Random buffer used for both AT_RANDOM (Linux, first 16 bytes) and
     * AT_FBSD_CANARY (FreeBSD, all 64 bytes).  FreeBSD libc's __guard_setup
     * calls _elf_aux_info(AT_CANARY, &__stack_chk_guard, sizeof(...)) where
     * __stack_chk_guard is `long[8]` = 32 bytes on i386.  _elf_aux_info
     * requires AT_CANARYLEN >= buflen, so we must publish at least 32 bytes;
     * 64 leaves headroom for future LP64 ports (long[8] = 64 bytes). */
    uint8_t rand_buf[64];
    int rand_rc = random_get_bytes_flags(rand_buf, sizeof(rand_buf), GRND_NONBLOCK);
    if (rand_rc != (int)sizeof(rand_buf)) {
        /* Avoid stalling exec during early boot when entropy is still low. */
        random_get_bytes_flags(rand_buf, sizeof(rand_buf), GRND_INSECURE);
    }
    sp -= sizeof(rand_buf);
    sp &= ~15;
    uint32_t rand_ptr = sp;
    for (unsigned i = 0; i < sizeof(rand_buf) / 4; i++) {
        uint32_t val;
        memcpy(&val, &rand_buf[i * 4], 4);
        STACK_WRITE32(sp + i * 4, val);
    }

    /* Page-sizes array for FreeBSD AT_PAGESIZES.  FreeBSD libc's
     * getpagesizes() — called from libthr/jemalloc init (thr_malloc.c) even
     * in statically-linked binaries — reads the supported page sizes from
     * AT_PAGESIZES/AT_PAGESIZESLEN, falling back to a sysctl(hw.pagesizes) we
     * don't implement.  With neither, getpagesizes() returns ENOENT and the
     * process aborts ("Unable to read page sizes").
     *
     * libc reads with buflen == sizeof(u_long[MAXPAGESIZES]) and
     * _elf_aux_info() only serves AT_PAGESIZES when AT_PAGESIZESLEN >= that
     * buflen, so we must publish the FULL MAXPAGESIZES-slot array (unused
     * slots zeroed; getpagesizes trims trailing zeros) and set the length to
     * its full byte size — not just the one populated entry.  On i386
     * MAXPAGESIZES == 2, so this is u_long[2] = {4096, 0} (8 bytes). */
    sp -= 8;
    pagesizes_ptr = sp;
    STACK_WRITE32(sp,     4096);
    STACK_WRITE32(sp + 4, 0);

    /* Align to 16 before the auxv array. */
    sp &= ~15;

    if (!image) {
        kprint("execve: Missing ELF image metadata for AUXV\n");
        kfree(stack_pages, sizeof(stack_page_t) * user_stack_size);
        return -1;
    }

    /* Emit a clean, contiguous Elf_Auxinfo[] array.
     * AT_NULL is pushed first so it lands at the highest address;
     * subsequent entries are pushed below it.  The rtld walks from
     * the lowest entry (last pushed) up to AT_NULL.
     *
     * Indices 0..14 are POSIX-aligned and identical between Linux and
     * FreeBSD.  Indices >=15 diverge — Linux has AT_PLATFORM/AT_HWCAP/
     * AT_CLKTCK/AT_SECURE/AT_RANDOM/AT_EXECFN at 15/16/17/23/25/31, while
     * FreeBSD assigns those slots to AT_EXECPATH/AT_CANARY/AT_CANARYLEN/
     * AT_STACKPROT/AT_HWCAP/AT_ENVV.  Sending Linux entries to a FreeBSD
     * process therefore makes libc read garbage — most visibly, FreeBSD
     * libc reads our AT_EXECFN (argv[0] string pointer) as AT_ENVV (the
     * environ array), and the next getenv() walks off into the weeds. */

    int is_freebsd = current_process && current_process->perso_id == PERS_FREEBSD;
    int is_netbsd  = current_process && current_process->perso_id == PERS_NETBSD;

    uint32_t execfn_ptr = 0;
    if (k_argv && argc > 0) execfn_ptr = (uint32_t)(uintptr_t)k_argv[0];

    sp -= 4; STACK_WRITE32(sp, 0);
    sp -= 4; STACK_WRITE32(sp, AT_NULL);

    sp -= 4; STACK_WRITE32(sp, at_entry);
    sp -= 4; STACK_WRITE32(sp, AT_ENTRY);

    sp -= 4; STACK_WRITE32(sp, image->ehdr.e_phnum);
    sp -= 4; STACK_WRITE32(sp, AT_PHNUM);

    sp -= 4; STACK_WRITE32(sp, image->ehdr.e_phentsize);
    sp -= 4; STACK_WRITE32(sp, AT_PHENT);

    sp -= 4; STACK_WRITE32(sp, at_phdr);
    sp -= 4; STACK_WRITE32(sp, AT_PHDR);

    sp -= 4; STACK_WRITE32(sp, 4096);
    sp -= 4; STACK_WRITE32(sp, AT_PAGESZ);

    sp -= 4; STACK_WRITE32(sp, 0);
    sp -= 4; STACK_WRITE32(sp, AT_FLAGS);

    sp -= 4; STACK_WRITE32(sp, at_base);
    sp -= 4; STACK_WRITE32(sp, AT_BASE);

    /* Linux/generic credential auxv.  NetBSD numbers these differently and
     * reuses a_type 13 for AT_STACKBASE, so it emits its own set below. */
    if (!is_netbsd) {
        sp -= 4; STACK_WRITE32(sp, current_process->uid);
        sp -= 4; STACK_WRITE32(sp, AT_UID);
        sp -= 4; STACK_WRITE32(sp, current_process->gid);
        sp -= 4; STACK_WRITE32(sp, AT_GID);
        sp -= 4; STACK_WRITE32(sp, current_process->euid);
        sp -= 4; STACK_WRITE32(sp, AT_EUID);
        sp -= 4; STACK_WRITE32(sp, current_process->egid);
        sp -= 4; STACK_WRITE32(sp, AT_EGID);
    }

    if (is_netbsd) {
        /*
         * NetBSD auxv (sys/kern/exec_elf.c): the common AT_PHDR/PHENT/PHNUM/
         * PAGESZ/BASE/FLAGS/ENTRY above already match; NetBSD additionally
         * wants AT_STACKBASE (the low end of the main-thread stack region,
         * ep_minsaddr) and the Solaris-numbered credential entries.  No
         * Linux AT_PLATFORM/RANDOM/EXECFN — NetBSD's rtld neither needs nor
         * expects them.
         */
        sp -= 4; STACK_WRITE32(sp, user_stack_top - USER_STACK_MAX);
        sp -= 4; STACK_WRITE32(sp, AT_NETBSD_STACKBASE);
        sp -= 4; STACK_WRITE32(sp, current_process->euid);
        sp -= 4; STACK_WRITE32(sp, AT_NETBSD_EUID);
        sp -= 4; STACK_WRITE32(sp, current_process->uid);
        sp -= 4; STACK_WRITE32(sp, AT_NETBSD_RUID);
        sp -= 4; STACK_WRITE32(sp, current_process->egid);
        sp -= 4; STACK_WRITE32(sp, AT_NETBSD_EGID);
        sp -= 4; STACK_WRITE32(sp, current_process->gid);
        sp -= 4; STACK_WRITE32(sp, AT_NETBSD_RGID);
    } else if (is_freebsd) {
        /* FreeBSD-specific entries.  Order doesn't matter (rtld walks
         * the array indexing by a_type), but use BSD a_type values. */
        sp -= 4; STACK_WRITE32(sp, execfn_ptr);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_EXECPATH);

        /* Stack canary: point at the 64 random bytes we already pushed.
         * FreeBSD libc's __guard_setup requests sizeof(__stack_chk_guard) =
         * 32 bytes (long[8] on i386), and _elf_aux_info requires
         * AT_CANARYLEN >= the request.  Publishing 16 here used to fall
         * through to a sysctl(KERN_ARND) we don't implement, leaving the
         * guard at the {0,0,'\\n',0xff} terminator canary while compiled
         * code wrote real (non-terminator) bytes — every function return
         * tripped __stack_chk_fail and aborted via the syslog/abort path. */
        sp -= 4; STACK_WRITE32(sp, rand_ptr);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_CANARY);
        sp -= 4; STACK_WRITE32(sp, 64);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_CANARYLEN);

        sp -= 4; STACK_WRITE32(sp, 1403000);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_OSRELDATE);

        sp -= 4; STACK_WRITE32(sp, 1);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_NCPUS);

        /* Supported page sizes (one 4 KiB u_long entry); getpagesizes()
         * needs both the array pointer and its byte length. */
        sp -= 4; STACK_WRITE32(sp, pagesizes_ptr);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_PAGESIZES);
        sp -= 4; STACK_WRITE32(sp, 8);   /* MAXPAGESIZES(2) * sizeof(u_long)(4) */
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_PAGESIZESLEN);

        /* Main-thread user stack bounds.  FreeBSD libthr's thr_init
         * (__thr_get_main_stack_base / _lim) reads AT_USRSTACKBASE (the
         * top of the user stack) and AT_USRSTACKLIM (its grow-down limit)
         * via _elf_aux_info, falling back to a sysctl(kern.usrstack) we
         * don't implement -- without them it aborts ("Cannot get
         * kern.usrstack", thr_init.c).  libc only serves non-zero values. */
        sp -= 4; STACK_WRITE32(sp, user_stack_top);     /* 0xC0000000 */
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_USRSTACKBASE);
        sp -= 4; STACK_WRITE32(sp, USER_STACK_MAX);     /* 8 MiB ceiling */
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_USRSTACKLIM);

        /* PROT_READ|PROT_WRITE = 0x3 — stack protection FreeBSD libc
         * uses to refrain from setting PROT_EXEC on returns. */
        sp -= 4; STACK_WRITE32(sp, 0x3);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_STACKPROT);

        sp -= 4; STACK_WRITE32(sp, 0);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_HWCAP);

        sp -= 4; STACK_WRITE32(sp, 0);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_BSDFLAGS);

        sp -= 4; STACK_WRITE32(sp, (uint32_t)argc);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_ARGC);

        sp -= 4; STACK_WRITE32(sp, (uint32_t)envc);
        sp -= 4; STACK_WRITE32(sp, AT_FBSD_ENVC);
    } else {
        if (current_process && current_process->perso_id == PERS_LINUX) {
            sp -= 4; STACK_WRITE32(sp, HZ);
            sp -= 4; STACK_WRITE32(sp, AT_CLKTCK);

            sp -= 4; STACK_WRITE32(sp, 0);
            sp -= 4; STACK_WRITE32(sp, AT_HWCAP);
        }

        sp -= 4; STACK_WRITE32(sp, rand_ptr);
        sp -= 4; STACK_WRITE32(sp, AT_RANDOM);

        sp -= 4; STACK_WRITE32(sp, 0);
        sp -= 4; STACK_WRITE32(sp, AT_SECURE);

        sp -= 4; STACK_WRITE32(sp, platform_ptr);
        sp -= 4; STACK_WRITE32(sp, AT_PLATFORM);

        sp -= 4; STACK_WRITE32(sp, execfn_ptr);
        sp -= 4; STACK_WRITE32(sp, AT_EXECFN);
    }

    /* envp pointer array (NULL-terminated), then argv, then argc.
     * Capture the user-space addresses of the argv and envp ARRAYS
     * (i.e. address of argv[0], address of envp[0]) for ps_strings. */
    sp -= 4; STACK_WRITE32(sp, 0);
    uint32_t envp_arr_user = sp;  /* tentative: NULL-only case */
    if (k_envp) {
        for (int i = envc - 1; i >= 0; i--) {
            sp -= 4;
            STACK_WRITE32(sp, (uint32_t)(uintptr_t)k_envp[i]);
        }
        envp_arr_user = sp;  /* address of envp[0] */
    }

    sp -= 4; STACK_WRITE32(sp, 0);
    uint32_t argv_arr_user = sp;
    if (k_argv) {
        for (int i = argc - 1; i >= 0; i--) {
            sp -= 4;
            STACK_WRITE32(sp, (uint32_t)(uintptr_t)k_argv[i]);
        }
        argv_arr_user = sp;
    }

    sp -= 4; STACK_WRITE32(sp, argc);

    /* Populate the ps_strings struct at the reserved top slot, if any. */
    if (needs_ps_strings) {
        STACK_WRITE32(ps_strings_addr +  0, argv_arr_user);
        STACK_WRITE32(ps_strings_addr +  4, (uint32_t)argc);
        STACK_WRITE32(ps_strings_addr +  8, envp_arr_user);
        STACK_WRITE32(ps_strings_addr + 12, (uint32_t)envc);
    }

    /*
     * FreeBSD initial main-thread "curthread" placeholder.
     *
     * FreeBSD libthr reads curthread from %gs:8 (tcb_thread) and dereferences
     * it during very early startup — before its own _thr_init runs — e.g.
     * __pthread_cleanup_push_imp touches curthread->cleanup at +0x188.  The
     * rtld/csu install the program's TLS via sysarch(I386_SET_GSBASE) with a
     * fresh TCB whose tcb_thread is NULL, so that read returns NULL and the
     * first libthr call faults (SIGSEGV at addr ~0x188).
     *
     * We do NOT touch %gs at exec (static libc and the rtld both set up their
     * own TLS and assume %gs starts cleared — seeding it makes static libc's
     * __libc_setup_tls dereference a NULL DTV).  Instead, reserve a zeroed,
     * permanently-mapped placeholder "struct pthread" at the base of the eager
     * stack region (page 0 is already memset(0) above) and record its address;
     * the sysarch handler injects it into tcb_thread when the installed TCB has
     * none yet.  It sits far below the initial %esp/argv/auxv and is dead once
     * libthr stores the real curthread.
     */
    if (current_thread && current_process &&
        current_process->perso_id == PERS_FREEBSD) {
        current_thread->fbsd_init_curthread = user_stack_base;  /* zeroed block */
    }

    #undef STACK_WRITE32
    #undef PUSH_STRING

    *sp_out = sp;
    if (ps_strings_out) *ps_strings_out = ps_strings_addr;
    kfree(stack_pages, sizeof(stack_page_t) * user_stack_size);
    return 0;
}

// Execute a binary - loads ELF and prepares for userspace transition
// Returns 0 on success, negative error code on failure
int elf_execve(int fd, const char *path, char *const argv[], char *const envp[]) {
    elf_image_info_t *image = NULL;
    fs_node_t *root = (current_process && current_process->root_node) ? current_process->root_node : fs_root;
    pmap_t old_pmap = NULL;
    /*
     * Save the old vm_map up-front: we install a fresh vm_map before
     * elf_load() runs so that PT_LOAD segments can be inserted with
     * vm_map_insert() against vnode-backed shared vm_objects.  On
     * rollback we restore old_vm_map and destroy the new one; on
     * commit we destroy the old one.
     */
    struct vm_map *old_vm_map = current_process ? current_process->vm_map : NULL;
    struct vm_map *new_vm_map = NULL;
    int old_perso_id = current_process ? current_process->perso_id : PERS_NATIVE;
    int switched_pmap = 0;
    int vm_state_committed = 0;
    if (!root) {
        if (fd >= 0) kern_close(fd);
        return -1;
    }

    // ARG_MAX: Maximum bytes for arguments + environment
    // We use a fixed 32KB buffer to avoid Double Fetch / TOCTOU issues.
    char **k_argv = NULL;
    char **k_envp = NULL;
    char *arg_buffer = NULL;
    int argc = 0;
    int envc = 0;
    int error_code = -1;
    int ret;

    fs_node_t *file = NULL;
    if (fd >= 0 && fd < MAX_FD && current_process && current_process->fds[fd]) {
        file = (fs_node_t *)current_process->fds[fd]->f_data;
    } else {
        // Fallback or error if fd is invalid (though exec_dispatch should pass a valid fd)
        file = vfs_lookup(root, path);
    }

    if (!file) {
        kprint("execve: File not found: ");
        kprint(path);
        kprint("\n");
        if (fd >= 0) kern_close(fd);
        return -2; // ENOENT
    }

    if ((file->flags & 0x7) != FS_FILE) {
        kprint("execve: Not a regular file\n");
        if (fd >= 0) kern_close(fd);
        return -1;
    }

    image = elf_image_alloc();
    if (!image) {
        if (fd >= 0) kern_close(fd);
        return -12;
    }

    if (elf_get_image_info(file, image) != 0) {
        kprint("execve: Failed to load executable metadata\n");
        kfree(image, sizeof(*image));
        if (fd >= 0) kern_close(fd);
        return -ENOEXEC;
    }

    // Capture arguments and environment
    ret = exec_count_args(argv, &argc, "execve: Too many arguments\n");
    if (ret < 0) return ret;

    ret = exec_count_args(envp, &envc, "execve: Too many env vars\n");
    if (ret < 0) return ret;

    // Allocate buffers
    if (argc > 0) {
        k_argv = kmalloc((argc + 1) * sizeof(char*));
        if (!k_argv) return -12; // ENOMEM
        k_argv[argc] = NULL;
    }

    if (envc > 0) {
        k_envp = kmalloc((envc + 1) * sizeof(char*));
        if (!k_envp) {
            if (k_argv) kfree(k_argv, (argc + 1) * sizeof(char*));
            return -12;
        }
        k_envp[envc] = NULL;
    }

    // Always allocate full buffer to avoid TOCTOU re-measurement
    arg_buffer = kmalloc(ARG_MAX_BYTES);
    if (!arg_buffer) {
        if (k_argv) kfree(k_argv, (argc + 1) * sizeof(char*));
        if (k_envp) kfree(k_envp, (envc + 1) * sizeof(char*));
        return -12;
    }

    // Copy strings
    char *p_buf = arg_buffer;
    size_t remaining = ARG_MAX_BYTES;

    ret = exec_copy_args(argv, argc, k_argv, &p_buf, &remaining, "execve: Failed to copy argument\n");
    if (ret < 0) {
        error_code = ret;
        goto cleanup;
    }

    ret = exec_copy_args(envp, envc, k_envp, &p_buf, &remaining, "execve: Failed to copy env\n");
    if (ret < 0) {
        error_code = ret;
        goto cleanup;
    }

    // Create new address space for this process
    pmap_t new_pmap = pmap_create();
    if (!new_pmap) {
        kprint("execve: Failed to create pmap\n");
        error_code = -1;
        goto cleanup;
    }

    // Assign to process immediately so elf_load uses it
    if (current_process) {
        old_pmap = (pmap_t)(uintptr_t)current_process->pmap;
        current_process->pmap = (struct pmap *)(uintptr_t)new_pmap;
    }

    // Switch to new address space NOW so pmap_enter works (uses recursive mapping of active PD)
    pmap_activate(new_pmap);
    switched_pmap = 1;

    // Build the new vm_map before loading any ELF segments so that
    // elf_load() can use vm_map_insert() against vnode-backed shared
    // objects (.text/.rodata pages of the same binary will then
    // physically share between every process exec'ing it).
    new_vm_map = vm_map_create(new_pmap, 0x10000, 0xC0000000);
    if (!new_vm_map) {
        kprint("execve: Failed to create vm_map\n");
        error_code = -1;
        goto cleanup;
    }
    if (current_process) {
        current_process->vm_map = new_vm_map;
    }

    // Load the ELF
    char interp_path[256];
    uint32_t interp_len = sizeof(interp_path);
    uint32_t at_base = 0;
    uint32_t main_load_base = elf_exec_main_load_base(image);
    uint32_t at_phdr = elf_runtime_phdr_addr(image, main_load_base);
    uint32_t main_entry = elf_load(file, main_load_base, 1, interp_path, &interp_len);
    if (main_entry == 0) {
        kprint("execve: Failed to load ELF\n");
        goto cleanup;
    }

    uint32_t entry = main_entry;
    if (interp_len == 0 && image && image->ehdr.e_type == 3) {
        /* No PT_INTERP and the main image is ET_DYN.  Two cases:
         *   1. The dynamic linker is being run directly (e.g. ldd execs
         *      /libexec/ld-elf.so.1 with the target binary as argv[1]).
         *   2. A PIE executable that links itself.
         * In both cases AT_BASE must equal the load base of the program
         * the auxv describes — for FreeBSD rtld that's how _rtld_start
         * recovers its own mapbase before any relocation has happened.
         * Without this, init_rtld() reads aux_info[AT_BASE]=0, computes
         * dynamic = 0 + p_vaddr (e.g. 0x1c1d0), and faults. */
        at_base = main_load_base;
    }
    if (interp_len > 0) {
        if (elf_debug_enabled() || cmdline_debug_enabled("perso:linux")) {
            kprint("execve: Loading interpreter: ");
            kprint(interp_path);
            kprint("\n");
        }

        if (is_linux_ldso_path(interp_path) && current_process) {
            /*
             * Hard-wire Linux personality for Linux ld.so PT_INTERP.
             * This keeps syscall ABI and signal semantics aligned with
             * Linux dynamic linker expectations regardless of ELF OSABI.
             */
            current_process->perso_id = PERS_LINUX;
        }

        const char *perso_prefix = NULL;
        if (current_process) {
            struct personality *p = perso_lookup(current_process->perso_id);
            if (p) perso_prefix = p->path_prefix;
        }
        fs_node_t *interp_file = elf_lookup_interpreter(root, interp_path, perso_prefix);
        if (!interp_file) {
            kprint("execve: Interpreter not found\n");
            goto cleanup;
        }

        uint32_t interp_base = 0x40000000;
        uint32_t interp_entry = elf_load(interp_file, interp_base, 0, NULL, NULL);
        if (interp_entry == 0) {
            kprint("execve: Failed to load interpreter\n");
            goto cleanup;
        }

        at_base = interp_base;
        entry = interp_entry;

        if (is_linux_ldso_path(interp_path) && current_process) {
            // Re-assert Linux personality after interpreter load branding.
            current_process->perso_id = PERS_LINUX;
        }
    }


    if (current_process) {
        // Reset signal handlers on successful exec (POSIX requirement)
        exec_reset_signals();

        // Handle setuid/setgid bits (POSIX exec credential change)
        if (file && (file->mask & S_ISUID))
            current_process->euid = file->uid;
        if (file && (file->mask & S_ISGID))
            current_process->egid = file->gid;
        // POSIX: on every exec the saved-set-IDs are set to the
        // (possibly just-changed) effective IDs.  Without this a
        // setuid program that drops privilege with seteuid() could
        // never regain it, and setuid() from the new euid would
        // have no saved-ID to fall back on.
        current_process->suid = current_process->euid;
        current_process->sgid = current_process->egid;

        // Extract basename
        const char *name = path;
        for (const char *p = path; *p; p++) {
            if (*p == '/') name = p + 1;
        }
        strncpy(current_process->comm, name, sizeof(current_process->comm) - 1);
        current_process->comm[sizeof(current_process->comm) - 1] = '\0';
        strncpy(current_process->exec_path, path, sizeof(current_process->exec_path) - 1);
        current_process->exec_path[sizeof(current_process->exec_path) - 1] = '\0';
        proc_capture_cmdline(current_process, k_argv);
        /*
         * Flat native/Linux/FreeBSD ELF images must not retain a prior
         * ELKS/private-LDT execution context across exec.
         */
        ldt_free_process(current_process);
    }

    // Set up kernel stack for this process in TSS
    set_kernel_stack((uint32_t)current_thread->kstack_top);

    /*
     * exec_setup_stack writes user pages in the *new* pmap; on failure we
     * still want to be able to roll back to the old vm_map/pmap, so keep
     * old_vm_map alive across this call.  vm_state_committed only flips
     * once we've passed the point of no return.
     */
    uint32_t sp;
    uint32_t ps_strings_user = 0;
    if (exec_setup_stack(new_pmap, &sp, k_argv, argc, k_envp, envc,
                         main_entry, at_base, at_phdr, image,
                         &ps_strings_user) < 0) {
        goto cleanup;
    }

    /*
     * Past this point we are committed: the image is loaded, the user
     * stack is built, and we will jump to userspace.  Drop the old
     * vm_map *inline* — the success path exits via jump_to_userspace
     * which never reaches the cleanup label, so destroying old_vm_map
     * here is the only place we can do it without leaking ~1 MB per
     * exec.  Null the saved pointer so a post-commit failure that does
     * goto cleanup does not try to double-free.
     */
    if (old_vm_map) {
        /*
         * The old heap (brk) pages live in old_vm_map's pmap, not in a
         * vm_map entry, so vm_map_destroy won't uncharge them.  Release
         * the old image's brk commit reservation here -- elf_load already
         * reset brk/brk_start for the new image (whose heap is empty), so
         * brk_committed still reflects the OLD heap.  Zero it for the new
         * image.
         */
        if (current_process) {
            vm_commit_uncharge(current_process->brk_committed);
            current_process->brk_committed = 0;
        }
        vm_map_destroy(old_vm_map);
        old_vm_map = NULL;
    }
    vm_state_committed = 1;

    char hexbuf[16];
    uint32_t val;

    if (elf_debug_enabled()) {
        kprint("execve: Jumping to userspace, entry=0x");
        val = entry;
        for (int i = 7; i >= 0; i--) {
            int nib = (val >> (i * 4)) & 0xF;
            hexbuf[7 - i] = nib < 10 ? '0' + nib : 'A' + nib - 10;
        }
        hexbuf[8] = '\0';
        kprint(hexbuf);
        kprint(", sp=0x");
        val = sp;
        for (int i = 7; i >= 0; i--) {
            int nib = (val >> (i * 4)) & 0xF;
            hexbuf[7 - i] = nib < 10 ? '0' + nib : 'A' + nib - 10;
        }
        hexbuf[8] = '\0';
        kprint(hexbuf);
        kprint("\n");
    }
    
    if (elf_debug_enabled()) {
        kprint("execve: Final check - entry=0x");
        val = entry;
        for (int i = 7; i >= 0; i--) {
            int nib = (val >> (i * 4)) & 0xF;
            hexbuf[7 - i] = nib < 10 ? '0' + nib : 'A' + nib - 10;
        }
        hexbuf[8] = '\0';
        kprint(hexbuf);
        kprint(", stack=0x");
        val = sp;
        for (int i = 7; i >= 0; i--) {
            int nib = (val >> (i * 4)) & 0xF;
            hexbuf[7 - i] = nib < 10 ? '0' + nib : 'A' + nib - 10;
        }
        hexbuf[8] = '\0';
        kprint(hexbuf);
        kprint("\n");
    }
    
    proc_close_cloexec(current_process);

    // Cleanup kernel arguments
    if (k_argv) kfree(k_argv, (argc + 1) * sizeof(char*));
    if (k_envp) kfree(k_envp, (envc + 1) * sizeof(char*));
    if (arg_buffer) kfree(arg_buffer, ARG_MAX_BYTES);
    if (image) kfree(image, sizeof(*image));
    if (fd >= 0) kern_close(fd);

    // Jump to userspace - does not return.
    // For NetBSD/OpenBSD, _start reads ps_strings from %ebx
    // (lib/csu/arch/i386/crt0.S); other personalities ignore the
    // third arg.  jump_to_userspace zeros %ebx unless we override.
    uint32_t entry_ebx = 0;
    if (current_process &&
        (current_process->perso_id == PERS_NETBSD ||
         current_process->perso_id == PERS_OPENBSD)) {
        entry_ebx = ps_strings_user;
    }

    /* ptrace exec-stop: a traced process parks at the new image's entry point
     * so its tracer (gdb) can plant breakpoints before the program runs a
     * single instruction.  Exec enters via jump_to_userspace() (not an iret),
     * so we hand ptrace_exec_stop() a trapframe describing the entry state for
     * the tracer's GETREGS/SETREGS, then honour any changes it makes. */
    if (current_process && (current_process->p_flag & P_TRACED)) {
        registers_t tf;
        memset(&tf, 0, sizeof(tf));
        tf.eip = entry;
        tf.useresp = sp;
        tf.ebx = entry_ebx;
        tf.cs = 0x1B;                                   /* user code segment */
        tf.ss = tf.ds = tf.es = tf.fs = tf.gs = 0x23;   /* user data segment */
        tf.eflags = 0x202;                              /* reserved bit 1 + IF */
        ptrace_exec_stop(&tf);
        entry = tf.eip;
        sp = tf.useresp;
        entry_ebx = tf.ebx;
    }

    jump_to_userspace(entry, sp, entry_ebx);
    
    // Should never reach here
    panic("jump_to_userspace returned!");
    __builtin_unreachable();

cleanup:
    if (!vm_state_committed && current_process) {
        current_process->perso_id = old_perso_id;
    }
    if (!vm_state_committed && switched_pmap) {
        if (old_pmap) {
            pmap_activate(old_pmap);
        }
        if (current_process) {
            current_process->pmap = (struct pmap *)(uintptr_t)old_pmap;
        }
    }
    if (!vm_state_committed && new_vm_map) {
        /* Restore the old vm_map and destroy the failed-exec one. */
        if (current_process) {
            current_process->vm_map = old_vm_map;
        }
        vm_map_destroy(new_vm_map);
        /* vm_map_destroy() owns and destroys new_vm_map->pmap, which is
         * new_pmap (vm_map_create was handed new_pmap above).  NULL it so the
         * standalone pmap_destroy() below does not destroy the same pmap a
         * second time -- the double free hit the page directory and tripped
         * "vm_phys: free of unallocated page" whenever exec failed *after* the
         * vm_map was built (e.g. a missing PT_INTERP). */
        new_pmap = NULL;
    }
    /* On success, old_vm_map was destroyed inline at commit (and the
     * pointer NULL'd), so we don't need a post-commit branch here.  This frees
     * new_pmap only when exec failed before vm_map_create took ownership. */
    if (!vm_state_committed && new_pmap) {
        pmap_destroy(new_pmap);
    }
    if (k_argv) kfree(k_argv, (argc + 1) * sizeof(char*));
    if (k_envp) kfree(k_envp, (envc + 1) * sizeof(char*));
    if (arg_buffer) kfree(arg_buffer, ARG_MAX_BYTES);
    if (image) kfree(image, sizeof(*image));
    if (fd >= 0) kern_close(fd);
    exec_unpin_current_thread();
    return error_code;
}

// Legacy function for compatibility
int elf_load_file(void *file, uint32_t size) {
    (void)file; (void)size;
    return 0;
}
