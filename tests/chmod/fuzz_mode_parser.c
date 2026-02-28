#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#include "mode_parser.h"

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef S_ISUID
#define S_ISUID 0004000
#endif
#ifndef S_ISGID
#define S_ISGID 0002000
#endif
#ifndef S_ISVTX
#define S_ISVTX 0001000
#endif

#define MODE_BITS (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)

struct corpus_file {
    char path[PATH_MAX];
};

static uint32_t
xorshift32(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static size_t
read_file_bytes(const char *path, unsigned char *buf, size_t cap)
{
    FILE *fp = fopen(path, "rb");
    size_t n;

    if (fp == NULL) {
        return 0;
    }
    n = fread(buf, 1, cap, fp);
    fclose(fp);
    return n;
}

static void
bytes_to_mode_string(const unsigned char *in, size_t in_len, char *out,
    size_t out_len)
{
    static const char alphabet[] = "01234567ugoa+rwxXst-=,z!@#";
    size_t i;

    if (out_len == 0) {
        return;
    }

    for (i = 0; i + 1 < out_len && i < in_len; ++i) {
        out[i] = alphabet[in[i] % (sizeof(alphabet) - 1)];
    }
    out[i] = '\0';
}

static void
mutate_bytes(unsigned char *buf, size_t len, uint32_t *rng)
{
    size_t flips;
    size_t i;

    if (len == 0) {
        return;
    }

    flips = 1 + (xorshift32(rng) % 6u);
    for (i = 0; i < flips; ++i) {
        size_t pos = xorshift32(rng) % len;
        buf[pos] ^= (unsigned char)(xorshift32(rng) & 0xFFu);
    }
}

static int
load_corpus(const char *dir_path, struct corpus_file **out_files, size_t *out_count)
{
    DIR *dir = opendir(dir_path);
    struct dirent *de;
    struct corpus_file *files = NULL;
    size_t count = 0;
    size_t cap = 0;

    if (dir == NULL) {
        perror("opendir corpus");
        return -1;
    }

    while ((de = readdir(dir)) != NULL) {
        struct corpus_file *grown;
        int n;

        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        if (count == cap) {
            size_t new_cap = (cap == 0) ? 16u : cap * 2u;
            grown = (struct corpus_file *)realloc(files,
                new_cap * sizeof(*grown));
            if (grown == NULL) {
                closedir(dir);
                free(files);
                return -1;
            }
            files = grown;
            cap = new_cap;
        }

        n = snprintf(files[count].path, sizeof(files[count].path), "%s/%s",
            dir_path, de->d_name);
        if (n < 0 || (size_t)n >= sizeof(files[count].path)) {
            closedir(dir);
            free(files);
            return -1;
        }

        ++count;
    }

    closedir(dir);

    if (count == 0) {
        fprintf(stderr, "fuzz_mode_parser: empty corpus at %s\n", dir_path);
        free(files);
        return -1;
    }

    *out_files = files;
    *out_count = count;
    return 0;
}

int
main(int argc, char *argv[])
{
    struct corpus_file *files = NULL;
    size_t file_count = 0;
    uint32_t rng = 0xC0FFEEu;
    unsigned char raw[256];
    char mode_str[256];
    size_t iterations;
    size_t i;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s <corpus_dir> [iterations]\n", argv[0]);
        return 1;
    }

    iterations = (argc == 3) ? (size_t)strtoul(argv[2], NULL, 10) : 10000u;

    if (load_corpus(argv[1], &files, &file_count) != 0) {
        return 1;
    }

    for (i = 0; i < iterations; ++i) {
        size_t idx = xorshift32(&rng) % file_count;
        size_t n = read_file_bytes(files[idx].path, raw, sizeof(raw));
        struct chmod_mode *parsed;
        char errbuf[128];
        mode_t seed_mode;

        if (n == 0) {
            continue;
        }

        mutate_bytes(raw, n, &rng);
        bytes_to_mode_string(raw, n, mode_str, sizeof(mode_str));

        parsed = chmod_setmode(mode_str, errbuf, sizeof(errbuf));
        if (parsed == NULL) {
            continue;
        }

        seed_mode = (mode_t)(xorshift32(&rng) & MODE_BITS);
        (void)chmod_getmode(parsed, seed_mode);
        chmod_freemode(parsed);
    }

    free(files);
    puts("fuzz_mode_parser: ok");
    return 0;
}
