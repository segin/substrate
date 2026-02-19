#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <sys/mman.h> // for mmap

// Mock kernel constants/types if not provided by headers
#define __kernel_size_t size_t
#define __kernel_ssize_t ssize_t
#define __kernel_off_t off_t

// Missing definitions for host test
#define AC_COMM_LEN 16
#define BITNESS_32 32
#define BITNESS_64 64

// Mock functions
void kprint(const char *str) {
    printf("[KERNEL] %s", str);
}

void panic(const char *str) {
    printf("PANIC: %s\n", str);
    exit(1);
}

// Mock kmalloc to detect overflow
#define GUARD_SIZE 128
#define GUARD_VAL 0xAA

typedef struct {
    size_t size;
    uint8_t data[];
} mock_alloc_t;

void *kmalloc(size_t size) {
    size_t total_size = sizeof(mock_alloc_t) + size + 2 * GUARD_SIZE;
    mock_alloc_t *p = malloc(total_size);
    if (!p) return NULL;
    p->size = size;
    memset(p->data, GUARD_VAL, GUARD_SIZE);
    memset(p->data + GUARD_SIZE + size, GUARD_VAL, GUARD_SIZE);
    return p->data + GUARD_SIZE;
}

void kfree(void *ptr, size_t size) {
    if (!ptr) return;
    uint8_t *p_data = (uint8_t*)ptr - GUARD_SIZE;
    mock_alloc_t *p = (mock_alloc_t*)((uint8_t*)p_data - offsetof(mock_alloc_t, data));

    // Verify guards
    for (int i = 0; i < GUARD_SIZE; i++) {
        if (p->data[i] != GUARD_VAL) {
            printf("Heap corruption detected (pre-guard)!\n");
            exit(1);
        }
        if (p->data[GUARD_SIZE + p->size + i] != GUARD_VAL) {
            printf("Heap corruption detected (post-guard)!\n");
            exit(1);
        }
    }

    free(p);
}

// Mock copyinstr
// Scenario:
// First pass (len check): returns "short"
// Second pass (copy): returns "longstringthatoverflows"
static int copyinstr_call_count = 0;
static int use_toctou_attack = 0;

int copyinstr(const void *src, void *dst, size_t maxlen, size_t *len) {
    const char *s = (const char *)src;
    // Simulate user changing string between calls
    if (use_toctou_attack && strcmp(s, "attack") == 0) {
        copyinstr_call_count++;
        const char *real_s;
        if (copyinstr_call_count == 1) {
            // First call: return "attack" (len 7)
            real_s = "attack";
        } else {
            // Second call: return "attack_overflow_now" (len 20)
            real_s = "attack_overflow_now";
        }

        size_t slen = strlen(real_s) + 1;

        // Check maxlen constraint
        if (slen > maxlen) {
            if (len) *len = maxlen;
            if (dst) strncpy(dst, real_s, maxlen);
            return -2; // ENAMETOOLONG
        }

        if (dst) strcpy(dst, real_s);
        if (len) *len = slen;
        return 0;
    }

    // Normal behavior
    size_t slen = strlen(s) + 1;
    if (slen > maxlen) {
        if (len) *len = maxlen;
        if (dst) strncpy(dst, s, maxlen);
        return -2;
    }
    if (dst) strcpy(dst, s);
    if (len) *len = slen;
    return 0;
}

int copyin(const void *src, void *dst, size_t size) {
    memcpy(dst, src, size);
    return 0;
}

// Includes from kernel
#include <sys/types.h>
#include <sys/proc.h>
#include <vfs/vfs.h>
#include <vm/vm_map.h> // for vm_map_create declaration
#include <arch/i386/pmap.h> // for pmap_t
#include <exec/formats/elf.h> // For Elf32_Ehdr

// Missing ELF definitions
#define EI_DATA     5
#define EI_VERSION  6
#define ELFDATA2LSB 1
#define EV_CURRENT  1
#define ET_EXEC     2
#define EM_386      3
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

// Global variables required by elf.c
fs_node_t *fs_root = NULL;
process_t mock_process;
process_t *current_process = &mock_process;

// Mock file read for elf_load
// Signature must match fs_node_t read function pointer type
// On host (64-bit), off_t is 64-bit, size_t is 64-bit.
static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    if (offset == 0 && size == sizeof(Elf32_Ehdr)) {
        Elf32_Ehdr *hdr = (Elf32_Ehdr*)buffer;
        memset(hdr, 0, sizeof(*hdr));
        hdr->e_ident[0] = ELFMAG0;
        hdr->e_ident[1] = ELFMAG1;
        hdr->e_ident[2] = ELFMAG2;
        hdr->e_ident[3] = ELFMAG3;
        hdr->e_ident[EI_CLASS] = ELFCLASS32;
        hdr->e_ident[EI_DATA] = ELFDATA2LSB;
        hdr->e_ident[EI_VERSION] = EV_CURRENT;
        hdr->e_type = ET_EXEC;
        hdr->e_machine = EM_386;
        hdr->e_version = EV_CURRENT;
        hdr->e_entry = 0x08048000;
        hdr->e_phoff = sizeof(Elf32_Ehdr);
        hdr->e_phnum = 0; // No segments to simplify
        hdr->e_shentsize = sizeof(Elf32_Shdr);
        hdr->e_phentsize = sizeof(Elf32_Phdr);
        return size;
    }
    // Return zeros for other reads
    memset(buffer, 0, size);
    return size;
}

// vfs_lookup mock
fs_node_t *vfs_lookup(fs_node_t *root, const char *path) {
    static fs_node_t node;
    memset(&node, 0, sizeof(node));
    node.flags = FS_FILE;
    node.read = mock_read;
    return &node;
}

// Stub pmap functions
pmap_t pmap_create(void) { return (pmap_t)0xDEADBEEF; }
void pmap_activate(pmap_t pmap) {}
int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) { return 0; }
void *pmm_alloc_block(void) { return malloc(4096); }
// Mock pmap_kernel
pmap_t pmap_kernel(void) { return (pmap_t)0xCAFEBABE; }

// Stub vm_map
vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max) { return NULL; }
void vm_map_destroy(vm_map_t *map) {}

// Stub random
int random_get_bytes(void *buf, size_t len) { return 0; }

// Stub jump_to_userspace
void jump_to_userspace(uint32_t entry, uint32_t stack) {
    // Verify that copyinstr was called exactly once for the attack string
    if (copyinstr_call_count != 1) {
        printf("FAILED: copyinstr called %d times for attack string, expected 1 (Single Pass)\n", copyinstr_call_count);
        exit(1);
    }
    printf("PASSED: elf_execve succeeded and single-pass verified\n");
    exit(0);
}

// Stub set_kernel_stack
void set_kernel_stack(uint32_t stack) {}

// Include elf.c
#include "../../sys/exec/formats/elf.c"

int main() {
    printf("Running elf_execve TOCTOU test...\n");

    // Allocate low memory for argv/envp/strings
    // Use mmap with MAP_32BIT
    size_t map_size = 4096;
    void *low_mem = mmap(NULL, map_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT, -1, 0);
    if (low_mem == MAP_FAILED) {
        perror("mmap failed");
        // Fallback: try to allocate specific low address?
        low_mem = mmap((void*)0x10000000, map_size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
        if (low_mem == MAP_FAILED) {
             perror("mmap fixed failed");
             printf("Cannot allocate low memory for test. Skipping.\n");
             return 1;
        }
    }

    // printf("Allocated low memory at %p\n", low_mem);

    if (!is_user_ptr(low_mem)) {
        printf("ERROR: Even mmap'd low memory is not considered user ptr! Check is_user_ptr logic.\n");
        return 1;
    }

    // Setup argv/envp in low memory
    // Layout:
    // [char *argv[3]] [char *envp[1]] [string "prog"] [string "attack"]

    char **u_argv = (char**)low_mem;
    char **u_envp = (char**)(u_argv + 3);
    char *s_prog = (char*)(u_envp + 1);
    char *s_attack = s_prog + 16;

    strcpy(s_prog, "prog");
    strcpy(s_attack, "attack");

    u_argv[0] = s_prog;
    u_argv[1] = s_attack;
    u_argv[2] = NULL;

    u_envp[0] = NULL;

    use_toctou_attack = 1;
    copyinstr_call_count = 0;

    // Initialize fs_root
    fs_root = vfs_lookup(NULL, "/");

    int ret = elf_execve("/bin/prog", u_argv, u_envp);

    // If we reach here, it means failure (because success exits in jump_to_userspace)
    if (ret != 0) {
        printf("FAILED: elf_execve failed with code %d\n", ret);
        return 1;
    }

    // Should not happen
    return 1;
}
