#include "elf.h"
#include "../../drivers/video/vga.h"
#include "../../vfs/vfs.h"
#include "../../kern/console.h"
#include "../../pm/pm.h"
#include "../../kern/panic.h"
#include <string.h>
#include "../../vm/vm_map.h"

// Forward declarations
extern process_t *current_process;
extern fs_node_t *fs_root;
extern struct personality personality_native;
extern struct personality personality_linux;
extern struct personality personality_freebsd;
extern struct personality personality_svr4;

// Globals for userspace transition

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
    
    // Use pmap_t from vm_map.h/pmap.h
    extern void *pmm_alloc_block(void);
    
    void *pmap = pmap_kernel();
    uint32_t max_vaddr = 0;
    
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
            // Track PA for each VA so we can write to it via kernel mapping
            typedef struct { uint32_t va; void *pa; } page_map_t;
            page_map_t page_maps[256]; // Max 256 pages per segment (1MB)
            int num_pages = 0;
            
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
                
                // Save mapping for later access via kernel space
                if (num_pages < 256) {
                    page_maps[num_pages].va = va;
                    page_maps[num_pages].pa = pa;
                    num_pages++;
                }
                
                // Zero the page using PA mapped to kernel space
                #define VIRTUAL_d(x)  ((void*)(uintptr_t)((uint32_t)(x) + 0xC0000000))
                memset(VIRTUAL_d(pa), 0, 0x1000);
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
                    uint32_t segment_va = phdr.p_vaddr;
                    
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
                        
                        // Copy to kernel-mapped page
                        uint8_t *dest = (uint8_t *)VIRTUAL_d(page_maps[pi].pa) + offset_in_page;
                        memcpy(dest, segment_buffer + offset_in_segment, copy_size);
                        bytes_copied += copy_size;
                    }
                }
            }
            
            // BSS is already zeroed since we memset each page
            
            if (va_end > max_vaddr) max_vaddr = va_end;
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
        
        current_process->brk_start = max_vaddr;
        current_process->brk = max_vaddr;
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
        
        // Initialize VM map
        extern vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max);
        if (current_process->vm_map) {
             // TODO: kfree old map
        }
        current_process->vm_map = vm_map_create(pmap_kernel(), 0, 0xC0000000);
    }
    
    // Set up kernel stack for this process in TSS
    extern void set_kernel_stack(uint32_t stack);
    static uint8_t kernel_stack[8192] __attribute__((aligned(16)));
    set_kernel_stack((uint32_t)(uintptr_t)(kernel_stack + 8192));
    
    // Allocate and map user stack pages
    extern void *pmm_alloc_block(void);
    
    void *pmap = pmap_kernel();
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
            return -1;
        }
        // Map with user access and WRITE permission for stack operations
        if (pmap_enter(pmap, va, (uint32_t)(uintptr_t)pa, VM_PROT_WRITE, 0) < 0) {
            kprint("execve: Failed to map user stack\n");
            return -1;
        }
        stack_pages[i].va = va;
        stack_pages[i].pa = pa;
        
        // Zero via kernel mapping
        #define VIRTUAL_d(x)  ((void*)(uintptr_t)((uint32_t)(x) + 0xC0000000))
        memset(VIRTUAL_d(pa), 0, 0x1000);
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
            uint32_t *kptr = (uint32_t*)((uint8_t*)VIRTUAL_d(stack_pages[page_idx].pa) + offset); \
            *kptr = (val); \
        } \
    } while(0)
    
    // Copy argv[0] string to stack (use actual path, not "sh")
    const char *argv0_str = path;
    size_t argv0_len = strlen(argv0_str) + 1;
    sp -= argv0_len;
    sp &= ~3; // 4-byte align
    uint32_t argv0_ptr = sp;
    
    // Copy string byte by byte via kernel mapping  
    for (size_t i = 0; i < argv0_len; i++) {
        uint32_t addr = argv0_ptr + i;
        uint32_t page_idx = (addr - user_stack_base) / 0x1000;
        uint32_t offset = (addr - user_stack_base) % 0x1000;
        if (page_idx < user_stack_size) {
            uint8_t *kptr = (uint8_t*)VIRTUAL_d(stack_pages[page_idx].pa) + offset;
            *kptr = argv0_str[i];
        }
    }
    
    // Now align to 16 bytes BEFORE placing pointers (after all strings)
    sp &= ~15;
    
    // Build envp array (just NULL)
    sp -= 4; STACK_WRITE32(sp, 0);           // envp[0] = NULL
    
    // Build argv array
    sp -= 4; STACK_WRITE32(sp, 0);           // argv[1] = NULL (terminator)
    sp -= 4; STACK_WRITE32(sp, argv0_ptr);   // argv[0] = pointer to string
    
    // Push argc
    sp -= 4; STACK_WRITE32(sp, 1);           // argc = 1
    
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
    
    // Jump to userspace - does not return
    extern void jump_to_userspace(uint32_t entry, uint32_t stack);
    jump_to_userspace(entry, sp);
    
    // Should never reach here
    panic("jump_to_userspace returned!");
    return 0;
}

// Legacy function for compatibility
int elf_load_file(void *file, uint32_t size) {
    (void)file; (void)size;
    return 0;
}
