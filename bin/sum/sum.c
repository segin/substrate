#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SUM_IO_BUFSZ 65536
#define SUM_MAX_ALGS 8

typedef enum {
    SUM_ALG_OLD1 = 1,
    SUM_ALG_OLD2 = 2
} sum_algorithm_t;

typedef enum {
    SUM_COMPAT_BSD = 0,
    SUM_COMPAT_GNU = 1
} sum_compat_t;

struct sum_state {
    sum_algorithm_t alg;
    uint16_t old1;
    uint64_t old2;
    uint64_t bytes;
};

struct sum_result {
    sum_algorithm_t alg;
    uint32_t checksum;
    uint64_t blocks;
};

struct sum_options {
    const char *progname;
    sum_compat_t compat;
    bool quiet;
    bool reverse;
    bool echo_stdin;

    bool algorithm_explicit;
    sum_algorithm_t algorithms[SUM_MAX_ALGS];
    size_t algorithm_count;

    const char **strings;
    size_t string_count;
    size_t string_cap;
};

static void usage(FILE *f, const char *progname)
{
    fprintf(f, "Usage: %s [OPTION]... [FILE]...\n", progname);
}

static void print_version(void)
{
    printf("sum (Substrate) 1.0\n");
}

static void print_help(const struct sum_options *o)
{
    usage(stdout, o->progname);
    printf(
"Compute legacy checksums and block counts.\n\n"
"With no FILE, or when FILE is -, read standard input.\n\n"
"Legacy algorithm selection:\n"
"  -o 1|2               select historic algorithm 1 (BSD) or 2 (SysV)\n"
"      --sysv           select historic algorithm 2 (SysV)\n"
"  -a ALGORITHM         add algorithm (old1/old2/bsd/sysv/1/2)\n"
"      --algorithm=ALG  add algorithm (same as -a)\n\n"
"Compatibility and conflict policy:\n"
"      --compat=MODE    MODE is bsd (default) or gnu\n"
"  -s STRING            BSD mode: checksum the literal string\n"
"                       GNU compat mode: select SysV algorithm\n"
"      --string=TEXT    checksum the literal string (always)\n"
"  -r                   BSD mode: reverse-output flag (legacy no-op)\n"
"                       GNU compat mode: select BSD algorithm\n\n"
"Output controls:\n"
"  -q                   quiet (checksum only)\n"
"  -p                   echo stdin to stdout while checksumming\n"
"      --help           display this help and exit\n"
"      --version        output version information and exit\n"
    );
}

static const char *algorithm_name(sum_algorithm_t alg)
{
    if (alg == SUM_ALG_OLD1) {
        return "old1";
    }
    return "old2";
}

static int parse_compat_value(const char *value, sum_compat_t *compat)
{
    if (strcmp(value, "bsd") == 0) {
        *compat = SUM_COMPAT_BSD;
        return 0;
    }
    if (strcmp(value, "gnu") == 0) {
        *compat = SUM_COMPAT_GNU;
        return 0;
    }
    return -1;
}

static int prescan_compat(int argc, char *argv[], sum_compat_t *compat)
{
    int i;

    *compat = SUM_COMPAT_BSD;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            break;
        }

        if (strncmp(arg, "--compat=", 9) == 0) {
            if (parse_compat_value(arg + 9, compat) != 0) {
                return -1;
            }
            continue;
        }

        if (strcmp(arg, "--compat") == 0) {
            if (i + 1 >= argc) {
                return -1;
            }
            i++;
            if (parse_compat_value(argv[i], compat) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static bool parse_algorithm_token(const char *token, size_t len, sum_algorithm_t *alg)
{
    if (len == 1 && token[0] == '1') {
        *alg = SUM_ALG_OLD1;
        return true;
    }
    if (len == 1 && token[0] == '2') {
        *alg = SUM_ALG_OLD2;
        return true;
    }

    if (len == 3 && strncmp(token, "bsd", 3) == 0) {
        *alg = SUM_ALG_OLD1;
        return true;
    }
    if (len == 4 && strncmp(token, "sysv", 4) == 0) {
        *alg = SUM_ALG_OLD2;
        return true;
    }
    if (len == 4 && strncmp(token, "old1", 4) == 0) {
        *alg = SUM_ALG_OLD1;
        return true;
    }
    if (len == 4 && strncmp(token, "old2", 4) == 0) {
        *alg = SUM_ALG_OLD2;
        return true;
    }
    if (len == 4 && strncmp(token, "sum1", 4) == 0) {
        *alg = SUM_ALG_OLD1;
        return true;
    }
    if (len == 4 && strncmp(token, "sum2", 4) == 0) {
        *alg = SUM_ALG_OLD2;
        return true;
    }

    return false;
}

static void set_default_algorithm(struct sum_options *o)
{
    o->algorithm_count = 1;
    o->algorithms[0] = SUM_ALG_OLD1;
    o->algorithm_explicit = false;
}

static bool algorithm_exists(const struct sum_options *o, sum_algorithm_t alg)
{
    size_t i;

    for (i = 0; i < o->algorithm_count; i++) {
        if (o->algorithms[i] == alg) {
            return true;
        }
    }
    return false;
}

static int add_algorithm(struct sum_options *o, sum_algorithm_t alg)
{
    if (algorithm_exists(o, alg)) {
        return 0;
    }
    if (o->algorithm_count >= SUM_MAX_ALGS) {
        return -1;
    }
    o->algorithms[o->algorithm_count++] = alg;
    return 0;
}

static int replace_algorithms_with(struct sum_options *o, sum_algorithm_t alg)
{
    o->algorithm_count = 0;
    if (add_algorithm(o, alg) != 0) {
        return -1;
    }
    o->algorithm_explicit = true;
    return 0;
}

static int parse_algorithm_list(struct sum_options *o, const char *arg)
{
    const char *p = arg;

    if (!o->algorithm_explicit) {
        o->algorithm_count = 0;
        o->algorithm_explicit = true;
    }

    while (*p != '\0') {
        const char *comma = strchr(p, ',');
        size_t len;
        sum_algorithm_t alg;

        if (comma == NULL) {
            len = strlen(p);
        } else {
            len = (size_t)(comma - p);
        }

        if (len == 0) {
            return -1;
        }

        if (!parse_algorithm_token(p, len, &alg)) {
            return -1;
        }
        if (add_algorithm(o, alg) != 0) {
            return -1;
        }

        if (comma == NULL) {
            break;
        }
        p = comma + 1;
    }

    if (o->algorithm_count == 0) {
        return -1;
    }

    return 0;
}

static int add_string_input(struct sum_options *o, const char *s)
{
    if (o->string_count >= o->string_cap) {
        return -1;
    }
    o->strings[o->string_count++] = s;
    return 0;
}

static void state_init(struct sum_state *st, sum_algorithm_t alg)
{
    st->alg = alg;
    st->old1 = 0;
    st->old2 = 0;
    st->bytes = 0;
}

static void state_update(struct sum_state *st, const unsigned char *buf, size_t len)
{
    size_t i;

    st->bytes += len;

    if (st->alg == SUM_ALG_OLD1) {
        for (i = 0; i < len; i++) {
            if ((st->old1 & 1U) != 0U) {
                st->old1 = (uint16_t)((st->old1 >> 1) | 0x8000U);
            } else {
                st->old1 = (uint16_t)(st->old1 >> 1);
            }
            st->old1 = (uint16_t)(st->old1 + buf[i]);
        }
    } else {
        for (i = 0; i < len; i++) {
            st->old2 += buf[i];
        }
    }
}

static struct sum_result state_finalize(const struct sum_state *st)
{
    struct sum_result r;

    r.alg = st->alg;

    if (st->alg == SUM_ALG_OLD1) {
        r.checksum = st->old1;
        r.blocks = (st->bytes + 1023U) / 1024U;
    } else {
        uint32_t s32 = (uint32_t)st->old2;
        uint32_t fold1 = (s32 & 0xFFFFU) + (s32 >> 16);
        uint32_t fold2 = (fold1 & 0xFFFFU) + (fold1 >> 16);

        r.checksum = fold2 & 0xFFFFU;
        r.blocks = (st->bytes + 511U) / 512U;
    }

    return r;
}

static int write_all_stdout(const unsigned char *buf, size_t len)
{
    size_t off = 0;

    while (off < len) {
        ssize_t n = write(STDOUT_FILENO, buf + off, len - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        off += (size_t)n;
    }

    return 0;
}

static int checksum_fd(const struct sum_options *o, int fd, bool echo,
                       struct sum_result *results)
{
    struct sum_state states[SUM_MAX_ALGS];
    unsigned char buf[SUM_IO_BUFSZ];
    size_t i;

    for (i = 0; i < o->algorithm_count; i++) {
        state_init(&states[i], o->algorithms[i]);
    }

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));

        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        if (echo && write_all_stdout(buf, (size_t)n) != 0) {
            return -1;
        }

        for (i = 0; i < o->algorithm_count; i++) {
            state_update(&states[i], buf, (size_t)n);
        }
    }

    for (i = 0; i < o->algorithm_count; i++) {
        results[i] = state_finalize(&states[i]);
    }

    return 0;
}

static void checksum_string(const struct sum_options *o, const char *s,
                            struct sum_result *results)
{
    struct sum_state states[SUM_MAX_ALGS];
    const unsigned char *buf = (const unsigned char *)s;
    size_t len = strlen(s);
    size_t i;

    for (i = 0; i < o->algorithm_count; i++) {
        state_init(&states[i], o->algorithms[i]);
        state_update(&states[i], buf, len);
        results[i] = state_finalize(&states[i]);
    }
}

static void print_one_result(const struct sum_options *o,
                             const struct sum_result *r,
                             const char *name, bool show_name,
                             bool multi_alg)
{
    if (multi_alg) {
        printf("%s ", algorithm_name(r->alg));
    }

    if (o->quiet) {
        printf("%u", r->checksum);
        if (show_name && name != NULL) {
            printf(" %s", name);
        }
        printf("\n");
        return;
    }

    printf("%u %llu", r->checksum, (unsigned long long)r->blocks);
    if (show_name && name != NULL) {
        printf(" %s", name);
    }
    printf("\n");
}

static void print_results(const struct sum_options *o,
                          const struct sum_result *results,
                          const char *name, bool show_name)
{
    size_t i;
    bool multi_alg = o->algorithm_count > 1;

    (void)o->reverse;

    for (i = 0; i < o->algorithm_count; i++) {
        print_one_result(o, &results[i], name, show_name, multi_alg);
    }
}

static int process_stdin_stream(const struct sum_options *o,
                                bool show_name,
                                const char *name,
                                bool echo_stdin)
{
    struct sum_result results[SUM_MAX_ALGS];

    if (checksum_fd(o, STDIN_FILENO, echo_stdin, results) != 0) {
        fprintf(stderr, "%s: stdin: %s\n", o->progname, strerror(errno));
        return 1;
    }

    print_results(o, results, name, show_name);
    return 0;
}

static int process_file(const struct sum_options *o, const char *path)
{
    struct sum_result results[SUM_MAX_ALGS];
    int fd;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "%s: %s: %s\n", o->progname, path, strerror(errno));
        return 1;
    }

    if (checksum_fd(o, fd, false, results) != 0) {
        int saved = errno;

        close(fd);
        errno = saved;
        fprintf(stderr, "%s: %s: %s\n", o->progname, path, strerror(errno));
        return 1;
    }

    close(fd);
    print_results(o, results, path, true);
    return 0;
}

static int process_string(const struct sum_options *o, const char *s)
{
    struct sum_result results[SUM_MAX_ALGS];

    checksum_string(o, s, results);
    print_results(o, results, NULL, false);
    return 0;
}

int main(int argc, char *argv[])
{
    struct sum_options o;
    int opt;
    int ret = 0;
    int i;

    static const struct option longopts[] = {
        {"algorithm", required_argument, NULL, 1},
        {"compat", required_argument, NULL, 2},
        {"help", no_argument, NULL, 3},
        {"string", required_argument, NULL, 4},
        {"sysv", no_argument, NULL, 5},
        {"version", no_argument, NULL, 6},
        {NULL, 0, NULL, 0}
    };

    memset(&o, 0, sizeof(o));
    o.progname = argv[0];

    if (prescan_compat(argc, argv, &o.compat) != 0) {
        fprintf(stderr, "%s: invalid or missing value for --compat (expected bsd or gnu)\n", o.progname);
        return 1;
    }

    o.string_cap = (size_t)argc;
    o.strings = calloc(o.string_cap, sizeof(*o.strings));
    if (o.strings == NULL) {
        fprintf(stderr, "%s: out of memory\n", o.progname);
        return 1;
    }

    set_default_algorithm(&o);

    opterr = 0;
    if (o.compat == SUM_COMPAT_GNU) {
        while ((opt = getopt_long(argc, argv, "+:a:o:pqrs", longopts, NULL)) != -1) {
            switch (opt) {
            case 'a':
            case 1:
                if (parse_algorithm_list(&o, optarg) != 0) {
                    fprintf(stderr, "%s: invalid algorithm '%s'\n", o.progname, optarg);
                    free(o.strings);
                    return 1;
                }
                break;
            case 'o':
                if (strcmp(optarg, "1") == 0) {
                    if (replace_algorithms_with(&o, SUM_ALG_OLD1) != 0) {
                        fprintf(stderr, "%s: algorithm selection overflow\n", o.progname);
                        free(o.strings);
                        return 1;
                    }
                } else if (strcmp(optarg, "2") == 0) {
                    if (replace_algorithms_with(&o, SUM_ALG_OLD2) != 0) {
                        fprintf(stderr, "%s: algorithm selection overflow\n", o.progname);
                        free(o.strings);
                        return 1;
                    }
                } else {
                    fprintf(stderr, "%s: invalid value for -o: %s\n", o.progname, optarg);
                    free(o.strings);
                    return 1;
                }
                break;
            case 'p':
                o.echo_stdin = true;
                break;
            case 'q':
                o.quiet = true;
                break;
            case 'r':
                if (replace_algorithms_with(&o, SUM_ALG_OLD1) != 0) {
                    fprintf(stderr, "%s: algorithm selection overflow\n", o.progname);
                    free(o.strings);
                    return 1;
                }
                break;
            case 's':
            case 5:
                if (replace_algorithms_with(&o, SUM_ALG_OLD2) != 0) {
                    fprintf(stderr, "%s: algorithm selection overflow\n", o.progname);
                    free(o.strings);
                    return 1;
                }
                break;
            case 2: {
                sum_compat_t parsed;
                if (parse_compat_value(optarg, &parsed) != 0 || parsed != o.compat) {
                    fprintf(stderr, "%s: --compat must be set to bsd or gnu before conflicting short options\n", o.progname);
                    free(o.strings);
                    return 1;
                }
                break;
            }
            case 3:
                print_help(&o);
                free(o.strings);
                return 0;
            case 4:
                if (add_string_input(&o, optarg) != 0) {
                    fprintf(stderr, "%s: too many -s/--string values\n", o.progname);
                    free(o.strings);
                    return 1;
                }
                break;
            case 6:
                print_version();
                free(o.strings);
                return 0;
            case ':':
                fprintf(stderr, "%s: option requires an argument\n", o.progname);
                free(o.strings);
                return 1;
            case '?':
            default:
                usage(stderr, o.progname);
                free(o.strings);
                return 1;
            }
        }
    } else {
        while ((opt = getopt_long(argc, argv, "+:a:o:pqrs:", longopts, NULL)) != -1) {
            switch (opt) {
            case 'a':
            case 1:
                if (parse_algorithm_list(&o, optarg) != 0) {
                    fprintf(stderr, "%s: invalid algorithm '%s'\n", o.progname, optarg);
                    free(o.strings);
                    return 1;
                }
                break;
            case 'o':
                if (strcmp(optarg, "1") == 0) {
                    if (replace_algorithms_with(&o, SUM_ALG_OLD1) != 0) {
                        fprintf(stderr, "%s: algorithm selection overflow\n", o.progname);
                        free(o.strings);
                        return 1;
                    }
                } else if (strcmp(optarg, "2") == 0) {
                    if (replace_algorithms_with(&o, SUM_ALG_OLD2) != 0) {
                        fprintf(stderr, "%s: algorithm selection overflow\n", o.progname);
                        free(o.strings);
                        return 1;
                    }
                } else {
                    fprintf(stderr, "%s: invalid value for -o: %s\n", o.progname, optarg);
                    free(o.strings);
                    return 1;
                }
                break;
            case 'p':
                o.echo_stdin = true;
                break;
            case 'q':
                o.quiet = true;
                break;
            case 'r':
                o.reverse = true;
                break;
            case 's':
            case 4:
                if (add_string_input(&o, optarg) != 0) {
                    fprintf(stderr, "%s: too many -s/--string values\n", o.progname);
                    free(o.strings);
                    return 1;
                }
                break;
            case 5:
                if (replace_algorithms_with(&o, SUM_ALG_OLD2) != 0) {
                    fprintf(stderr, "%s: algorithm selection overflow\n", o.progname);
                    free(o.strings);
                    return 1;
                }
                break;
            case 2: {
                sum_compat_t parsed;
                if (parse_compat_value(optarg, &parsed) != 0 || parsed != o.compat) {
                    fprintf(stderr, "%s: --compat must be set to bsd or gnu before conflicting short options\n", o.progname);
                    free(o.strings);
                    return 1;
                }
                break;
            }
            case 3:
                print_help(&o);
                free(o.strings);
                return 0;
            case 6:
                print_version();
                free(o.strings);
                return 0;
            case ':':
                fprintf(stderr, "%s: option requires an argument\n", o.progname);
                free(o.strings);
                return 1;
            case '?':
            default:
                usage(stderr, o.progname);
                free(o.strings);
                return 1;
            }
        }
    }

    for (i = 0; i < (int)o.string_count; i++) {
        if (process_string(&o, o.strings[i]) != 0) {
            ret = 1;
        }
    }

    if (optind < argc) {
        for (i = optind; i < argc; i++) {
            if (strcmp(argv[i], "-") == 0) {
                if (process_stdin_stream(&o, true, "-", o.echo_stdin) != 0) {
                    ret = 1;
                }
            } else {
                if (process_file(&o, argv[i]) != 0) {
                    ret = 1;
                }
            }
        }
    } else if (o.string_count == 0) {
        if (process_stdin_stream(&o, false, NULL, o.echo_stdin) != 0) {
            ret = 1;
        }
    }

    free(o.strings);
    return ret;
}
