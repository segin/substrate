/*
 * uncompress.c - Decompress .Z files (LZW)
 *
 * Copyright (c) 2024 The Substrate Project
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#define MAGIC_1 0x1f
#define MAGIC_2 0x9d

/* Bit masks for flags */
#define BIT_MASK    0x1f
#define BLOCK_MASK  0x80

/* Max code size */
#define MAXCODE(n)  ((1L << (n)) - 1)

/* LZW constants */
#define INIT_BITS 9
#define MAX_BITS 16
#define HSIZE 69001
#define BITS_MIN 9
#define BITS_MAX 16

/* Global flags */
static int force = 0;
static int cat = 0;
static int verbose = 0;

/* Buffer for code reading */
static int bits_in_buf = 0;
static long bit_buffer = 0;

/* Dictionary */
#define TAB_SIZE (1 << BITS_MAX)
static unsigned char suffix[TAB_SIZE];
static unsigned short prefix[TAB_SIZE];
static unsigned char stack[TAB_SIZE];

static void usage(void) {
    fprintf(stderr, "usage: uncompress [-cfv] [file ...]\n");
    exit(1);
}

/* Read one code from input */
static int get_code(FILE *fp, int *bits) {
    int code;

    while (bits_in_buf < *bits) {
        int c = fgetc(fp);
        if (c == EOF) {
            return -1;
        }
        bit_buffer |= ((long)c << bits_in_buf);
        bits_in_buf += 8;
    }

    code = bit_buffer & ((1 << *bits) - 1);
    bit_buffer >>= *bits;
    bits_in_buf -= *bits;

    return code;
}

static int decompress(FILE *in, FILE *out, const char *filename) {
    unsigned char header[3];
    int maxbits;
    int block_mode;
    int n_bits;
    int max_code;
    int free_ent;
    int old_code, code, incode;
    int finchar;
    unsigned char *stackp;

    /* Reset globals for get_code */
    bit_buffer = 0;
    bits_in_buf = 0;

    /* Read header */
    if (fread(header, 1, 3, in) != 3) {
        /* If empty file, just return success (empty output) or error? */
        /* Standard uncompress treats empty file as error "not in compressed format" usually */
        if (feof(in) && ftell(in) == 0) return -1;
        fprintf(stderr, "uncompress: %s: not in compressed format\n", filename);
        return -1;
    }

    if (header[0] != MAGIC_1 || header[1] != MAGIC_2) {
        fprintf(stderr, "uncompress: %s: not in compressed format\n", filename);
        return -1;
    }

    maxbits = header[2] & BIT_MASK;
    block_mode = header[2] & BLOCK_MASK;

    if (maxbits > BITS_MAX) {
        fprintf(stderr, "uncompress: %s: compressed with %d bits, can only handle %d bits\n", filename, maxbits, BITS_MAX);
        return -1;
    }

    /* Initialize */
    n_bits = INIT_BITS;
    max_code = MAXCODE(n_bits);
    free_ent = block_mode ? 257 : 256;

    /* Clear table */
    memset(prefix, 0, sizeof(prefix));
    memset(suffix, 0, sizeof(suffix));

    for (code = 0; code < 256; code++) {
        suffix[code] = (unsigned char)code;
    }

    /* Read first code */
    old_code = get_code(in, &n_bits);
    if (old_code == -1) return 0; /* EOF immediately after header? */

    if (old_code == 256 && block_mode) {
        /* Standard behavior: 256 is CLEAR. */
        /* But usually first code is not CLEAR. If it is, handle it. */
        /* Just treat as empty or read next? */
        /* Actually, if first code is CLEAR, we just reset (noop) and read next. */
         while (old_code == 256 && block_mode) {
             old_code = get_code(in, &n_bits);
             if (old_code == -1) return 0;
         }
    }

    finchar = old_code;
    if (old_code >= 256) {
        fprintf(stderr, "uncompress: %s: corrupt input (first code %d)\n", filename, old_code);
        return -1;
    }

    fputc((char)finchar, out);

    stackp = stack;

    while ((code = get_code(in, &n_bits)) != -1) {
        if (code == 256 && block_mode) {
            /* Clear dictionary */
            free_ent = 257;
            n_bits = INIT_BITS;
            max_code = MAXCODE(n_bits);

            /* Read next code */
            code = get_code(in, &n_bits);
            if (code == -1) break;

            if (code == 256) continue; /* Double CLEAR? */

            old_code = code;
            finchar = old_code;
            if (old_code >= 256) {
                 fprintf(stderr, "uncompress: %s: corrupt input (post-clear code %d)\n", filename, old_code);
                 return -1;
            }
            fputc((char)finchar, out);
            continue;
        }

        incode = code;

        /* Special case: code == free_ent (KwKwK exception) */
        if (code >= free_ent) {
            if (code > free_ent) {
                fprintf(stderr, "uncompress: %s: corrupt input (code %d > free %d)\n", filename, code, free_ent);
                return -1;
            }
            *stackp++ = (unsigned char)finchar;
            code = old_code;
        }

        /* Walk up the prefix chain */
        while (code >= 256) {
            *stackp++ = suffix[code];
            code = prefix[code];
        }

        finchar = suffix[code];
        *stackp++ = (unsigned char)finchar;

        /* Output stack */
        while (stackp > stack) {
            fputc(*--stackp, out);
        }

        /* Add new entry to dictionary */
        if (free_ent < (1 << maxbits)) {
            prefix[free_ent] = (unsigned short)old_code;
            suffix[free_ent] = (unsigned char)finchar;
            free_ent++;
        }

        /* Update bit size */
        if (free_ent > max_code && n_bits < maxbits) {
            n_bits++;
            max_code = MAXCODE(n_bits);
        }

        old_code = incode;
    }

    if (ferror(in)) {
        perror(filename);
        return -1;
    }
    if (ferror(out)) {
        perror("write error");
        return -1;
    }
    return 0;
}

static int process_file(const char *name) {
    FILE *in = NULL, *out = NULL;
    char out_name[1024];
    char in_name[1024];
    struct stat sb;
    struct stat in_sb;
    int res;
    int from_stdin = (name == NULL || strcmp(name, "-") == 0);

    if (from_stdin) {
        in = stdin;
        out = stdout; /* Implicitly stdout for stdin input unless overridden? standard says stdout. */
        /* If force is not set, checking if stdout is a terminal?
         * "Uncompress writes to standard output if no files are specified."
         * Doesn't check for terminal.
         */
    } else {
        strncpy(in_name, name, sizeof(in_name)-1);
        in_name[sizeof(in_name)-1] = '\0';

        /* Check if input exists */
        if (stat(in_name, &in_sb) < 0) {
            /* Try appending .Z */
            if (strlen(in_name) + 2 < sizeof(in_name)) {
                strcat(in_name, ".Z");
                if (stat(in_name, &in_sb) < 0) {
                    perror(name);
                    return 1;
                }
            } else {
                perror(name);
                return 1;
            }
        }

        in = fopen(in_name, "rb");
        if (!in) {
            perror(in_name);
            return 1;
        }

        if (cat) {
            out = stdout;
        } else {
            size_t len = strlen(in_name);
            /* Determine output name */
            if (len > 2 && strcmp(in_name + len - 2, ".Z") == 0) {
                strncpy(out_name, in_name, len - 2);
                out_name[len - 2] = '\0';
            } else {
                /* If not ending in .Z, standard uncompress complains. */
                fprintf(stderr, "uncompress: %s: unknown suffix -- ignored\n", in_name);
                fclose(in);
                return 1;
            }

            int fd;
            int flags = O_WRONLY | O_CREAT;

            if (!force) {
                flags |= O_EXCL;
            } else {
                flags |= O_TRUNC;
            }

            fd = open(out_name, flags, 0666);
            if (fd < 0) {
                if (errno == EEXIST) {
                    /* Check if same file */
                    if (stat(out_name, &sb) == 0 &&
                        sb.st_dev == in_sb.st_dev && sb.st_ino == in_sb.st_ino) {
                         fprintf(stderr, "%s: input and output are the same file\n", in_name);
                    } else {
                         fprintf(stderr, "%s: already exists\n", out_name);
                    }
                    fclose(in);
                    return 1;
                }
                perror(out_name);
                fclose(in);
                return 1;
            }

            out = fdopen(fd, "wb");
            if (!out) {
                perror(out_name);
                close(fd);
                fclose(in);
                return 1;
            }
        }
    }

    res = decompress(in, out, from_stdin ? "stdin" : in_name);

    if (!from_stdin) {
        fclose(in);
    }

    if (out != stdout) {
        if (fclose(out) != 0) {
            perror(out_name);
            res = -1;
        }

        if (res == 0) {
            /* Success. Restore mode/times. */
            /* We have in_sb from stat() */
            /* chmod */
            if (chmod(out_name, in_sb.st_mode & 07777) < 0) {
                /* ignore error */
            }
            /* utimes/utimensat - check availability */
            /* Assuming we can skip for now or use what's available. */

            if (verbose) {
                fprintf(stderr, "%s: unpacked\n", in_name);
            }

            /* Unlink input file */
            if (unlink(in_name) < 0) {
                perror(in_name);
                res = -1;
            }
        } else {
            /* Failure. Remove partial output. */
            if (unlink(out_name) < 0) {
                 /* ignore */
            }
        }
    } else {
        /* Output to stdout */
        if (res != 0) {
            return 1;
        }
    }

    return (res != 0);
}

int main(int argc, char *argv[]) {
    int c;
    int i;
    int exit_code = 0;

    while ((c = getopt(argc, argv, "cfv")) != -1) {
        switch (c) {
            case 'c':
                cat = 1;
                break;
            case 'f':
                force = 1;
                break;
            case 'v':
                verbose = 1;
                break;
            default:
                usage();
        }
    }

    if (optind == argc) {
        if (process_file(NULL) != 0) {
            exit_code = 1;
        }
    } else {
        for (i = optind; i < argc; i++) {
            if (process_file(argv[i]) != 0) {
                exit_code = 1;
            }
        }
    }

    return exit_code;
}
