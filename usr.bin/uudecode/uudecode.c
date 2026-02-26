/*
 * uudecode - decode a uuencoded file
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <ctype.h>
#include "libuu.h"

static int p_flag = 0;      /* -p: Write to stdout */
static char *o_flag = NULL; /* -o: Output file override */
static int s_flag = 0;      /* -s: Strip path components (secure mode) */

static void usage(void) {
    fprintf(stderr, "usage: uudecode [-p] [-s] [-o output_file] [file ...]\n");
    exit(1);
}

/* Simple basename replacement */
static const char *simple_basename(const char *path) {
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

static int is_unsafe_path(const char *path) {
    if (path == NULL) return 0;
    if (path[0] == '/') return 1; /* Absolute path */
    /* Check for .. at start */
    if (strncmp(path, "../", 3) == 0 || strcmp(path, "..") == 0) return 1;
    /* Check for /.. inside */
    if (strstr(path, "/../")) return 1;
    /* Check for /.. at end */
    size_t len = strlen(path);
    if (len >= 3 && strcmp(path + len - 3, "/..") == 0) return 1;
    return 0;
}

static int decode_file(FILE *fp, const char *input_name) {
    char line[1024];
    int mode = 0;
    char filename[256];
    int header_found = 0;
    FILE *out_fp = NULL;
    const char *out_path = NULL; /* The path we actually write to */

    /* Search for header */
    while (fgets(line, sizeof(line), fp)) {
        if (uu_parse_header(line, &mode, filename, sizeof(filename)) == 0) {
            header_found = 1;
            break;
        }
    }

    if (!header_found) {
        fprintf(stderr, "uudecode: %s: no begin header found\n", input_name);
        return 1;
    }

    /* Determine output file */
    if (p_flag) {
        out_fp = stdout;
        out_path = "stdout";
    } else {
        if (o_flag) {
            out_path = o_flag;
        } else {
            /* Use filename from header */
            out_path = filename;

            /* Security checks */
            /* Strip path components by default for security */
            out_path = simple_basename(out_path);

            /* Check for unsafe basename (e.g. "..") */
            if (is_unsafe_path(out_path)) {
                fprintf(stderr, "uudecode: %s: illegal filename.\n", out_path);
                return 1;
            }
        }

        /* Create file */
        /* Use O_TRUNC to overwrite if exists */
        /* Open with 0666 initially, we'll chmod later */
        int fd = open(out_path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (fd < 0) {
            perror(out_path);
            return 1;
        }
        out_fp = fdopen(fd, "w");
        if (!out_fp) {
            perror(out_path);
            close(fd);
            return 1;
        }
    }

    /* Decode body */
    unsigned char buf[1024]; /* buffer for decoded bytes */

    while (fgets(line, sizeof(line), fp)) {
        /* Check for explicit "end" line */
        if (strncmp(line, "end", 3) == 0 && (line[3] == '\n' || line[3] == '\r' || line[3] == '\0')) {
            break;
        }

        ssize_t n = uu_decode_line(line, buf, sizeof(buf));
        if (n < 0) {
            fprintf(stderr, "uudecode: %s: invalid line data\n", input_name);
            /* Don't remove file, just stop? Requirement R2: "fail safely without clobbering files."
               If we already opened and truncated, we clobbered it.
               Too late. But we can stop writing. */
            break;
        }
        if (n == 0) {
            /* Length 0 means end of data block. */
            /* Continue to find "end" line or stop? */
            /* Standard practice: data ends here. */
            break;
        }

        if (fwrite(buf, 1, n, out_fp) != (size_t)n) {
             perror("fwrite");
             break;
        }
    }

    if (!p_flag && out_fp != stdout) {
        fclose(out_fp);
        /* Apply mode from header */
        /* chmod handles permissions */
        if (chmod(out_path, mode) < 0) {
            perror(out_path);
            return 1;
        }
    }

    return 0;
}

int main(int argc, char *argv[]) {
    int i = 1;
    while (i < argc && argv[i][0] == '-') {
        if (strcmp(argv[i], "--") == 0) {
            i++;
            break;
        }

        char *arg = argv[i] + 1;
        if (*arg == '\0') {
             /* Handle "-" as stdin? standard usually treats "-" as file if following options.
                But here it's an option argument. If it's just "-", it's not a flag. */
             break;
        }

        while (*arg) {
            switch (*arg) {
                case 'p':
                    p_flag = 1;
                    break;
                case 's':
                    s_flag = 1;
                    break;
                case 'o':
                    /* Handle -o value */
                    if (*(arg + 1)) {
                        /* Value is attached: -ofile */
                        o_flag = arg + 1;
                        goto next_arg;
                    } else if (i + 1 < argc) {
                        /* Value is next arg */
                        o_flag = argv[++i];
                        goto next_arg;
                    } else {
                        fprintf(stderr, "uudecode: option requires an argument -- o\n");
                        usage();
                    }
                    break;
                default:
                    fprintf(stderr, "uudecode: illegal option -- %c\n", *arg);
                    usage();
            }
            arg++;
        }
        next_arg:
        i++;
    }

    int files_start = i;
    int files_count = argc - files_start;

    if (files_count == 0) {
        /* Read from stdin */
        if (decode_file(stdin, "stdin") != 0) {
            return 1;
        }
    } else {
        for (int j = files_start; j < argc; j++) {
            FILE *fp;
            if (strcmp(argv[j], "-") == 0) {
                fp = stdin;
            } else {
                fp = fopen(argv[j], "r");
                if (!fp) {
                    perror(argv[j]);
                    continue;
                }
            }

            if (decode_file(fp, argv[j]) != 0) {
                if (fp != stdin) fclose(fp);
                return 1;
            }
            if (fp != stdin) fclose(fp);
        }
    }

    return 0;
}
