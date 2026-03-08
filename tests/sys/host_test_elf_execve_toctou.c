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
#include <time.h>

// Mock kernel constants/types if not provided by headers
#define __kernel_size_t size_t
#define __kernel_ssize_t ssize_t
#define __kernel_off_t off_t

// Missing definitions for host test
#define AC_COMM_LEN 16
#define ELF_TEST_BITNESS_32 32
#define ELF_TEST_BITNESS_64 64

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
static int mock_header_reads = 0;
static int mock_phdr_reads = 0;
static uint8_t mock_osabi = 0;

#define MAX_HOST_MAPPINGS 1024
typedef struct {
    uint32_t va;
    uint8_t *page;
} host_mapping_t;

static host_mapping_t host_mappings[MAX_HOST_MAPPINGS];
static size_t host_mapping_count = 0;
static uintptr_t mock_pmm_next = 0xC1000000u;

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
thread_t mock_thread;
thread_t *current_thread = &mock_thread;

static void reset_env(void) {
    memset(&mock_process, 0, sizeof(mock_process));
    memset(&mock_thread, 0, sizeof(mock_thread));
    current_process = &mock_process;
    current_thread = &mock_thread;
    current_process->pid = 42;
    current_process->uid = 1000;
    current_process->gid = 100;
    current_process->euid = 1000;
    current_process->egid = 100;
    current_thread->kstack_top = 0xCAFED000u;
}

// Mock file read for elf_load
// Signature must match fs_node_t read function pointer type
// On host (64-bit), off_t is 64-bit, size_t is 64-bit.
static size_t mock_read(fs_node_t *node, off_t offset, size_t size, uint8_t *buffer) {
    (void)node;
    if (offset == 0 && size == sizeof(Elf32_Ehdr)) {
        mock_header_reads++;
        Elf32_Ehdr *hdr = (Elf32_Ehdr*)buffer;
        memset(hdr, 0, sizeof(*hdr));
        hdr->e_ident[0] = ELFMAG0;
        hdr->e_ident[1] = ELFMAG1;
        hdr->e_ident[2] = ELFMAG2;
        hdr->e_ident[3] = ELFMAG3;
        hdr->e_ident[EI_CLASS] = ELFCLASS32;
        hdr->e_ident[EI_DATA] = ELFDATA2LSB;
        hdr->e_ident[EI_VERSION] = EV_CURRENT;
        hdr->e_ident[EI_OSABI] = mock_osabi;
        hdr->e_type = ET_EXEC;
        hdr->e_machine = EM_386;
        hdr->e_version = EV_CURRENT;
        hdr->e_entry = 0x08048000;
        hdr->e_phoff = sizeof(Elf32_Ehdr);
        hdr->e_phnum = 1;
        hdr->e_shentsize = sizeof(Elf32_Shdr);
        hdr->e_phentsize = sizeof(Elf32_Phdr);
        return size;
    }
    if (offset == sizeof(Elf32_Ehdr) && size == sizeof(Elf32_Phdr)) {
        Elf32_Phdr *phdr = (Elf32_Phdr *)buffer;
        memset(phdr, 0, sizeof(*phdr));
        phdr->p_type = PT_LOAD;
        phdr->p_offset = 0;
        phdr->p_vaddr = 0x08048000;
        phdr->p_filesz = 0;
        phdr->p_memsz = 0;
        phdr->p_flags = 0x5;
        mock_phdr_reads++;
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
int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags) {
    (void)pmap;
    (void)prot;
    (void)flags;
    assert(host_mapping_count < MAX_HOST_MAPPINGS);
    host_mappings[host_mapping_count].va = (uint32_t)va & ~0xFFFu;
    host_mappings[host_mapping_count].page = (uint8_t *)(uintptr_t)(pa + 0xC0000000u);
    host_mapping_count++;
    return 0;
}
void *pmm_alloc_block(void) {
    void *addr = (void *)mock_pmm_next;
    void *mapped = mmap(addr, 4096, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    assert(mapped == addr);
    mock_pmm_next += 0x1000u;
    return mapped;
}
// Mock pmap_kernel
pmap_t pmap_kernel(void) { return (pmap_t)0xCAFEBABE; }

// Stub vm_map
vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max) { return NULL; }
void vm_map_destroy(vm_map_t *map) {}

// Stub random
int random_get_bytes(void *buf, size_t len) { return 0; }
int random_get_bytes_flags(void *buf, size_t len, unsigned int flags) {
    (void)flags;
    memset(buf, 0, len);
    return 0;
}

int kern_close(int fd) {
    (void)fd;
    return 0;
}

void exec_unpin_current_thread(void) {}

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

static void reset_elf_cache_state(void) {
    memset(elf_image_cache, 0, sizeof(elf_image_cache));
    elf_image_cache_hand = 0;
    mock_header_reads = 0;
    mock_phdr_reads = 0;
    mock_osabi = ELFOSABI_SUBSTRATE;
}

static void reset_host_mappings(void) {
    host_mapping_count = 0;
    mock_pmm_next = 0xC1000000u;
}

static uint8_t *host_user_ptr(uint32_t va) {
    uint32_t page_va = va & ~0xFFFu;
    uint32_t offset = va & 0xFFFu;
    for (size_t i = 0; i < host_mapping_count; i++) {
        if (host_mappings[i].va == page_va) {
            return host_mappings[i].page + offset;
        }
    }
    return NULL;
}

static uint32_t host_user_read32(uint32_t va) {
    uint8_t *ptr = host_user_ptr(va);
    uint32_t val = 0;
    assert(ptr != NULL);
    memcpy(&val, ptr, sizeof(val));
    return val;
}

static void host_user_read_cstr(uint32_t va, char *out, size_t out_size) {
    size_t i;
    for (i = 0; i + 1 < out_size; i++) {
        uint8_t *ptr = host_user_ptr(va + (uint32_t)i);
        assert(ptr != NULL);
        out[i] = (char)*ptr;
        if (out[i] == '\0') {
            break;
        }
    }
    out[(i + 1 < out_size) ? i : (out_size - 1)] = '\0';
}

static uint32_t host_find_auxv_value(uint32_t sp, uint32_t key) {
    uint32_t argc = host_user_read32(sp);
    uint32_t cursor = sp + 4;

    cursor += (argc + 1) * 4; /* argv pointers + NULL */
    while (host_user_read32(cursor) != 0) {
        cursor += 4;
    }
    cursor += 4; /* envp NULL */

    for (;;) {
        uint32_t type = host_user_read32(cursor);
        uint32_t value = host_user_read32(cursor + 4);
        if (type == AT_NULL) {
            break;
        }
        if (type == key) {
            return value;
        }
        cursor += 8;
    }

    return 0;
}

static void test_exec_reset_signals(void) {
    memset(&mock_process, 0, sizeof(mock_process));
    current_process = &mock_process;

    current_process->sig_actions[SIGUSR1 - 1].sa_handler = (sig_t)0x12345678;
    current_process->sig_actions[SIGUSR1 - 1].sa_mask = sigmask(SIGCHLD);
    current_process->sig_actions[SIGUSR1 - 1].sa_flags = SA_RESTART | SA_SIGINFO;
    current_process->sig_actions[SIGUSR2 - 1].sa_handler = SIG_IGN;
    current_process->sig_ignore = sigmask(SIGUSR2);
    current_process->sig_catch = sigmask(SIGUSR1);

    exec_reset_signals();

    assert(current_process->sig_actions[SIGUSR1 - 1].sa_handler == SIG_DFL);
    assert(current_process->sig_actions[SIGUSR1 - 1].sa_mask == 0);
    assert(current_process->sig_actions[SIGUSR1 - 1].sa_flags == 0);
    assert(current_process->sig_actions[SIGUSR2 - 1].sa_handler == SIG_IGN);
    assert(current_process->sig_ignore == sigmask(SIGUSR2));
    assert(current_process->sig_catch == 0);
}

static void test_elf_cache_uses_mount_inode_identity(void) {
    elf_image_info_t image;
    fs_node_t file_a;
    fs_node_t file_b;

    memset(&file_a, 0, sizeof(file_a));
    memset(&file_b, 0, sizeof(file_b));

    file_a.flags = FS_FILE;
    file_a.read = mock_read;
    file_a.inode = 1234;
    file_a.mp = (struct mount *)0x11110000;
    file_a.length = 8192;
    file_a.mtime = 10;
    file_a.ctime = 20;

    file_b = file_a;
    strcpy(file_a.name, "busybox");
    strcpy(file_b.name, "sh");

    reset_elf_cache_state();

    assert(elf_get_image_info(&file_a, &image) == 0);
    assert(mock_header_reads == 1);
    assert(mock_phdr_reads == 1);

    assert(elf_get_image_info(&file_b, &image) == 0);
    assert(mock_header_reads == 1);
    assert(mock_phdr_reads == 1);
}

static void test_elf_cache_invalidates_on_metadata_change(void) {
    elf_image_info_t image;
    fs_node_t file;

    memset(&file, 0, sizeof(file));
    file.flags = FS_FILE;
    file.read = mock_read;
    file.inode = 4321;
    file.mp = (struct mount *)0x22220000;
    file.length = 4096;
    file.mtime = 30;
    file.ctime = 40;

    reset_elf_cache_state();

    assert(elf_get_image_info(&file, &image) == 0);
    assert(mock_header_reads == 1);
    assert(mock_phdr_reads == 1);

    file.mtime++;
    assert(elf_get_image_info(&file, &image) == 0);
    assert(mock_header_reads == 2);
    assert(mock_phdr_reads == 2);
}

static void test_hot_cache_preserves_personality_detection(void) {
    elf_image_info_t image;
    fs_node_t file;

    reset_elf_cache_state();
    memset(&file, 0, sizeof(file));

    file.flags = FS_FILE;
    file.read = mock_read;
    file.inode = 5678;
    file.mp = (struct mount *)0x33330000;
    file.length = 16384;
    file.mtime = 90;
    file.ctime = 91;

    mock_osabi = ELFOSABI_LINUX;
    assert(elf_get_image_info(&file, &image) == 0);
    assert(image.detected_os == ELFOSABI_LINUX);
    assert(mock_header_reads == 1);
    assert(mock_phdr_reads == 1);

    memset(&image, 0, sizeof(image));
    assert(elf_get_image_info(&file, &image) == 0);
    assert(image.detected_os == ELFOSABI_LINUX);
    assert(mock_header_reads == 1);
    assert(mock_phdr_reads == 1);
}

static void test_exec_setup_stack_uses_call_specific_execfn(void) {
    elf_image_info_t image;
    char arg0_a[] = "busybox";
    char arg0_b[] = "sh";
    char *argv_a[] = { arg0_a, NULL };
    char *argv_b[] = { arg0_b, NULL };
    char execfn[32];
    uint32_t sp;
    uint32_t execfn_ptr;

    reset_env();
    memset(&image, 0, sizeof(image));
    image.ehdr.e_phnum = 1;
    image.ehdr.e_phentsize = sizeof(Elf32_Phdr);
    image.at_phdr = 0x08048034u;
    current_process->uid = 1000;
    current_process->gid = 100;
    current_process->euid = 1000;
    current_process->egid = 100;

    reset_host_mappings();
    assert(exec_setup_stack((pmap_t)0xDEADBEEF, &sp, argv_a, 1, NULL, 0,
                            0x08048000u, 0, &image) == 0);
    execfn_ptr = host_find_auxv_value(sp, AT_EXECFN);
    assert(execfn_ptr != 0);
    host_user_read_cstr(execfn_ptr, execfn, sizeof(execfn));
    assert(strcmp(execfn, "busybox") == 0);

    reset_host_mappings();
    assert(exec_setup_stack((pmap_t)0xDEADBEEF, &sp, argv_b, 1, NULL, 0,
                            0x08048000u, 0, &image) == 0);
    execfn_ptr = host_find_auxv_value(sp, AT_EXECFN);
    assert(execfn_ptr != 0);
    host_user_read_cstr(execfn_ptr, execfn, sizeof(execfn));
    assert(strcmp(execfn, "sh") == 0);
}

static void test_hot_cache_removes_repeat_metadata_reads(void) {
    elf_image_info_t image;
    fs_node_t file;
    struct timespec cold_start, cold_end, hot_start, hot_end;
    long cold_ns;
    long hot_ns;

    memset(&file, 0, sizeof(file));
    file.flags = FS_FILE;
    file.read = mock_read;
    file.inode = 9012;
    file.mp = (struct mount *)0x44440000;
    file.length = 4096;
    file.mtime = 12;
    file.ctime = 34;

    reset_elf_cache_state();

    clock_gettime(CLOCK_MONOTONIC, &cold_start);
    assert(elf_get_image_info(&file, &image) == 0);
    clock_gettime(CLOCK_MONOTONIC, &cold_end);

    assert(mock_header_reads == 1);
    assert(mock_phdr_reads == 1);

    clock_gettime(CLOCK_MONOTONIC, &hot_start);
    for (int i = 0; i < 1000; i++) {
        assert(elf_get_image_info(&file, &image) == 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &hot_end);

    assert(mock_header_reads == 1);
    assert(mock_phdr_reads == 1);

    cold_ns = (cold_end.tv_sec - cold_start.tv_sec) * 1000000000L +
              (cold_end.tv_nsec - cold_start.tv_nsec);
    hot_ns = (hot_end.tv_sec - hot_start.tv_sec) * 1000000000L +
             (hot_end.tv_nsec - hot_start.tv_nsec);
    assert(cold_ns >= 0);
    assert(hot_ns >= 0);
}

int main() {
    printf("Running elf_execve TOCTOU test...\n");
    test_exec_reset_signals();
    test_elf_cache_uses_mount_inode_identity();
    test_elf_cache_invalidates_on_metadata_change();
    test_hot_cache_preserves_personality_detection();
    test_exec_setup_stack_uses_call_specific_execfn();
    test_hot_cache_removes_repeat_metadata_reads();

#if !defined(__i386__)
    printf("Skipping execve TOCTOU path on non-i386 host build; exec_reset_signals verified.\n");
    return 0;
#endif

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

    int ret = elf_execve(-1, "/bin/prog", u_argv, u_envp);

    // If we reach here, it means failure (because success exits in jump_to_userspace)
    if (ret != 0) {
        printf("FAILED: elf_execve failed with code %d\n", ret);
        return 1;
    }

    // Should not happen
    return 1;
}
