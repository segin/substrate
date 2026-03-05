#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <getopt.h>
#include <errno.h>
#include <libjoin.h>

static void usage(FILE *f, const char *progname) {
    fprintf(f, "Usage: %s [options] file1 file2\n", progname);
}

static void add_out_list(join_options_t *o, int file_idx, int field_idx) {
    o->out_list = realloc(o->out_list, (o->out_list_len + 1) * sizeof(*o->out_list));
    if (!o->out_list) {
        perror("realloc out_list");
        exit(1);
    }
    o->out_list[o->out_list_len].file_idx = file_idx;
    o->out_list[o->out_list_len].field_idx = field_idx;
    o->out_list_len++;
}

static void parse_out_list(join_options_t *o, const char *spec_list) {
    /* parse a string like "1.2,2.3 0" or "1.2 2.3,0" */
    char *list_copy = strdup(spec_list);
    char *ptr = list_copy;
    char *tok;
    char *saveptr;

    /* we use strtok_r or manual strchr to avoid strsep availability issues */
    for (tok = strtok_r(ptr, ", \t", &saveptr); tok != NULL; tok = strtok_r(NULL, ", \t", &saveptr)) {
        if (*tok == '\0') continue;

        if (strcmp(tok, "0") == 0) {
            add_out_list(o, 0, 0);
        } else if (tok[0] == '1' && tok[1] == '.') {
            add_out_list(o, 1, atoi(tok + 2));
        } else if (tok[0] == '2' && tok[1] == '.') {
            add_out_list(o, 2, atoi(tok + 2));
        } else {
            fprintf(stderr, "join: invalid field specifier: %s\n", tok);
            exit(1);
        }
    }
    free(list_copy);
}

    join_options_t o;
    memset(&o, 0, sizeof(o));
    o.join_field_1 = 1;
    o.join_field_2 = 1;

    static const struct option longopts[] = {
        {"check-order",   no_argument, NULL, 1},
        {"nocheck-order", no_argument, NULL, 2},
        {"header",        no_argument, NULL, 3},
        {"ignore-case",   no_argument, NULL, 'i'},
        {"zero-terminated",no_argument,NULL, 'z'},
        {NULL, 0, NULL, 0}
    };

    int opt;
    /* -a, -e, -i, -j, -o, -t, -v, -1, -2, -z */
    while ((opt = getopt_long(argc, argv, "+1:2:a:e:ij:o:t:v:z", longopts, NULL)) != -1) {
        switch (opt) {
        case '1':
            o.join_field_1 = atoi(optarg);
            if (o.join_field_1 <= 0) {
                fprintf(stderr, "join: invalid field number: %s\n", optarg);
                return 1;
            }
            break;
        case '2':
            o.join_field_2 = atoi(optarg);
            if (o.join_field_2 <= 0) {
                fprintf(stderr, "join: invalid field number: %s\n", optarg);
                return 1;
            }
            break;
        case 'a':
            if (strcmp(optarg, "1") == 0) o.unpair_1 = true;
            else if (strcmp(optarg, "2") == 0) o.unpair_2 = true;
            else {
                /* BSD legacy: no arg means both files.
                   But because we declared 'a:', getopt consumed the next arg as optarg.
                   We need to push it back. */
                o.unpair_1 = true;
                o.unpair_2 = true;
                optind--;
            }
            break;
        case 'v':
            if (strcmp(optarg, "1") == 0) o.unpair_only_1 = true;
            else if (strcmp(optarg, "2") == 0) o.unpair_only_2 = true;
            else {
                fprintf(stderr, "join: invalid file number: %s\n", optarg);
                return 1;
            }
            break;
        case 'e':
            o.empty_str = optarg;
            break;
        case 'i':
            o.ignore_case = true;
            break;
        case 'j':
            /* Substrate libc getopt parsing trick: -j1 field -> handled if 'j' takes optional arg? 
               Wait, POSIX / BSD short options like -j1 are hard. We can parse string manually,
               but for now just standard -j field. */
            if (optarg && optarg[0] == '1' && optarg[1] == '\0') {
               /* Could be -j 1 field -- oops that's -j1 */
               o.join_field_1 = atoi(optarg); o.join_field_2 = atoi(optarg);
            } else if (optarg && optarg[0] == '2' && optarg[1] == '\0') {
               o.join_field_1 = atoi(optarg); o.join_field_2 = atoi(optarg);
            } else {
               int f = atoi(optarg);
               o.join_field_1 = f;
               o.join_field_2 = f;
            }
            break;
        case 'o':
            if (strcmp(optarg, "auto") == 0) {
                o.auto_format = true;
            } else {
                parse_out_list(&o, optarg);
                /* BSD legacy: consume multiple arguments for -o? 
                   If the next argument doesn't start with '-', and we still have args before files */
                while (optind < argc && argv[optind][0] != '-' && (argc - optind > 2)) {
                    parse_out_list(&o, argv[optind]);
                    optind++;
                }
            }
            break;
        case 't':
            if (optarg[0] == '\0') {
                o.empty_delim = true;
            } else {
                o.delim = optarg[0];
            }
            break;
        case 'z':
            o.zero_terminated = true;
            break;
        case 1:
            o.check_order = 1;
            break;
        case 2:
            o.check_order = -1;
            break;
        case 3:
            o.header = true;
            break;
        default:
            usage(stderr, argv[0]);
            return 1;
        }
    }

    if (argc - optind != 2) {
        fprintf(stderr, "join: wrong arg count. argc=%d optind=%d argv[optind]=%s\n", argc, optind, argv[optind] ? argv[optind] : "null");
        usage(stderr, argv[0]);
        return 1;
    }

    const char *f1_name = argv[optind];
    const char *f2_name = argv[optind + 1];

    FILE *f1 = (strcmp(f1_name, "-") == 0) ? stdin : fopen(f1_name, "r");
    if (!f1) {
        fprintf(stderr, "join: %s: %s\n", f1_name, strerror(errno));
        return 1;
    }

    FILE *f2 = (strcmp(f2_name, "-") == 0) ? stdin : fopen(f2_name, "r");
    if (!f2) {
        fprintf(stderr, "join: %s: %s\n", f2_name, strerror(errno));
        if (f1 != stdin) fclose(f1);
        return 1;
    }

    if (f1 == stdin && f2 == stdin) {
        fprintf(stderr, "join: both files cannot be standard input\n");
        return 1;
    }

    int ret = join_files(f1, f1_name, f2, f2_name, &o, stdout);

    if (f1 != stdin) fclose(f1);
    if (f2 != stdin) fclose(f2);
    free(o.out_list);

    return ret;
}