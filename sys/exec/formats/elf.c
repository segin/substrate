#include <exec/formats/elf.h>
#include <vfs/vfs.h>
#include <kern/console.h>
#include <sys/sysinfo.h> // For BITNESS_*
#include <sys/proc.h>
#include <pm/pm.h>
#include <kern/panic.h>
#include <string.h>
#include <vm/vm_map.h>
#include <vm/vm_kmem.h>
#include <exec/perso/personality.h>
#include <sys/random.h>
#include <sys/signal.h> // For copyin/copyout
#include <sys/kern_syscalls.h>

/*
 * exec_reset_signals - Reset signal handlers on exec
 *
 * POSIX: On exec(), all signals with handlers are reset to SIG_DFL.
 * Signals set to SIG_IGN remain ignored. Pending signals are cleared.
 * Signal mask is preserved (inherited by new program).
 */
static void exec_reset_signals(void) {
    if (!current_process) return;
    
    for (int sig = 1; sig <= NSIG; sig++) {
        struct sigaction *act = &current_process->sig_actions[sig - 1];
        
        // If handler is a function pointer (caught signal), reset to default
        if (act->sa_handler != SIG_IGN && act->sa_handler != SIG_DFL) {
            act->sa_handler = SIG_DFL;
            act->sa_mask = 0;
            act->sa_flags = 0;
        }
    }
    
    // Clear sig_catch bitmask since all caught signals are now SIG_DFL
    current_process->sig_catch = 0;
    // sig_ignore remains unchanged - ignored signals stay ignored
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
uint32_t elf_load(fs_node_t *file, uint32_t load_base, char *interp_path, uint32_t *interp_len) {

    if (interp_len) *interp_len = 0;

    if (!file || !file->read) {
        kprint("ELF: No file or read function\n");
        return 0;
    }
    
    // Read ELF header
    Elf32_Ehdr ehdr;
    if (file->read(file, 0, sizeof(Elf32_Ehdr), (uint8_t *)&ehdr) != sizeof(Elf32_Ehdr)) {
        kprint("ELF: Failed to read header\n");
        return 0;
    }
    
    if (!elf_check_file(&ehdr)) {
        kprint("ELF: Invalid magic\n");
        return 0;
    }
    
    if (ehdr.e_type != 2 && ehdr.e_type != 3) { // ET_EXEC or ET_DYN
        kprint("ELF: Not an executable or shared object\n");
        return 0;
    }
    
    if (ehdr.e_machine != 3) { // EM_386
        kprint("ELF: Not i386 architecture\n");
        return 0;
    }
    
    kprint("ELF: Loading executable, entry=0x");
    // Print entry point in hex (simple)
    char hexbuf[16];
    uint32_t entry = ehdr.e_entry + load_base;
    uint32_t val = entry;

    for (int i = 7; i >= 0; i--) {
        int nib = (val >> (i * 4)) & 0xF;
        hexbuf[7 - i] = nib < 10 ? '0' + nib : 'A' + nib - 10;
    }
    hexbuf[8] = '\0';
    kprint(hexbuf);
    kprint("\n");
    
    // Read program headers and load PT_LOAD segments
    Elf32_Phdr phdr;
    
    // Use pmap_t from vm_map.h/pmap.h
    extern void *pmm_alloc_block(void);
    
    void *pmap = pmap_kernel();
    if (current_process && current_process->pmap) {
        pmap = (void*)((uintptr_t)current_process->pmap);
    }
    uint32_t max_vaddr = 0;
    
    // TLS segment tracking
    uint32_t tls_vaddr = 0;
    uint32_t tls_filesz = 0;
    uint32_t tls_memsz = 0;
    uint32_t tls_align = 1;
    int has_tls = 0;
    (void)tls_vaddr;  // Will be used for debug output
    (void)tls_align;  // Will be used for proper alignment
    
    for (int i = 0; i < ehdr.e_phnum; i++) {
        uint32_t ph_offset = ehdr.e_phoff + i * ehdr.e_phentsize;
        if (file->read(file, ph_offset, sizeof(Elf32_Phdr), (uint8_t *)&phdr) != sizeof(Elf32_Phdr)) {
            kprint("ELF: Failed to read program header\n");
            return 0;
        }
        
        if (phdr.p_type == PT_INTERP) {
            if (interp_path && interp_len && phdr.p_filesz > 0) {
                uint32_t to_read = (phdr.p_filesz < *interp_len) ? phdr.p_filesz : (*interp_len - 1);
                if (file->read(file, phdr.p_offset, to_read, (uint8_t *)interp_path) == to_read) {
                    interp_path[to_read] = '\0';
                    *interp_len = to_read;
                }
            }
        }
        
        // Detect TLS segment
        if (phdr.p_type == PT_TLS) {
            tls_vaddr = phdr.p_vaddr;
            tls_filesz = phdr.p_filesz;
            tls_memsz = phdr.p_memsz;
            tls_align = phdr.p_align ? phdr.p_align : 1;
            has_tls = 1;
            kprint("ELF: Found TLS segment, memsz=");
            char tbuf[16];
            for (int j = 7; j >= 0; j--) {
                int nib = (tls_memsz >> (j * 4)) & 0xF;
                tbuf[7 - j] = nib < 10 ? '0' + nib : 'A' + nib - 10;
            }
            tbuf[8] = '\0';
            kprint(tbuf);
            kprint("\n");
        }
        
        if (phdr.p_type == PT_LOAD && phdr.p_memsz > 0) {
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
            
            // Calculate page-aligned start and end
            uint32_t vaddr = phdr.p_vaddr + load_base;
            uint32_t va_start = vaddr & 0xFFFFF000;
            uint32_t va_end = (vaddr + phdr.p_memsz + 0xFFF) & 0xFFFFF000;

            
            // Allocate and map pages for this segment
            // Track PA for each VA so we can write to it via kernel mapping
            typedef struct { uint32_t va; void *pa; } page_map_t;
            page_map_t page_maps[256]; // Max 256 pages per segment (1MB)
            int num_pages = 0;
            
            // Determine permissions from ELF segment flags
            int prot = 0;
            if (phdr.p_flags & 0x4) prot |= VM_PROT_READ;    // PF_R
            if (phdr.p_flags & 0x2) prot |= VM_PROT_WRITE;   // PF_W
            if (phdr.p_flags & 0x1) prot |= VM_PROT_EXEC;    // PF_X
            
            for (uint32_t va = va_start; va < va_end; va += 0x1000) {
                // Allocate physical page
                void *pa = pmm_alloc_block();
                if (!pa) {
                    kprint("ELF: Out of physical memory\n");
                    return 0;
                }
                
                // Map with permissions from segment header
                // pmap_enter expects physical address, convert virtual to physical
                uint32_t pa_phys = (uint32_t)(uintptr_t)pa - 0xC0000000;
                if (pmap_enter(pmap, va, pa_phys, prot, 0) < 0) {
                    kprint("ELF: Failed to map page\n");
                    return 0;
                }
                
                // Save mapping for later access via kernel space
                if (num_pages < 256) {
                    page_maps[num_pages].va = va;
                    page_maps[num_pages].pa = pa;
                    num_pages++;
                }
                
                // Zero the page - pa is already virtual from pmm_alloc_block
                memset(pa, 0, 0x1000);
            }
            
            
            // Now copy file data to the mapped segment via kernel space
            // We need to translate user VA to kernel VA via the physical address
            // Simple approach: read directly into kernel buffer then copy page-by-page
            if (phdr.p_filesz > 0) {
                // Allocate temporary buffer in kernel space for segment data
                static uint8_t segment_buffer[1024*1024]; // 1MB max segment size
                if (phdr.p_filesz > sizeof(segment_buffer)) {
                    kprint("ELF: Segment too large\n");
                    return 0;
                }
                
                // Read entire segment into kernel buffer
                if (file->read(file, phdr.p_offset, phdr.p_filesz, segment_buffer) != phdr.p_filesz) {
                    kprint("ELF: Failed to read segment data\n");
                    return 0;
                }
                
                // Copy from kernel buffer to mapped user pages (via kernel addresses)
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
                        
                        // Copy to kernel-mapped page (pa is already virtual)
                        uint8_t *dest = (uint8_t *)page_maps[pi].pa + offset_in_page;
                        memcpy(dest, segment_buffer + offset_in_segment, copy_size);
                        bytes_copied += copy_size;
                    }
                }
            }
            
            // BSS is already zeroed since we memset each page
            
            if (va_end > max_vaddr) max_vaddr = va_end;
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
    int detected_os = ELFOSABI_SUBSTRATE;
    if (ehdr.e_ident[EI_OSABI] == ELFOSABI_FREEBSD) {
        detected_os = ELFOSABI_FREEBSD;
    } else if (ehdr.e_ident[EI_OSABI] == ELFOSABI_LINUX) {
        detected_os = ELFOSABI_LINUX;
    }
    
    kprint("ELF: Personality: ");
    if (detected_os == ELFOSABI_LINUX) kprint("Linux\n");
    else if (detected_os == ELFOSABI_FREEBSD) kprint("FreeBSD\n");
    else kprint("Native\n");
    
    if (current_process) {
        switch (detected_os) {
            case ELFOSABI_FREEBSD:
                proc_set_personality(current_process, PERS_FREEBSD);
                break;
            case ELFOSABI_LINUX:
                proc_set_personality(current_process, PERS_LINUX);
                break;
            default:
                proc_set_personality(current_process, PERS_NATIVE);
                break;
        }
        
        // Set Bitness
        if (ehdr.e_ident[EI_CLASS] == ELFCLASS64) {
             current_process->bitness = BITNESS_64;
        } else {
             current_process->bitness = BITNESS_32;
        }

        
        if (load_base == 0) {
            current_process->brk_start = max_vaddr;
            current_process->brk = max_vaddr;
        }
    }
    
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

static int capture_strlen(const char *s, size_t *out_len) {
    if (is_user_ptr(s)) {
        size_t len;
        int ret = copyinstr(s, NULL, 4096, &len);
        if (ret == 0) {
            *out_len = len;
            return 0;
        }
        return ret;
    } else {
        *out_len = strlen(s) + 1;
        return 0;
    }
}

static int capture_strcpy(char *dst, const char *src) {
    if (is_user_ptr(src)) {
        return copyinstr(src, dst, 4096, NULL);
    } else {
        strcpy(dst, src);
        return 0;
    }
}

// Execute a binary - loads ELF and prepares for userspace transition
// Returns 0 on success, -1 on failure
int elf_execve(const char *path, char *const argv[], char *const envp[]) {
    fs_node_t *root = (current_process && current_process->root_node) ? current_process->root_node : fs_root;
    if (!root) return -1;

    // Variables for argument capturing
    int argc = 0;
    int envc = 0;
    size_t strings_size = 0;
    char **k_argv = NULL;
    char **k_envp = NULL;
    char *arg_buffer = NULL;

    // Lookup the file
    fs_node_t *file = vfs_lookup(root, path);
    if (!file) {
        kprint("execve: File not found: ");
        kprint(path);
        kprint("\n");
        return -1;
    }
    
    if ((file->flags & 0x7) != FS_FILE) {
        kprint("execve: Not a regular file\n");
        return -1;
    }

    // Capture arguments and environment
    // Count argc and size
    if (argv) {
        while (1) {
            char *uarg;
            if (capture_ptr(argv, argc, &uarg) != 0 || uarg == NULL) break;
            
            size_t len;
            int res = capture_strlen(uarg, &len);
            if (res == -14) return -14;
            if (res == -36) return -36;
            
            strings_size += len;
            argc++;
            if (strings_size > 32 * 1024) {
                kprint("execve: Argument list too long\n");
                return -7; // E2BIG
            }
        }
    }

    // Count envc and size
    if (envp) {
        while (1) {
            char *uarg;
            if (capture_ptr(envp, envc, &uarg) != 0 || uarg == NULL) break;
            
            size_t len;
            int res = capture_strlen(uarg, &len);
            if (res == -14) return -14;
            if (res == -36) return -36;
            
            strings_size += len;
            envc++;
            if (strings_size > 32 * 1024) {
                kprint("execve: Argument list too long\n");
                return -7; // E2BIG
            }
        }
    }

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

    if (strings_size > 0) {
        arg_buffer = kmalloc(strings_size);
        if (!arg_buffer) {
             if (k_argv) kfree(k_argv, (argc + 1) * sizeof(char*));
             if (k_envp) kfree(k_envp, (envc + 1) * sizeof(char*));
             return -12;
        }
    }

    // Copy strings
    char *p_buf = arg_buffer;
    for (int i = 0; i < argc; i++) {
        char *uarg;
        capture_ptr(argv, i, &uarg);
        k_argv[i] = p_buf;
        size_t len;
        capture_strlen(uarg, &len);
        capture_strcpy(p_buf, uarg);
        p_buf += len;
    }

    for (int i = 0; i < envc; i++) {
        char *uarg;
        capture_ptr(envp, i, &uarg);
        k_envp[i] = p_buf;
        size_t len;
        capture_strlen(uarg, &len);
        capture_strcpy(p_buf, uarg);
        p_buf += len;
    }
    
    // Create new address space for this process
    extern pmap_t pmap_create(void);
    pmap_t new_pmap = pmap_create();
    if (!new_pmap) {
        kprint("execve: Failed to create pmap\n");
        goto cleanup;
    }

    // Assign to process immediately so elf_load uses it
    if (current_process) {
        current_process->pmap = (struct pmap *)(uintptr_t)new_pmap;
    }
    
    // Reset signal handlers on exec (POSIX requirement)
    exec_reset_signals();

    // Switch to new address space NOW so pmap_enter works (uses recursive mapping of active PD)
    extern void pmap_activate(pmap_t pmap);
    pmap_activate(new_pmap);

    // Load the ELF
    char interp_path[256];
    uint32_t interp_len = sizeof(interp_path);
    uint32_t at_base = 0;
    uint32_t entry = elf_load(file, 0, interp_path, &interp_len);
    if (entry == 0) {
        kprint("execve: Failed to load ELF\n");
        goto cleanup;
    }

    if (interp_len > 0) {
        kprint("execve: Loading interpreter: ");
        kprint(interp_path);
        kprint("\n");
        
        fs_node_t *interp_file = vfs_lookup(fs_root, interp_path);
        if (!interp_file) {
            kprint("execve: Interpreter not found\n");
            goto cleanup;
        }
        
        uint32_t interp_base = 0x40000000;
        uint32_t interp_entry = elf_load(interp_file, interp_base, NULL, NULL);
        if (interp_entry == 0) {
            kprint("execve: Failed to load interpreter\n");
            goto cleanup;
        }
        
        at_base = interp_base;
        entry = interp_entry;
    }

    
    // Update process name
    if (current_process) {
        // Extract basename
        const char *name = path;
        for (const char *p = path; *p; p++) {
            if (*p == '/') name = p + 1;
        }
        strncpy(current_process->comm, name, sizeof(current_process->comm) - 1);
        
        // Initialize VM map
        extern vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max);
        if (current_process->vm_map) {
             vm_map_destroy(current_process->vm_map);
        }
        // Use the proper pmap pointer (already active)
        current_process->vm_map = vm_map_create((pmap_t)(uintptr_t)current_process->pmap, 0, 0xC0000000); 
    }
    
    // Set up kernel stack for this process in TSS
    extern void set_kernel_stack(uint32_t stack);
    static uint8_t kernel_stack[8192] __attribute__((aligned(16)));
    set_kernel_stack((uint32_t)(uintptr_t)(kernel_stack + 8192));
    
    // Allocate and map user stack pages
    extern void *pmm_alloc_block(void);
    
    // pmap is already active, so pmap_enter will use the correct page directory
    pmap_t pmap = new_pmap;
    uint32_t user_stack_base = 0xBFFF0000;
    uint32_t user_stack_size = 16; // 64KB
    
    // Track physical addresses for kernel-space access
    typedef struct { uint32_t va; void *pa; } stack_page_t;
    stack_page_t stack_pages[16];
    
    for (uint32_t i = 0; i < user_stack_size; i++) {
        uint32_t va = user_stack_base + i * 0x1000;
        void *pa = pmm_alloc_block();
        if (!pa) {
            kprint("execve: Out of memory for user stack\n");
            goto cleanup;
        }
        // Map with user access and WRITE permission for stack operations
        // pmap_enter expects physical address, convert virtual to physical
        uint32_t pa_phys = (uint32_t)(uintptr_t)pa - 0xC0000000;
        if (pmap_enter(pmap, va, pa_phys, VM_PROT_WRITE, 0) < 0) {
            kprint("execve: Failed to map user stack\n");
            goto cleanup;
        }
        stack_pages[i].va = va;
        stack_pages[i].pa = pa;
        
        // Zero the page - pa is already virtual from pmm_alloc_block
        memset(pa, 0, 0x1000);
    }
    
    // Build minimal stack: just argc and argv[0] = path
    // Stack grows DOWN, strings are placed at TOP, then pointers below
    // Layout: [strings...] [envp NULL] [argv NULL] [argv[0]] [argc] <- sp
    
    uint32_t sp = 0xBFFFFFFC; // Start at very top (leave 4 bytes margin)
    
    // Helper to write to user stack via kernel mapping
    #define STACK_WRITE32(user_va, val) do { \
        uint32_t page_idx = ((user_va) - user_stack_base) / 0x1000; \
        uint32_t offset = ((user_va) - user_stack_base) % 0x1000; \
        if (page_idx < user_stack_size) { \
            /* stack_pages[].pa is already virtual from pmm_alloc_block */ \
            uint32_t *kptr = (uint32_t*)((uint8_t*)stack_pages[page_idx].pa + offset); \
            *kptr = (val); \
        } \
    } while(0)
    
    // Helper macro to copy string to user stack (simulating multiple pushes)
    // We do this inline to avoid function call overhead/complexity with local vars
    #define PUSH_STRING(str, ptr_out) do { \
        const char *s = (str); \
        size_t len = strlen(s) + 1; \
        sp -= len; \
        sp &= ~3; /* 4-byte align */ \
        ptr_out = sp; \
        for (size_t i = 0; i < len; i++) { \
            uint32_t addr = ptr_out + i; \
            uint32_t page_idx = (addr - user_stack_base) / 0x1000; \
            uint32_t offset = (addr - user_stack_base) % 0x1000; \
            if (page_idx < user_stack_size) { \
                /* stack_pages[].pa is already virtual from pmm_alloc_block */ \
                uint8_t *kptr = (uint8_t*)stack_pages[page_idx].pa + offset; \
                *kptr = s[i]; \
            } \
        } \
    } while(0)

    // Push strings to stack (High -> Low address)
    // Order: envp[last]...envp[0], argv[last]...argv[0]
    
    // Push environment strings
    if (k_envp) {
        for (int i = envc - 1; i >= 0; i--) {
            uint32_t user_ptr;
            PUSH_STRING(k_envp[i], user_ptr);
            // Store user pointer back into k_envp array (reusing storage)
            k_envp[i] = (char*)user_ptr;
        }
    }
    
    // Push argument strings
    if (k_argv) {
        for (int i = argc - 1; i >= 0; i--) {
            uint32_t user_ptr;
            PUSH_STRING(k_argv[i], user_ptr);
            k_argv[i] = (char*)user_ptr;
        }
    }
    
    #undef PUSH_STRING
    
    // now align to 16 bytes BEFORE placing pointers (after all strings)
    sp &= ~15;

    // --- AUXV SETUP ---
    // Read ELF header again to get PHDR info
    Elf32_Ehdr ehdr;
    if (file->read(file, 0, sizeof(Elf32_Ehdr), (uint8_t *)&ehdr) != sizeof(Elf32_Ehdr)) {
        kprint("execve: Failed to re-read header for AUXV\n");
        goto cleanup;
    }
    
    // Calculate AT_PHDR
    // We scan PHDRs to find where the program headers are mapped.
    // Ideally use PT_PHDR, otherwise find PT_LOAD containing e_phoff.
    uint32_t at_phdr = 0;
    Elf32_Phdr phdr_scan;
    
    for (int i = 0; i < ehdr.e_phnum; i++) {
        uint32_t offset = ehdr.e_phoff + i * ehdr.e_phentsize;
        if (file->read(file, offset, sizeof(Elf32_Phdr), (uint8_t *)&phdr_scan) == sizeof(Elf32_Phdr)) {
            if (phdr_scan.p_type == 6) { // PT_PHDR
                at_phdr = phdr_scan.p_vaddr;
                break;
            }
            // Fallback: Check if this PT_LOAD covers the file offset of phdr table
            if (phdr_scan.p_type == PT_LOAD) {
                if (ehdr.e_phoff >= phdr_scan.p_offset && 
                    ehdr.e_phoff < (phdr_scan.p_offset + phdr_scan.p_filesz)) {
                    at_phdr = phdr_scan.p_vaddr + (ehdr.e_phoff - phdr_scan.p_offset);
                }
            }
        }
    }
    
    if (at_phdr == 0) {
        // Fallback to assumption if not found (e.g. headers not mapped?)
        // Standard Linux static binaries usually have headers mapped.
        // If not mapped, musl might fail to init TLS.
        kprint("execve: Warning - Could not determine AT_PHDR\n");
        at_phdr = 0x08048000 + ehdr.e_phoff; // Legacy guess
    }
    
    // Debug: print AT_PHDR value
    kprint("execve: AT_PHDR=0x");
    char phdr_buf[9];
    for (int j = 7; j >= 0; j--) {
        int nib = (at_phdr >> (j * 4)) & 0xF;
        phdr_buf[7 - j] = nib < 10 ? '0' + nib : 'A' + nib - 10;
    }
    phdr_buf[8] = '\0';
    kprint(phdr_buf);
    kprint(" phnum=");
    // Print phnum
    if (ehdr.e_phnum < 10) {
        char c = '0' + ehdr.e_phnum;
        kprint(&c);
    } else {
        kprint("?");
    }
    kprint("\n");
    
    // Push AUXV entries (Key, Value) pairs
    // Stack grows down, so push in reverse order of desired array?
    // Array: [Type1, Val1], [Type2, Val2] ... [NULL, 0]
    // Pushing down: Push [NULL, 0] first (highest address), then others.
    
    // AT_NULL
    sp -= 4; STACK_WRITE32(sp, 0); // Val
    sp -= 4; STACK_WRITE32(sp, AT_NULL); // Type
    
    // AT_ENTRY
    sp -= 4; STACK_WRITE32(sp, entry);
    sp -= 4; STACK_WRITE32(sp, AT_ENTRY);
    
    // AT_PHNUM
    sp -= 4; STACK_WRITE32(sp, ehdr.e_phnum);
    sp -= 4; STACK_WRITE32(sp, AT_PHNUM);
    
    // AT_PHENT
    sp -= 4; STACK_WRITE32(sp, ehdr.e_phentsize);
    sp -= 4; STACK_WRITE32(sp, AT_PHENT);

    // AT_PHDR
    sp -= 4; STACK_WRITE32(sp, at_phdr);
    sp -= 4; STACK_WRITE32(sp, AT_PHDR);
    
    // AT_PAGESZ (4096)
    sp -= 4; STACK_WRITE32(sp, 4096);
    sp -= 4; STACK_WRITE32(sp, AT_PAGESZ);

    // AT_FLAGS (0)
    sp -= 4; STACK_WRITE32(sp, 0);
    sp -= 4; STACK_WRITE32(sp, AT_FLAGS);
    
    // AT_BASE (Interpreter base, 0 for static)
    sp -= 4; STACK_WRITE32(sp, at_base);
    sp -= 4; STACK_WRITE32(sp, AT_BASE);

    // Push 16 bytes of random data for AT_RANDOM
    uint32_t rand_ptr;
    sp -= 16;
    rand_ptr = sp;

    uint8_t rand_buf[16];
    random_get_bytes(rand_buf, sizeof(rand_buf));

    for (int i = 0; i < 4; i++) {
        uint32_t val;
        memcpy(&val, &rand_buf[i * 4], 4);
        STACK_WRITE32(sp + i * 4, val);
    }
    
    // AT_RANDOM
    sp -= 4; STACK_WRITE32(sp, rand_ptr);
    sp -= 4; STACK_WRITE32(sp, AT_RANDOM);

    // AT_SECURE
    sp -= 4; STACK_WRITE32(sp, 0);
    sp -= 4; STACK_WRITE32(sp, AT_SECURE);

    // AT_UID / AT_GID
    sp -= 4; STACK_WRITE32(sp, current_process->uid);
    sp -= 4; STACK_WRITE32(sp, AT_UID);
    sp -= 4; STACK_WRITE32(sp, current_process->gid);
    sp -= 4; STACK_WRITE32(sp, AT_GID);
    sp -= 4; STACK_WRITE32(sp, current_process->euid);
    sp -= 4; STACK_WRITE32(sp, AT_EUID);
    sp -= 4; STACK_WRITE32(sp, current_process->egid);
    sp -= 4; STACK_WRITE32(sp, AT_EGID);

    // Push platform string "i686" for AT_PLATFORM
    const char *platform_str = "i686";
    size_t platform_len = 5; // strlen("i686") + 1
    sp -= platform_len;
    sp &= ~3; // Align to 4 bytes
    uint32_t platform_ptr = sp;
    for (size_t i = 0; i < platform_len; i++) {
        uint32_t addr = platform_ptr + i;
        uint32_t page_idx = (addr - user_stack_base) / 0x1000;
        uint32_t offset = (addr - user_stack_base) % 0x1000;
        if (page_idx < user_stack_size) {
            /* stack_pages[].pa is already virtual from pmm_alloc_block */
            uint8_t *kptr = (uint8_t*)stack_pages[page_idx].pa + offset;
            *kptr = platform_str[i];
        }
    }
    
    // AT_PLATFORM
    sp -= 4; STACK_WRITE32(sp, platform_ptr);
    sp -= 4; STACK_WRITE32(sp, AT_PLATFORM);
    
    // AT_EXECFN - points to the executable path (argv[0])
    // If argc > 0, argv[0] is at k_argv[0] (user address)
    uint32_t execfn_ptr = 0;
    if (k_argv && argc > 0) execfn_ptr = (uint32_t)(uintptr_t)k_argv[0];

    sp -= 4; STACK_WRITE32(sp, execfn_ptr);
    sp -= 4; STACK_WRITE32(sp, AT_EXECFN);
    
    // ------------------
    
    // Build envp array: [envp[0], envp[1], ..., NULL]
    // Stack grows down, so push NULL first (highest address), then pointers in reverse order
    
    // envp[envc] = NULL
    sp -= 4; STACK_WRITE32(sp, 0);
    
    if (k_envp) {
        for (int i = envc - 1; i >= 0; i--) {
            sp -= 4;
            STACK_WRITE32(sp, (uint32_t)(uintptr_t)k_envp[i]);
        }
    }
    
    // Build argv array: [argv[0], ..., NULL]
    
    // argv[argc] = NULL
    sp -= 4; STACK_WRITE32(sp, 0);

    if (k_argv) {
        for (int i = argc - 1; i >= 0; i--) {
            sp -= 4;
            STACK_WRITE32(sp, (uint32_t)(uintptr_t)k_argv[i]);
        }
    }
    
    // Push argc
    sp -= 4; STACK_WRITE32(sp, argc);
    
    kprint("execve: Jumping to userspace, entry=0x");
    char hexbuf[16];
    uint32_t val = entry;
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
    
    // Cleanup kernel arguments
    if (k_argv) kfree(k_argv, (argc + 1) * sizeof(char*));
    if (k_envp) kfree(k_envp, (envc + 1) * sizeof(char*));
    if (arg_buffer) kfree(arg_buffer, strings_size);

    // Jump to userspace - does not return
    extern void jump_to_userspace(uint32_t entry, uint32_t stack);
    jump_to_userspace(entry, sp);
    
    // Should never reach here
    panic("jump_to_userspace returned!");
    return 0;

cleanup:
    if (k_argv) kfree(k_argv, (argc + 1) * sizeof(char*));
    if (k_envp) kfree(k_envp, (envc + 1) * sizeof(char*));
    if (arg_buffer) kfree(arg_buffer, strings_size);
    return -1;
}

// Legacy function for compatibility
int elf_load_file(void *file, uint32_t size) {
    (void)file; (void)size;
    return 0;
}
