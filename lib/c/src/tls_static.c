/*
 * src/tls_static.c — thread-local storage for statically linked programs.
 *
 * A dynamically linked program has its TLS laid out by the dynamic linker,
 * which also hands libpthread a block for each new thread
 * (__ldso_alloc_tls).  A statically linked one has no interpreter, so libc
 * does the same job for the one module there is, the executable: find its
 * PT_TLS segment through the auxiliary vector, build the initial thread's
 * block, and point the thread register at it before any code touches a
 * __thread variable.
 *
 * Layout: variant II on both i386 and amd64 (docs/specs/abi-amd64.md,
 * section 8; ld.so's ld_tls.c lays i386 out the same way).  The TLS image
 * sits immediately below the thread pointer, at tp - round_up(memsz, align);
 * tp[0] is the thread pointer itself (%gs:0 on i386, %fs:0 on amd64) and
 * tp[1] the DTV slot, unused here because a static link resolves every
 * access to a fixed offset.
 *
 * This used to be amd64 only, on the belief that every i386 program is
 * dynamically linked.  A static i386 program then had no TLS image and its
 * threads a NULL TLS base: libpthread's own TLS faulted and the program's
 * __thread variables were never initialised.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <elf.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <tls_local.h>

#define TLS_AT_NULL   0
#define TLS_AT_PHDR   3
#define TLS_AT_PHENT  4
#define TLS_AT_PHNUM  5
#define TLS_TCB_SIZE  64        /* self pointer, DTV slot, spare */

#if defined(__x86_64__)
typedef Elf64_Phdr tls_phdr_t;
#else
typedef Elf32_Phdr tls_phdr_t;
#endif

extern void *__ldso_alloc_tls(void) __attribute__((weak));

static const void *tls_image;       /* initialised part of the template */
static size_t tls_filesz;
static size_t tls_memsz;
static size_t tls_align = 1;

/* The distance from the start of the image to the thread pointer: the
 * segment's size rounded up to ITS alignment, which is what the link
 * editor used to compute every variable's offset from %fs. */
static size_t tls_block_size(void) {
    return (tls_memsz + tls_align - 1) & ~(tls_align - 1);
}

/* Fill `mem`, of tls_block_size() + TLS_TCB_SIZE bytes, and return the
 * thread pointer inside it. */
static void *tls_build(void *mem) {
    size_t bsz = tls_block_size();
    uintptr_t *tp = (uintptr_t *)((char *)mem + bsz);

    memset(mem, 0, bsz + TLS_TCB_SIZE);
    if (tls_filesz) memcpy((char *)tp - bsz, tls_image, tls_filesz);
    tp[0] = (uintptr_t)tp;
    return tp;
}

/*
 * A TLS block for a new thread, or NULL when the program has no TLS (or
 * memory is short).  libpthread calls this when there is no dynamic linker
 * to ask.
 */
void *__libc_alloc_tls(void) {
    if (tls_memsz == 0) return NULL;
    size_t total = tls_block_size() + TLS_TCB_SIZE;
    void *mem = NULL;

    /* The block must honour the segment's alignment at tp - block size,
     * i.e. at its start. */
    if (posix_memalign(&mem, tls_align < 16 ? 16 : tls_align, total) != 0)
        return NULL;
    return tls_build(mem);
}

/*
 * Called first thing in __stdio_init, which crt0 calls (and which is also
 * libc's constructor), with the environment pointer the auxiliary vector
 * follows.  amd64's crt0 calls it as well, so it runs only once.
 *
 * i386's crt0 does not call it: a program linked with a crt0 that named
 * this function could not start against a libc.so.0 from before it
 * existed.  Reached through __stdio_init, a newly linked program needs
 * nothing an older libc lacks, and a static one still links it in.
 */
void __libc_static_tls_init(char **envp) {
    static int done;

    if (done) return;
    done = 1;
    if (__ldso_alloc_tls) return;       /* the dynamic linker did it */
    if (!envp) return;

    char **p = envp;
    while (*p) p++;
    const uintptr_t *aux = (const uintptr_t *)(p + 1);
    const tls_phdr_t *phdr = NULL;
    size_t phnum = 0, phent = sizeof(tls_phdr_t);

    for (; aux[0] != TLS_AT_NULL; aux += 2) {
        if (aux[0] == TLS_AT_PHDR) phdr = (const tls_phdr_t *)aux[1];
        else if (aux[0] == TLS_AT_PHNUM) phnum = aux[1];
        else if (aux[0] == TLS_AT_PHENT) phent = aux[1];
    }
    if (!phdr) return;

    for (size_t i = 0; i < phnum; i++) {
        const tls_phdr_t *ph =
            (const tls_phdr_t *)((const char *)phdr + i * phent);
        if (ph->p_type != PT_TLS) continue;
        tls_image = (const void *)(uintptr_t)ph->p_vaddr;
        tls_filesz = ph->p_filesz;
        tls_memsz = ph->p_memsz;
        if (ph->p_align > tls_align) tls_align = ph->p_align;
        break;
    }
    if (tls_memsz == 0) return;

    /* The initial thread's block comes straight from the kernel: malloc is
     * not set up yet, and this block lives as long as the process. */
    size_t total = tls_block_size() + TLS_TCB_SIZE;
    void *mem = mmap(NULL, total, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) return;
    syscall(SYS_SET_GSBASE, (uintptr_t)tls_build(mem));
}
