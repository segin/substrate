#include "elf.h"
#include "../../drivers/video/vga.h"
#include "../../vfs/vfs.h"
#include "../../kern/console.h"
#include "../../pm/pm.h"
#include <string.h>

// Forward declarations
extern process_t *current_process;
extern fs_node_t *fs_root;
extern struct personality personality_native;
extern struct personality personality_linux;
extern struct personality personality_freebsd;
extern struct personality personality_svr4;

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
uint32_t elf_load(fs_node_t *file) {
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
    
    if (ehdr.e_type != 2) { // ET_EXEC
        kprint("ELF: Not an executable\n");
        return 0;
    }
    
    if (ehdr.e_machine != 3) { // EM_386
        kprint("ELF: Not i386 architecture\n");
        return 0;
    }
    
    kprint("ELF: Loading executable, entry=0x");
    // Print entry point in hex (simple)
    char hexbuf[16];
    uint32_t val = ehdr.e_entry;
    for (int i = 7; i >= 0; i--) {
        int nib = (val >> (i * 4)) & 0xF;
        hexbuf[7 - i] = nib < 10 ? '0' + nib : 'A' + nib - 10;
    }
    hexbuf[8] = '\0';
    kprint(hexbuf);
    kprint("\n");
    
    // Read program headers and load PT_LOAD segments
    Elf32_Phdr phdr;
    
    // Import pmap functions
    extern int pmap_enter(void *pmap, uint32_t va, uint32_t pa, uint32_t prot, uint32_t flags);
    extern void *pmap_kernel(void);
    extern void *pmm_alloc_block(void);
    
    void *pmap = pmap_kernel();
    
    for (int i = 0; i < ehdr.e_phnum; i++) {
        uint32_t ph_offset = ehdr.e_phoff + i * ehdr.e_phentsize;
        if (file->read(file, ph_offset, sizeof(Elf32_Phdr), (uint8_t *)&phdr) != sizeof(Elf32_Phdr)) {
            kprint("ELF: Failed to read program header\n");
            return 0;
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
            uint32_t va_start = phdr.p_vaddr & 0xFFFFF000;
            uint32_t va_end = (phdr.p_vaddr + phdr.p_memsz + 0xFFF) & 0xFFFFF000;
            
            // Allocate and map pages for this segment
            for (uint32_t va = va_start; va < va_end; va += 0x1000) {
                // Allocate physical page
                void *pa = pmm_alloc_block();
                if (!pa) {
                    kprint("ELF: Out of physical memory\n");
                    return 0;
                }
                
                // Map with user access (PTE_U set in pmap_enter)
                if (pmap_enter(pmap, va, (uint32_t)(uintptr_t)pa, 0, 0) < 0) {
                    kprint("ELF: Failed to map page\n");
                    return 0;
                }
                
                // Zero the page first
                memset((void *)va, 0, 0x1000);
            }
            
            // Now copy file data to the mapped pages
            if (phdr.p_filesz > 0) {
                file->read(file, phdr.p_offset, phdr.p_filesz, (uint8_t *)phdr.p_vaddr);
            }
            
            // BSS is already zeroed since we memset each page
        }
    }
    
    // Detect personality based on OSABI
    int detected_os = ELFOSABI_TESTUNIX;
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
                current_process->pers = &personality_freebsd;
                break;
            case ELFOSABI_LINUX:
                current_process->pers = &personality_linux;
                break;
            default:
                current_process->pers = &personality_native;
                break;
        }
    }
    
    return ehdr.e_entry;
}

// Execute a binary - loads ELF and prepares for userspace transition
// Returns 0 on success, -1 on failure
int elf_execve(const char *path, char *const argv[], char *const envp[]) {
    (void)argv; (void)envp; // TODO: Setup argc/argv/envp on stack
    
    if (!fs_root) return -1;
    
    // Lookup the file
    fs_node_t *file = vfs_lookup(fs_root, path);
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
    
    // Load the ELF
    uint32_t entry = elf_load(file);
    if (entry == 0) {
        kprint("execve: Failed to load ELF\n");
        return -1;
    }
    
    // Update process name
    if (current_process) {
        // Extract basename
        const char *name = path;
        for (const char *p = path; *p; p++) {
            if (*p == '/') name = p + 1;
        }
        strncpy(current_process->comm, name, sizeof(current_process->comm) - 1);
    }
    
    // Set up kernel stack for this process in TSS
    extern void set_kernel_stack(uint32_t stack);
    // Use a static kernel stack for now
    static uint8_t kernel_stack[8192] __attribute__((aligned(16)));
    set_kernel_stack((uint32_t)(uintptr_t)(kernel_stack + 8192));
    
    // Allocate and map user stack pages (16KB stack = 4 pages)
    // Stack grows down, so map pages at 0xBFFFC000 - 0xBFFFFFFF
    extern int pmap_enter(void *pmap, uint32_t va, uint32_t pa, uint32_t prot, uint32_t flags);
    extern void *pmap_kernel(void);
    extern void *pmm_alloc_block(void);
    
    void *pmap = pmap_kernel();
    uint32_t user_stack_base = 0xBFFF0000; // Bottom of 64KB stack
    uint32_t user_stack_size = 16; // 16 pages = 64KB
    
    for (uint32_t i = 0; i < user_stack_size; i++) {
        uint32_t va = user_stack_base + i * 0x1000;
        void *pa = pmm_alloc_block();
        if (!pa) {
            kprint("execve: Out of memory for user stack\n");
            return -1;
        }
        if (pmap_enter(pmap, va, (uint32_t)(uintptr_t)pa, 0, 0) < 0) {
            kprint("execve: Failed to map user stack\n");
            return -1;
        }
        memset((void *)va, 0, 0x1000);
    }
    
    // Build argc/argv/envp on user stack
    // User stack is mapped from user_stack_base to user_stack_base + (user_stack_size * 0x1000)
    // That's 0xBFFF0000 to 0xC0000000 (64KB, 16 pages)
    // Valid addresses: 0xBFFF0000 to 0xBFFFFFFF (last byte before 0xC0000000)
    // Start sp at top of valid range and work down
    uint32_t sp = user_stack_base + (user_stack_size * 0x1000); // 0xC0000000
    // This is one byte PAST the last valid address, so back up
    // We'll immediately subtract when we place the first string, so this is OK
    // But to be safe, let's start 16 bytes below to ensure 16-byte alignment  
    sp = (sp - 16) & ~15; // Start 16 bytes below boundary, 16-byte aligned
    
    // 1. Copy argv strings to stack (at the top)
    int argc = 0;
    uint32_t argv_ptrs[32]; // Max 32 args for now
    
    if (argv) {
        for (int i = 0; argv[i] && i < 32; i++) {
            size_t len = strlen(argv[i]) + 1;
            sp -= len;
            sp &= ~3; // Align to 4 bytes
            memcpy((void *)sp, argv[i], len);
            argv_ptrs[argc++] = sp;
        }
    } else {
        // Default: use the path as argv[0]
        size_t len = strlen(path) + 1;
        sp -= len;
        sp &= ~3;
        memcpy((void *)sp, path, len);
        argv_ptrs[argc++] = sp;
    }
    
    // 2. Copy envp strings to stack
    int envc = 0;
    uint32_t envp_ptrs[64]; // Max 64 env vars
    
    if (envp) {
        for (int i = 0; envp[i] && i < 64; i++) {
            size_t len = strlen(envp[i]) + 1;
            sp -= len;
            sp &= ~3;
            memcpy((void *)sp, envp[i], len);
            envp_ptrs[envc++] = sp;
        }
    }
    
    // 3. Align stack to 16 bytes for ABI compliance
    sp &= ~15;
    
    // 4. Build auxiliary vector (for Linux compatibility)
    // Push in reverse order (since stack grows down)
    sp -= 8; *(uint32_t *)sp = 0; *((uint32_t *)sp + 1) = 0; // AT_NULL
    sp -= 8; *(uint32_t *)sp = 6; *((uint32_t *)sp + 1) = 0x1000; // AT_PAGESZ = 4096
    
    // 5. Push envp array (NULL terminated)
    sp -= 4; *(uint32_t *)sp = 0; // envp NULL terminator
    for (int i = envc - 1; i >= 0; i--) {
        sp -= 4; *(uint32_t *)sp = envp_ptrs[i];
    }
    
    // 6. Push argv array (NULL terminated)
    sp -= 4; *(uint32_t *)sp = 0; // argv NULL terminator
    for (int i = argc - 1; i >= 0; i--) {
        sp -= 4; *(uint32_t *)sp = argv_ptrs[i];
    }
    
    // 7. Push argc
    sp -= 4; *(uint32_t *)sp = argc;
    
    // Setup complete, jump to userspace
    extern uint32_t g_user_stack;
    extern uint32_t g_entry_point;
    
    g_user_stack = sp;
    g_entry_point = entry;
    
    extern void jump_to_userspace(void);
    jump_to_userspace();
    
    // Should never reach here
    return 0;
}

// Legacy function for compatibility
// Global variable to pass stack pointer to ISR
// Bypassing stack parameter passing issues
uint32_t g_user_stack = 0;
uint32_t g_entry_point = 0;

int elf_load_file(void *file, uint32_t size) {
    (void)file; (void)size;
    return 0;
}
