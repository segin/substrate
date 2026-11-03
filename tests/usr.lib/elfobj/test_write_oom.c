/*
 * Writing an object must give back everything it allocated when an
 * allocation fails part of the way through: the symbol table, its
 * extended-index table and a shared object's made-up .dynstr were built
 * before being handed to the section list, and a failed hand-over left
 * them to nobody.
 *
 * The test is linked with --wrap=malloc,calloc,realloc,free, counts the
 * blocks alive, and makes each allocation of a write fail in turn.  The
 * number of sections before .symtab and .dynstr is varied so that the
 * section list has to grow exactly when each of them is added.  After a
 * failed write and elf_close() no block the test made may still be alive.
 */
#include <elfobj.h>
#include "elf_private.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t sz);
void *__real_realloc(void *p, size_t n);
void __real_free(void *p);

static long live_blocks;        /* blocks allocated and not yet freed */
static long alloc_calls;        /* allocations seen since arming */
static long fail_at;            /* the allocation to fail, 1-based; 0 never */
static int armed;

/**
 * Count an allocation attempt and say whether this one must fail.
 *
 * @return 1 when the caller is to return NULL without allocating
 */
static int should_fail(void) {
    if (!armed) {
        return 0;
    }
    alloc_calls++;
    return fail_at != 0 && alloc_calls == fail_at;
}

/**
 * malloc as the library sees it: counted, and failed on request.
 *
 * @param n  bytes wanted
 * @return the block, or NULL
 */
void *__wrap_malloc(size_t n) {
    void *p;

    if (should_fail()) {
        return NULL;
    }
    p = __real_malloc(n);
    if (p != NULL) {
        live_blocks++;
    }
    return p;
}

/**
 * calloc as the library sees it: counted, and failed on request.
 *
 * @param n   elements wanted
 * @param sz  size of each
 * @return the zeroed block, or NULL
 */
void *__wrap_calloc(size_t n, size_t sz) {
    void *p;

    if (should_fail()) {
        return NULL;
    }
    p = __real_calloc(n, sz);
    if (p != NULL) {
        live_blocks++;
    }
    return p;
}

/**
 * realloc as the library sees it: a new block when @p p is NULL, else the
 * same block resized; failed on request, leaving @p p alone.
 *
 * @param p  the block to resize, or NULL
 * @param n  bytes wanted
 * @return the block, or NULL
 */
void *__wrap_realloc(void *p, size_t n) {
    void *q;

    if (should_fail()) {
        return NULL;
    }
    q = __real_realloc(p, n);
    if (q != NULL && p == NULL) {
        live_blocks++;
    }
    return q;
}

/**
 * free as the library sees it: counted.
 *
 * @param p  the block, or NULL
 */
void __wrap_free(void *p) {
    if (p != NULL) {
        live_blocks--;
    }
    __real_free(p);
}

/**
 * Make an object with @p extra sections of its own besides .text, and a
 * couple of symbols; a relocatable one also gets a COMDAT group.
 *
 * @param type   ET_REL or ET_DYN
 * @param extra  how many more sections to add
 * @return the object; exits the test on failure
 */
static elfobj_t *make_obj(uint16_t type, int extra) {
    static const unsigned char code[] = {0x90, 0x90, 0xC3};
    elfobj_t *obj;
    elf_section_t *text;
    char name[32];
    int i;

    obj = elf_create(type, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        fprintf(stderr, "test_write_oom: elf_create failed\n");
        exit(1);
    }
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL || elf_section_set_data(text, code, sizeof(code)) != ELF_OK) {
        fprintf(stderr, "test_write_oom: cannot add .text\n");
        exit(1);
    }
    for (i = 0; i < extra; ++i) {
        elf_section_t *s;

        snprintf(name, sizeof(name), ".data.x%d", i);
        s = elf_add_section(obj, name, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE);
        if (s == NULL || elf_section_set_data(s, code, 1) != ELF_OK) {
            fprintf(stderr, "test_write_oom: cannot add %s\n", name);
            exit(1);
        }
    }
    if (type == ET_REL && extra % 2 == 1) {
        elf_section_t *g = elf_add_section(obj, ".text.comdat", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);

        if (g == NULL || elf_section_set_data(g, code, sizeof(code)) != ELF_OK ||
            elf_section_set_group(g, 1, 1) != ELF_OK ||
            elf_section_set_group_signature(g, "comdat_fn") != ELF_OK) {
            fprintf(stderr, "test_write_oom: cannot add the group\n");
            exit(1);
        }
        if (elf_add_symbol(obj, "comdat_fn", 0, 3, STB_WEAK, STT_FUNC) == NULL) {
            fprintf(stderr, "test_write_oom: cannot add comdat_fn\n");
            exit(1);
        }
    }
    if (elf_add_symbol(obj, "fn", 0, 3, STB_GLOBAL, STT_FUNC) == NULL ||
        elf_add_symbol(obj, "ext", 0, 0, STB_GLOBAL, STT_NOTYPE) == NULL) {
        fprintf(stderr, "test_write_oom: cannot add symbols\n");
        exit(1);
    }
    return obj;
}

/**
 * Fail each allocation of writing one object in turn, and require every
 * failed write to leave nothing behind once the object is closed.
 *
 * @param type   ET_REL or ET_DYN
 * @param extra  sections to add besides .text
 * @return the number of leaking failure points
 */
static int check_one(uint16_t type, int extra) {
    int bad = 0;
    long n;

    for (n = 1;; ++n) {
        long before = live_blocks;
        elfobj_t *obj = make_obj(type, extra);
        uint8_t *buf = NULL;
        size_t sz = 0;
        elf_err_t err;

        alloc_calls = 0;
        fail_at = n;
        armed = 1;
        err = elf__write_to_buffer(obj, &buf, &sz);
        armed = 0;
        if (err == ELF_OK) {
            free(buf);
        }
        elf_close(obj);
        if (live_blocks != before) {
            fprintf(stderr, "FAIL: type %u, %d extra sections: failing allocation %ld of the write leaks %ld block(s)\n",
                    (unsigned)type, extra, n, live_blocks - before);
            bad++;
        }
        if (err == ELF_OK && alloc_calls < n) {
            break;      /* every allocation of the write has been failed once */
        }
    }
    return bad;
}

int main(void) {
    int bad = 0;
    int extra;

    for (extra = 0; extra <= 36; ++extra) {
        bad += check_one(ET_REL, extra);
        bad += check_one(ET_DYN, extra);
    }
    if (bad != 0) {
        fprintf(stderr, "test_write_oom: %d leaking failure point(s)\n", bad);
        return 1;
    }
    printf("test_write_oom: ok\n");
    return 0;
}
