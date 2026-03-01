#include <elfobj.h>

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(ADDR2LINE_HAVE_ZLIB)
#include <zlib.h>
#endif

#define ADDR2LINE_VERSION "0.1.0"

typedef struct {
    const char *exe_path;
    const char **addr_args;
    size_t addr_argc;
} addr2line_opts_t;

typedef struct {
    uint64_t value;
    uint64_t size;
    const char *name;
} func_symbol_t;

typedef struct {
    const char *canonical_name;
    const char *resolved_name;
    const uint8_t *data;
    size_t size;
    uint8_t *owned_data;
    int present;
    int compressed;
} debug_blob_t;

typedef struct {
    elfobj_t *elf;
    char *path;
    elfobj_class_t elf_class;
    elfobj_endian_t elf_endian;
    uint16_t elf_type;

    debug_blob_t debug_line;
    debug_blob_t debug_info;
    debug_blob_t debug_abbrev;
    debug_blob_t debug_str;
    debug_blob_t debug_line_str;
    debug_blob_t debug_ranges;
    debug_blob_t debug_rnglists;

    int has_symtab;
    int has_dynsym;
    func_symbol_t *func_syms;
    size_t func_count;
    size_t func_cap;
} addr2line_image_t;

static const char *g_progname = "addr2line";

static void usage(FILE *out) {
    fprintf(out,
            "usage: %s [-e file] [addr ...]\n"
            "       %s --help\n"
            "       %s --version\n",
            g_progname, g_progname, g_progname);
}

static void print_version(void) {
    printf("%s %s\n", g_progname, ADDR2LINE_VERSION);
}

static void warnf(const char *fmt, const char *arg) {
    fprintf(stderr, "%s: ", g_progname);
    fprintf(stderr, fmt, arg);
    fputc('\n', stderr);
}

static uint32_t rd_u32(const uint8_t *p, elfobj_endian_t e) {
    if (e == ELFOBJ_ENDIAN_BE) {
        return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
               (uint32_t)p[3];
    }
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint64_t rd_u64(const uint8_t *p, elfobj_endian_t e) {
    if (e == ELFOBJ_ENDIAN_BE) {
        return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) |
               ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
               ((uint64_t)p[6] << 8) | (uint64_t)p[7];
    }
    return ((uint64_t)p[0]) | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

static uint64_t rd_be64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

static int func_symbol_cmp(const void *lhs, const void *rhs) {
    const func_symbol_t *a = (const func_symbol_t *)lhs;
    const func_symbol_t *b = (const func_symbol_t *)rhs;

    if (a->value < b->value) {
        return -1;
    }
    if (a->value > b->value) {
        return 1;
    }
    if (a->size < b->size) {
        return -1;
    }
    if (a->size > b->size) {
        return 1;
    }
    return strcmp(a->name, b->name);
}

#if defined(ADDR2LINE_HAVE_ZLIB)
static int inflate_payload(const uint8_t *payload, size_t payload_size,
                          uint64_t expected_size, uint8_t **out_data,
                          size_t *out_size) {
    z_stream stream;
    uint8_t *buf;
    size_t cap;
    int rc;

    if (payload == NULL || out_data == NULL || out_size == NULL) {
        return -1;
    }

    cap = expected_size > 0 ? (size_t)expected_size : (payload_size * 8u + 1024u);
    if (cap < 1024u) {
        cap = 1024u;
    }

    buf = (uint8_t *)malloc(cap);
    if (buf == NULL) {
        return -1;
    }

    memset(&stream, 0, sizeof(stream));
    stream.next_in = (Bytef *)payload;
    stream.avail_in = (uInt)payload_size;
    stream.next_out = (Bytef *)buf;
    stream.avail_out = (uInt)cap;

    rc = inflateInit(&stream);
    if (rc != Z_OK) {
        free(buf);
        return -1;
    }

    while (1) {
        rc = inflate(&stream, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) {
            break;
        }
        if (rc != Z_OK) {
            inflateEnd(&stream);
            free(buf);
            return -1;
        }
        if (stream.avail_out == 0) {
            uint8_t *next;
            size_t used = (size_t)stream.total_out;
            size_t new_cap = cap * 2u;
            if (new_cap <= cap) {
                inflateEnd(&stream);
                free(buf);
                return -1;
            }
            next = (uint8_t *)realloc(buf, new_cap);
            if (next == NULL) {
                inflateEnd(&stream);
                free(buf);
                return -1;
            }
            buf = next;
            cap = new_cap;
            stream.next_out = (Bytef *)(buf + used);
            stream.avail_out = (uInt)(cap - used);
        }
    }

    inflateEnd(&stream);

    if (expected_size != 0 && (uint64_t)stream.total_out != expected_size) {
        free(buf);
        return -1;
    }

    *out_data = buf;
    *out_size = (size_t)stream.total_out;
    return 0;
}
#else
static int inflate_payload(const uint8_t *payload, size_t payload_size,
                          uint64_t expected_size, uint8_t **out_data,
                          size_t *out_size) {
    (void)payload;
    (void)payload_size;
    (void)expected_size;
    (void)out_data;
    (void)out_size;
    return -1;
}
#endif

static int decompress_debug_section(const elf_section_t *sec,
                                    elfobj_class_t elf_class,
                                    elfobj_endian_t elf_endian,
                                    const uint8_t *in,
                                    size_t in_size,
                                    uint8_t **out,
                                    size_t *out_size) {
    const char *name;
    const uint8_t *payload;
    size_t payload_size;
    uint64_t expect_size;

    if (sec == NULL || in == NULL || out == NULL || out_size == NULL) {
        return -1;
    }

    name = elf_section_name(sec);
    payload = NULL;
    payload_size = 0;
    expect_size = 0;

    if (name != NULL && strncmp(name, ".zdebug_", 8) == 0) {
        if (in_size < 12) {
            return -1;
        }
        if (memcmp(in, "ZLIB", 4) != 0) {
            return -1;
        }
        expect_size = rd_be64(in + 4);
        payload = in + 12;
        payload_size = in_size - 12;
    } else if ((elf_section_flags(sec) & SHF_COMPRESSED) != 0) {
        uint32_t ch_type;

        if (elf_class == ELFOBJ_CLASS_64) {
            if (in_size < 24) {
                return -1;
            }
            ch_type = rd_u32(in, elf_endian);
            expect_size = rd_u64(in + 8, elf_endian);
            payload = in + 24;
            payload_size = in_size - 24;
        } else {
            if (in_size < 12) {
                return -1;
            }
            ch_type = rd_u32(in, elf_endian);
            expect_size = rd_u32(in + 4, elf_endian);
            payload = in + 12;
            payload_size = in_size - 12;
        }

        if (ch_type != 1u) {
            return -1;
        }
    } else {
        return -1;
    }

    if (inflate_payload(payload, payload_size, expect_size, out, out_size) != 0) {
        return -1;
    }

    return 0;
}

static int load_debug_blob(addr2line_image_t *img, const char *canonical_name,
                           debug_blob_t *blob) {
    elf_section_t *sec;
    size_t size = 0;
    const uint8_t *data;

    memset(blob, 0, sizeof(*blob));
    blob->canonical_name = canonical_name;

    sec = elf_find_section(img->elf, canonical_name);
    if (sec == NULL && strncmp(canonical_name, ".debug_", 7) == 0) {
        char alt_name[128];
        int n = snprintf(alt_name, sizeof(alt_name), ".zdebug_%s", canonical_name + 7);
        if (n > 0 && (size_t)n < sizeof(alt_name)) {
            sec = elf_find_section(img->elf, alt_name);
        }
    }

    if (sec == NULL) {
        return 0;
    }

    data = (const uint8_t *)elf_section_data(sec, &size);
    if (size > 0 && data == NULL) {
        warnf("section has no payload: %s", canonical_name);
        return -1;
    }

    blob->present = 1;
    blob->resolved_name = elf_section_name(sec);

    if (elf_section_is_compressed_debug(sec)) {
        uint8_t *decompressed = NULL;
        size_t decompressed_size = 0;

        if (decompress_debug_section(sec,
                                     img->elf_class,
                                     img->elf_endian,
                                     data,
                                     size,
                                     &decompressed,
                                     &decompressed_size) != 0) {
            warnf("failed to decompress debug section: %s", canonical_name);
            return -1;
        }

        blob->compressed = 1;
        blob->owned_data = decompressed;
        blob->data = decompressed;
        blob->size = decompressed_size;
        return 0;
    }

    blob->data = data;
    blob->size = size;
    return 0;
}

static void image_reset(addr2line_image_t *img) {
    if (img == NULL) {
        return;
    }

    if (img->debug_line.owned_data != NULL) {
        free(img->debug_line.owned_data);
    }
    if (img->debug_info.owned_data != NULL) {
        free(img->debug_info.owned_data);
    }
    if (img->debug_abbrev.owned_data != NULL) {
        free(img->debug_abbrev.owned_data);
    }
    if (img->debug_str.owned_data != NULL) {
        free(img->debug_str.owned_data);
    }
    if (img->debug_line_str.owned_data != NULL) {
        free(img->debug_line_str.owned_data);
    }
    if (img->debug_ranges.owned_data != NULL) {
        free(img->debug_ranges.owned_data);
    }
    if (img->debug_rnglists.owned_data != NULL) {
        free(img->debug_rnglists.owned_data);
    }

    free(img->func_syms);
    free(img->path);

    if (img->elf != NULL) {
        elf_close(img->elf);
    }

    memset(img, 0, sizeof(*img));
}

static int image_append_func_symbol(addr2line_image_t *img,
                                    uint64_t value,
                                    uint64_t size,
                                    const char *name) {
    func_symbol_t *next;

    if (img->func_count == img->func_cap) {
        size_t new_cap = img->func_cap == 0 ? 128u : img->func_cap * 2u;
        next = (func_symbol_t *)realloc(img->func_syms, new_cap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        img->func_syms = next;
        img->func_cap = new_cap;
    }

    img->func_syms[img->func_count].value = value;
    img->func_syms[img->func_count].size = size;
    img->func_syms[img->func_count].name = name;
    img->func_count++;
    return 0;
}

static int image_collect_symbols(addr2line_image_t *img) {
    size_t i;
    size_t count = elf_symbol_count(img->elf);

    img->has_symtab = elf_find_section(img->elf, ".symtab") != NULL;
    img->has_dynsym = elf_find_section(img->elf, ".dynsym") != NULL;

    for (i = 0; i < count; ++i) {
        elf_symbol_t *sym = elf_symbol_at(img->elf, i);
        const char *name;

        if (sym == NULL) {
            continue;
        }
        if (elf_symbol_type(sym) != STT_FUNC) {
            continue;
        }
        name = elf_symbol_name(sym);
        if (name == NULL || name[0] == '\0') {
            continue;
        }
        if (image_append_func_symbol(img,
                                     elf_symbol_value(sym),
                                     elf_symbol_size(sym),
                                     name) != 0) {
            return -1;
        }
    }

    if (img->func_count > 1) {
        qsort(img->func_syms, img->func_count, sizeof(img->func_syms[0]), func_symbol_cmp);
    }

    return 0;
}

static void print_open_error(const char *path, elf_err_t err) {
    if (err == ELF_ERR_FORMAT) {
        fprintf(stderr, "%s: %s: file format not recognized\n", g_progname, path);
        return;
    }
    fprintf(stderr, "%s: %s: %s\n", g_progname, path, elf_errstr(err));
}

static int image_open(addr2line_image_t *img, const char *path) {
    elf_err_t err;

    memset(img, 0, sizeof(*img));

    img->path = strdup(path);
    if (img->path == NULL) {
        fprintf(stderr, "%s: out of memory\n", g_progname);
        return -1;
    }

    err = elf_open(path, &img->elf);
    if (err != ELF_OK) {
        print_open_error(path, err);
        image_reset(img);
        return -1;
    }

    img->elf_class = elf_class(img->elf);
    img->elf_endian = elf_endian(img->elf);
    img->elf_type = elf_type(img->elf);

    if (load_debug_blob(img, ".debug_line", &img->debug_line) != 0 ||
        load_debug_blob(img, ".debug_info", &img->debug_info) != 0 ||
        load_debug_blob(img, ".debug_abbrev", &img->debug_abbrev) != 0 ||
        load_debug_blob(img, ".debug_str", &img->debug_str) != 0 ||
        load_debug_blob(img, ".debug_line_str", &img->debug_line_str) != 0 ||
        load_debug_blob(img, ".debug_ranges", &img->debug_ranges) != 0 ||
        load_debug_blob(img, ".debug_rnglists", &img->debug_rnglists) != 0) {
        image_reset(img);
        return -1;
    }

    if (image_collect_symbols(img) != 0) {
        fprintf(stderr, "%s: out of memory\n", g_progname);
        image_reset(img);
        return -1;
    }

    return 0;
}

static int parse_hex_address(const char *text, uint64_t *out) {
    char *end = NULL;
    unsigned long long v;

    if (text == NULL || out == NULL) {
        return -1;
    }

    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }

    if (*text == '\0') {
        return -1;
    }

    errno = 0;
    v = strtoull(text, &end, 16);
    if (errno != 0 || end == text) {
        return -1;
    }
    while (*end != '\0') {
        if (!isspace((unsigned char)*end)) {
            return -1;
        }
        end++;
    }

    *out = (uint64_t)v;
    return 0;
}

static void output_unresolved(void) {
    puts("??:0");
}

static int resolve_stdin(void) {
    char line[256];

    while (fgets(line, sizeof(line), stdin) != NULL) {
        uint64_t query;
        if (parse_hex_address(line, &query) != 0) {
            warnf("invalid address: %s", line);
            output_unresolved();
            continue;
        }
        (void)query;
        output_unresolved();
    }

    return 0;
}

static int resolve_argv(const char **args, size_t n) {
    size_t i;

    for (i = 0; i < n; ++i) {
        uint64_t query;
        if (parse_hex_address(args[i], &query) != 0) {
            warnf("invalid address: %s", args[i]);
            output_unresolved();
            continue;
        }
        (void)query;
        output_unresolved();
    }

    return 0;
}

static int parse_options(int argc, char **argv, addr2line_opts_t *opts) {
    int i;

    memset(opts, 0, sizeof(*opts));
    opts->exe_path = "a.out";

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }

        if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            usage(stdout);
            exit(0);
        }
        if (strcmp(arg, "-V") == 0 || strcmp(arg, "--version") == 0) {
            print_version();
            exit(0);
        }

        if (strcmp(arg, "-e") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "%s: -e requires a file path\n", g_progname);
                return -1;
            }
            i++;
            opts->exe_path = argv[i];
            continue;
        }
        if (strncmp(arg, "--exe=", 6) == 0) {
            opts->exe_path = arg + 6;
            continue;
        }

        if (arg[0] == '-') {
            fprintf(stderr, "%s: unknown option: %s\n", g_progname, arg);
            return -1;
        }

        break;
    }

    opts->addr_args = (const char **)&argv[i];
    opts->addr_argc = (size_t)(argc - i);
    return 0;
}

int main(int argc, char **argv) {
    addr2line_opts_t opts;
    addr2line_image_t img;
    int rc;

    if (argc > 0 && argv[0] != NULL && argv[0][0] != '\0') {
        g_progname = argv[0];
    }

    if (parse_options(argc, argv, &opts) != 0) {
        usage(stderr);
        return 1;
    }

    if (image_open(&img, opts.exe_path) != 0) {
        return 1;
    }

    if (opts.addr_argc == 0) {
        rc = resolve_stdin();
    } else {
        rc = resolve_argv(opts.addr_args, opts.addr_argc);
    }

    image_reset(&img);
    return rc;
}
