/*
 * ld_map.c -- the link map and the --reproduce bundle.
 */

#include "ld.h"

static int ensure_dir_exists(const char *path) {
    struct stat st;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    if (stat(path, &st) == 0) {
        if (!S_ISDIR(st.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        return 0;
    }
    if (errno != ENOENT) {
        return -1;
    }
    if (mkdir(path, 0755) != 0) {
        return -1;
    }
    return 0;
}

static int copy_file_bytes(const char *src, const char *dst) {
    unsigned char *buf = NULL;
    size_t sz = 0;
    FILE *fp;

    if (src == NULL || dst == NULL) {
        return -1;
    }
    if (read_file(src, &buf, &sz) != 0) {
        return -1;
    }
    fp = fopen(dst, "wb");
    if (fp == NULL) {
        free(buf);
        return -1;
    }
    if (sz != 0 && fwrite(buf, 1, sz, fp) != sz) {
        fclose(fp);
        free(buf);
        return -1;
    }
    fclose(fp);
    free(buf);
    return 0;
}

int write_reproduce_bundle(const ld_ctx_t *ctx, const objvec_t *inputs) {
    char manifest_path[1024];
    char script_path[1024];
    char script_copy[1024];
    FILE *mf = NULL;
    FILE *sf = NULL;
    size_t i;

    if (ctx == NULL || inputs == NULL || ctx->reproduce_path == NULL || ctx->reproduce_path[0] == '\0') {
        return 0;
    }
    if (ensure_dir_exists(ctx->reproduce_path) != 0) {
        fprintf(stderr, "ld: failed to create --reproduce directory %s: %s\n", ctx->reproduce_path, strerror(errno));
        return -1;
    }
    snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.txt", ctx->reproduce_path);
    snprintf(script_path, sizeof(script_path), "%s/repro.sh", ctx->reproduce_path);
    mf = fopen(manifest_path, "w");
    if (mf == NULL) {
        fprintf(stderr, "ld: failed to write --reproduce manifest %s: %s\n", manifest_path, strerror(errno));
        return -1;
    }
    fprintf(mf, "mode=%s\n", ctx->mode == 64 ? "x86_64" : "i386");
    fprintf(mf, "type=%u\n", (unsigned)ctx->expect_type);
    if (ctx->entry_symbol != NULL) {
        fprintf(mf, "entry=%s\n", ctx->entry_symbol);
    }
    if (ctx->script_path != NULL) {
        fprintf(mf, "script=%s\n", ctx->script_path);
    }
    if (ctx->plugin_path != NULL) {
        fprintf(mf, "plugin=%s\n", ctx->plugin_path);
    }
    for (i = 0; i < inputs->count; ++i) {
        fprintf(mf, "input[%zu]=%s\n", i, inputs->names[i] != NULL ? inputs->names[i] : "<unknown>");
    }
    fclose(mf);

    for (i = 0; i < inputs->count; ++i) {
        char obj_path[1024];
        snprintf(obj_path, sizeof(obj_path), "%s/input_%03zu.o", ctx->reproduce_path, i);
        if (elf_write_file(inputs->objs[i], obj_path) != ELF_OK) {
            fprintf(stderr, "ld: failed to write --reproduce object %s\n", obj_path);
            return -1;
        }
    }
    if (ctx->script_path != NULL && ctx->script_path[0] != '\0') {
        snprintf(script_copy, sizeof(script_copy), "%s/linker_script.ld", ctx->reproduce_path);
        if (copy_file_bytes(ctx->script_path, script_copy) != 0) {
            fprintf(stderr, "ld: failed to copy linker script into --reproduce bundle\n");
            return -1;
        }
    }

    sf = fopen(script_path, "w");
    if (sf == NULL) {
        fprintf(stderr, "ld: failed to write --reproduce script %s: %s\n", script_path, strerror(errno));
        return -1;
    }
    fprintf(sf, "#!/bin/sh\nset -eu\n");
    fprintf(sf, "DIR=$(CDPATH= cd -- \"$(dirname -- \"$0\")\" && pwd)\n");
    fprintf(sf, "LD_TOOL=${LD_TOOL:-ld}\n");
    fprintf(sf, "exec \"$LD_TOOL\" -m%s ", ctx->mode == 64 ? "64" : "32");
    if (ctx->expect_type == ET_REL) {
        fprintf(sf, "-r ");
    } else if (ctx->expect_type == ET_DYN) {
        fprintf(sf, "-shared ");
    }
    if (ctx->entry_symbol != NULL && ctx->entry_symbol[0] != '\0') {
        fprintf(sf, "-e '%s' ", ctx->entry_symbol);
    }
    if (ctx->script_path != NULL && ctx->script_path[0] != '\0') {
        fprintf(sf, "-T \"$DIR/linker_script.ld\" ");
    }
    if (ctx->plugin_path != NULL && ctx->plugin_path[0] != '\0') {
        size_t pi;
        fprintf(sf, "-plugin '%s' ", ctx->plugin_path);
        for (pi = 0; pi < ctx->plugin_opt_count; ++pi) {
            fprintf(sf, "-plugin-opt '%s' ", ctx->plugin_opts[pi]);
        }
    }
    fprintf(sf, "-o \"$DIR/repro.out\" ");
    for (i = 0; i < inputs->count; ++i) {
        fprintf(sf, "\"$DIR/input_%03zu.o\" ", i);
    }
    fprintf(sf, "\"$@\"\n");
    fclose(sf);
    if (chmod(script_path, 0755) != 0) {
        fprintf(stderr, "ld: failed to mark --reproduce script executable: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int write_map_file(const ld_ctx_t *ctx, const objvec_t *inputs, elfobj_t *out) {
    FILE *fp;
    size_t i;

    if (ctx->map_path == NULL || ctx->map_path[0] == '\0') {
        return 0;
    }
    fp = fopen(ctx->map_path, "w");
    if (fp == NULL) {
        fprintf(stderr, "ld: failed to open map file %s: %s\n", ctx->map_path, strerror(errno));
        return -1;
    }

    fprintf(fp, "Output: %s\n", ctx->out_path);
    fprintf(fp, "Type: %u\n", (unsigned)elf_type(out));
    fprintf(fp, "Class: %s\n", elf_class(out) == ELFOBJ_CLASS_64 ? "ELF64" : "ELF32");
    fprintf(fp, "\nInputs:\n");
    for (i = 0; i < inputs->count; ++i) {
        fprintf(fp, "  %s\n", inputs->names[i]);
    }
    if (ctx->dso_inputs.count > 0) {
        fprintf(fp, "\nDSO Inputs:\n");
        for (i = 0; i < ctx->dso_inputs.count; ++i) {
            fprintf(fp, "  %s\n", ctx->dso_inputs.items[i]);
        }
    }

    fprintf(fp, "\nSections:\n");
    for (i = 0; i < elf_section_count(out); ++i) {
        const elf_section_t *sec = elf_section_get(out, i);
        const char *name = sec != NULL ? elf_section_name(sec) : NULL;

        if (sec == NULL) {
            continue;
        }
        fprintf(fp, "  %-20s addr=0x%llx size=0x%llx flags=0x%llx\n",
                name != NULL ? name : "<unnamed>",
                (unsigned long long)elf_section_addr(sec),
                (unsigned long long)elf_section_size(sec),
                (unsigned long long)elf_section_flags(sec));
    }

    fprintf(fp, "\nProgram Headers:\n");
    for (i = 0; i < (size_t)elf_program_header_count(out); ++i) {
        fprintf(fp, "  [%02zu] type=0x%x flags=0x%x align=0x%llx\n",
                i,
                (unsigned)elf_program_header_type(out, i),
                (unsigned)elf_program_header_flags(out, i),
                (unsigned long long)elf_program_header_align(out, i));
    }

    fprintf(fp, "\nSymbols:\n");
    for (i = 0; i < elf_symbol_count(out); ++i) {
        const elf_symbol_t *sym = elf_symbol_at(out, i);
        const char *name;
        const char *src;

        if (sym == NULL) {
            continue;
        }
        if (elf_symbol_bind(sym) == STB_LOCAL || elf_symbol_shndx(sym) == SHN_UNDEF) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        src = find_symbol_source_input(inputs, name);
        fprintf(fp, "  %-28s value=0x%llx size=%llu bind=%u type=%u shndx=%u source=%s\n",
                name,
                (unsigned long long)elf_symbol_value(sym),
                (unsigned long long)elf_symbol_size(sym),
                (unsigned)elf_symbol_bind(sym),
                (unsigned)elf_symbol_type(sym),
                (unsigned)elf_symbol_shndx(sym),
                src != NULL ? src : "<synthetic>");
    }

    fclose(fp);
    return 0;
}
