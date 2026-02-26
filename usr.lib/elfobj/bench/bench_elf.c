#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double ms_clock(clock_t a, clock_t b) {
    return ((double)(b - a) * 1000.0) / (double)CLOCKS_PER_SEC;
}

static int bench_write_10k_symbols(double *ms_out) {
    elfobj_t *obj;
    elf_section_t *text;
    clock_t t0;
    clock_t t1;
    uint8_t code[] = {0x90, 0x90, 0x90, 0x90, 0xC3};
    int i;

    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) return -1;
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!text) {
        elf_close(obj);
        return -1;
    }
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) {
        elf_close(obj);
        return -1;
    }

    t0 = clock();
    for (i = 0; i < 10000; ++i) {
        char name[48];
        elf_symbol_t *sym;
        (void)snprintf(name, sizeof(name), "bench_sym_%d", i);
        sym = elf_add_symbol(obj, name, (uint64_t)i, 4, STB_GLOBAL, STT_OBJECT);
        if (!sym) {
            elf_close(obj);
            return -1;
        }
        if (elf_symbol_define(sym, text, (uint64_t)(i % sizeof(code))) != ELF_OK) {
            elf_close(obj);
            return -1;
        }
    }
    if (elf_write_file(obj, "tmp_bench_10k.o") != ELF_OK) {
        elf_close(obj);
        return -1;
    }
    t1 = clock();
    *ms_out = ms_clock(t0, t1);
    elf_close(obj);
    return 0;
}

static int bench_link_many_inputs(size_t count, double *ms_out) {
    elfobj_t **inputs;
    elfobj_t *output = NULL;
    clock_t t0;
    clock_t t1;
    size_t i;

    inputs = (elfobj_t **)calloc(count, sizeof(inputs[0]));
    if (inputs == NULL) return -1;

    for (i = 0; i < count; ++i) {
        elfobj_t *obj;
        elf_section_t *text;
        elf_symbol_t *sym;
        uint8_t code[] = {0x90, 0xC3};
        char symname[48];

        obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
        if (obj == NULL) goto fail;
        text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
        if (text == NULL) {
            elf_close(obj);
            goto fail;
        }
        if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) {
            elf_close(obj);
            goto fail;
        }
        (void)snprintf(symname, sizeof(symname), "obj_%lu_sym", (unsigned long)i);
        sym = elf_add_symbol(obj, symname, 0, sizeof(code), STB_GLOBAL, STT_FUNC);
        if (sym == NULL || elf_symbol_define(sym, text, 0) != ELF_OK) {
            elf_close(obj);
            goto fail;
        }
        inputs[i] = obj;
    }

    t0 = clock();
    if (elf_link(inputs, count, &output) != ELF_OK) goto fail;
    t1 = clock();
    *ms_out = ms_clock(t0, t1);

    elf_close(output);
    for (i = 0; i < count; ++i) {
        elf_close(inputs[i]);
    }
    free(inputs);
    return 0;

fail:
    if (output != NULL) {
        elf_close(output);
    }
    for (i = 0; i < count; ++i) {
        if (inputs[i] != NULL) elf_close(inputs[i]);
    }
    free(inputs);
    return -1;
}

static int bench_read_image(const char *path, double *ms_out) {
    elfobj_t *obj = NULL;
    clock_t t0;
    clock_t t1;
    if (path == NULL || path[0] == '\0') return 1;
    t0 = clock();
    if (elf_open(path, &obj) != ELF_OK) return -1;
    t1 = clock();
    *ms_out = ms_clock(t0, t1);
    elf_close(obj);
    return 0;
}

int main(int argc, char **argv) {
    const char *image = getenv("ELFOBJ_BENCH_IMAGE");
    double write_10k_ms = 0.0;
    double link_many_ms = 0.0;
    double read_image_ms = 0.0;
    int read_rc;

    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        image = argv[1];
    }

    if (bench_write_10k_symbols(&write_10k_ms) != 0) {
        fprintf(stderr, "bench: write_10k_symbols failed\n");
        return 1;
    }
    if (bench_link_many_inputs(512, &link_many_ms) != 0) {
        fprintf(stderr, "bench: link_large_archive simulation failed\n");
        return 1;
    }
    read_rc = bench_read_image(image, &read_image_ms);
    if (read_rc < 0) {
        fprintf(stderr, "bench: kernel-image read failed for '%s'\n", image ? image : "");
        return 1;
    }

    printf("BENCH write_10k_symbols_ms=%.3f\n", write_10k_ms);
    printf("BENCH link_large_archive_ms=%.3f\n", link_many_ms);
    if (read_rc == 0) {
        printf("BENCH read_kernel_image_ms=%.3f\n", read_image_ms);
    } else {
        printf("BENCH read_kernel_image_ms=SKIP\n");
    }
    return 0;
}
