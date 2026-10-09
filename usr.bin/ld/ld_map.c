/*
 * ld_map.c -- the link map and the --reproduce bundle.
 *
 * Both are files written for someone to read or run later, when the link
 * that made them is gone: so a write that did not happen is an error and
 * not a shorter file, a path that does not fit is refused and not cut
 * short, and what goes into the shell script is quoted for the shell.
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

/* Everything written got there: nothing failed along the way, and the
 * close, which is when a full disk is often heard of, did not either. */
static int finish_file(FILE *fp, const char *path, const char *what) {
    int failed = ferror(fp);

    if (fclose(fp) != 0 || failed) {
        fprintf(stderr, "ld: failed to write %s %s: %s\n", what, path, strerror(errno ? errno : EIO));
        return -1;
    }
    return 0;
}

/* DIR/LEAF, whole or not at all. */
static int bundle_path(char *buf, size_t size, const char *dir, const char *leaf) {
    int n = snprintf(buf, size, "%s/%s", dir, leaf);

    if (n < 0 || (size_t)n >= size) {
        fprintf(stderr, "ld: --reproduce: the path %s/%s is too long\n", dir, leaf);
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
        fprintf(stderr, "ld: --reproduce: cannot read %s\n", src);
        return -1;
    }
    fp = fopen(dst, "wb");
    if (fp == NULL) {
        fprintf(stderr, "ld: failed to write %s: %s\n", dst, strerror(errno));
        free(buf);
        return -1;
    }
    if (sz != 0) {
        (void)fwrite(buf, 1, sz, fp);
    }
    free(buf);
    return finish_file(fp, dst, "the copy");
}

/* A word for sh: in single quotes, where only a single quote means
 * anything, and that one written as '\''. */
static void put_shell_word(FILE *fp, const char *s) {
    fputc('\'', fp);
    for (; *s != '\0'; ++s) {
        if (*s == '\'') {
            fputs("'\\''", fp);
        } else {
            fputc(*s, fp);
        }
    }
    fputs("' ", fp);
}

/* A value for a "key=value" line: the line ends where the value does,
 * so a newline in it, and the backslash that says so, are spelt out. */
static void put_manifest_value(FILE *fp, const char *s) {
    for (; *s != '\0'; ++s) {
        if (*s == '\n') {
            fputs("\\n", fp);
        } else if (*s == '\\') {
            fputs("\\\\", fp);
        } else {
            fputc(*s, fp);
        }
    }
    fputc('\n', fp);
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
    if (bundle_path(manifest_path, sizeof(manifest_path), ctx->reproduce_path, "manifest.txt") != 0 ||
        bundle_path(script_path, sizeof(script_path), ctx->reproduce_path, "repro.sh") != 0) {
        return -1;
    }
    mf = fopen(manifest_path, "w");
    if (mf == NULL) {
        fprintf(stderr, "ld: failed to write --reproduce manifest %s: %s\n", manifest_path, strerror(errno));
        return -1;
    }
    fprintf(mf, "mode=%s\n", ctx->mode == 64 ? "x86_64" : "i386");
    fprintf(mf, "type=%u\n", (unsigned)ctx->expect_type);
    if (ctx->entry_symbol != NULL) {
        fputs("entry=", mf);
        put_manifest_value(mf, ctx->entry_symbol);
    }
    if (ctx->script_path != NULL) {
        fputs("script=", mf);
        put_manifest_value(mf, ctx->script_path);
    }
    if (ctx->plugin_path != NULL) {
        fputs("plugin=", mf);
        put_manifest_value(mf, ctx->plugin_path);
    }
    for (i = 0; i < inputs->count; ++i) {
        fprintf(mf, "input[%zu]=", i);
        put_manifest_value(mf, inputs->names[i] != NULL ? inputs->names[i] : "<unknown>");
    }
    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        fprintf(mf, "shared[%zu]=", i);
        put_manifest_value(mf, ctx->dso_inputs.items[i] != NULL ? ctx->dso_inputs.items[i] : "<unknown>");
    }
    if (finish_file(mf, manifest_path, "the --reproduce manifest") != 0) {
        return -1;
    }

    for (i = 0; i < inputs->count; ++i) {
        char obj_path[1024];
        char leaf[32];

        snprintf(leaf, sizeof(leaf), "input_%03zu.o", i);
        if (bundle_path(obj_path, sizeof(obj_path), ctx->reproduce_path, leaf) != 0) {
            return -1;
        }
        if (elf_write_file(inputs->objs[i], obj_path) != ELF_OK) {
            fprintf(stderr, "ld: failed to write --reproduce object %s\n", obj_path);
            return -1;
        }
    }
    if (ctx->script_path != NULL && ctx->script_path[0] != '\0') {
        if (bundle_path(script_copy, sizeof(script_copy), ctx->reproduce_path, "linker_script.ld") != 0 ||
            copy_file_bytes(ctx->script_path, script_copy) != 0) {
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
        fprintf(sf, ctx->pie ? "-pie " : "-shared ");
    }
    if (ctx->entry_symbol != NULL && ctx->entry_symbol[0] != '\0') {
        fputs("-e ", sf);
        put_shell_word(sf, ctx->entry_symbol);
    }
    if (ctx->script_path != NULL && ctx->script_path[0] != '\0') {
        fprintf(sf, "-T \"$DIR/linker_script.ld\" ");
    }
    if (ctx->plugin_path != NULL && ctx->plugin_path[0] != '\0') {
        size_t pi;

        fputs("-plugin ", sf);
        put_shell_word(sf, ctx->plugin_path);
        for (pi = 0; pi < ctx->plugin_opt_count; ++pi) {
            fputs("-plugin-opt ", sf);
            put_shell_word(sf, ctx->plugin_opts[pi]);
        }
    }
    fprintf(sf, "-o \"$DIR/repro.out\" ");
    for (i = 0; i < inputs->count; ++i) {
        fprintf(sf, "\"$DIR/input_%03zu.o\" ", i);
    }
    /* The shared objects are not copied: they are named, where they were. */
    for (i = 0; i < ctx->dso_inputs.count; ++i) {
        if (ctx->dso_inputs.items[i] != NULL) {
            put_shell_word(sf, ctx->dso_inputs.items[i]);
        }
    }
    fprintf(sf, "\"$@\"\n");
    if (finish_file(sf, script_path, "the --reproduce script") != 0) {
        return -1;
    }
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

    fprintf(fp, "Output: %s\n", ctx->out_path != NULL ? ctx->out_path : "<none>");
    fprintf(fp, "Type: %u\n", (unsigned)elf_type(out));
    fprintf(fp, "Class: %s\n", elf_class(out) == ELFOBJ_CLASS_64 ? "ELF64" : "ELF32");
    fprintf(fp, "\nInputs:\n");
    for (i = 0; i < inputs->count; ++i) {
        fprintf(fp, "  %s\n", inputs->names[i] != NULL ? inputs->names[i] : "<unknown>");
    }
    if (ctx->dso_inputs.count > 0) {
        fprintf(fp, "\nDSO Inputs:\n");
        for (i = 0; i < ctx->dso_inputs.count; ++i) {
            fprintf(fp, "  %s\n", ctx->dso_inputs.items[i] != NULL ? ctx->dso_inputs.items[i] : "<unknown>");
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

    return finish_file(fp, ctx->map_path, "the map file");
}
