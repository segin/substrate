#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ls_opts.h"

static void set_output_mode(ls_config_t *config, char mode) {
    config->one_per_line = false;
    config->multi_column = false;
    config->comma_sep = false;
    config->by_lines = false;

    if (mode == '1') {
        config->one_per_line = true;
    } else if (mode == 'C') {
        config->multi_column = true;
    } else if (mode == 'm') {
        config->comma_sep = true;
    } else if (mode == 'x') {
        config->by_lines = true;
        config->multi_column = true;
    }
}

static int parse_block_size(const char *text, long *out_size) {
    char *end = NULL;
    long value;
    long long mult = 1;
    long long scaled;

    if (text == NULL || *text == '\0') {
        return -1;
    }

    if (isdigit((unsigned char)text[0])) {
        value = strtol(text, &end, 10);
        if (value <= 0) {
            return -1;
        }
    } else {
        value = 1;
        end = (char *)text;
    }

    if (end != NULL && *end != '\0') {
        if (end[1] != '\0') {
            return -1;
        }
        switch (*end) {
            case 'K':
            case 'k':
                mult = 1024L;
                break;
            case 'M':
            case 'm':
                mult = 1024L * 1024L;
                break;
            case 'G':
            case 'g':
                mult = 1024LL * 1024LL * 1024LL;
                break;
            case 'T':
            case 't':
                mult = 1024LL * 1024LL * 1024LL * 1024LL;
                break;
            default:
                return -1;
        }
    }

    scaled = (long long)value * mult;
    if (scaled <= 0 || scaled > LONG_MAX) {
        return -1;
    }
    *out_size = (long)scaled;
    return *out_size > 0 ? 0 : -1;
}

static int parse_time_type(const char *word, ls_time_type_t *out) {
    if (strcmp(word, "atime") == 0 || strcmp(word, "access") == 0 || strcmp(word, "use") == 0) {
        *out = TIME_ATIME;
        return 0;
    }
    if (strcmp(word, "ctime") == 0 || strcmp(word, "status") == 0) {
        *out = TIME_CTIME;
        return 0;
    }
    if (strcmp(word, "mtime") == 0 || strcmp(word, "modification") == 0) {
        *out = TIME_MTIME;
        return 0;
    }
    return -1;
}

static int parse_time_style(const char *word, ls_config_t *config) {
    if (strcmp(word, "full-iso") == 0) {
        config->time_style = TIME_STYLE_FULL_ISO;
        config->time_style_format = NULL;
        return 0;
    }
    if (strcmp(word, "long-iso") == 0) {
        config->time_style = TIME_STYLE_LONG_ISO;
        config->time_style_format = NULL;
        return 0;
    }
    if (strcmp(word, "iso") == 0) {
        config->time_style = TIME_STYLE_ISO;
        config->time_style_format = NULL;
        return 0;
    }
    if (strcmp(word, "locale") == 0) {
        config->time_style = TIME_STYLE_LOCALE;
        config->time_style_format = NULL;
        return 0;
    }
    if (word[0] == '+') {
        config->time_style = TIME_STYLE_CUSTOM;
        config->time_style_format = word + 1;
        return 0;
    }
    return -1;
}

static int parse_quoting_style(const char *word, ls_quote_mode_t *out) {
    if (strcmp(word, "literal") == 0) {
        *out = LS_QUOTE_LITERAL;
        return 0;
    }
    if (strcmp(word, "shell") == 0) {
        *out = LS_QUOTE_SHELL;
        return 0;
    }
    if (strcmp(word, "shell-always") == 0) {
        *out = LS_QUOTE_SHELL_ALWAYS;
        return 0;
    }
    if (strcmp(word, "c") == 0) {
        *out = LS_QUOTE_C;
        return 0;
    }
    if (strcmp(word, "escape") == 0) {
        *out = LS_QUOTE_ESCAPE;
        return 0;
    }
    return -1;
}

void ls_print_usage(const char *prog) {
    printf("Usage: %s [OPTION]... [FILE]...\n", prog);
    printf("List information about FILEs (the current directory by default).\n\n");
    printf("  -a, --all                  do not ignore entries starting with .\n");
    printf("  -A, --almost-all           do not list implied . and ..\n");
    printf("  -d, --directory            list directories themselves, not their contents\n");
    printf("  -I, --ignore=PATTERN       do not list implied entries matching PATTERN\n");
    printf("  -l                         use a long listing format\n");
    printf("  -1                         list one file per line\n");
    printf("  -C                         list entries by columns\n");
    printf("  -m                         fill width with comma-separated entries\n");
    printf("  -x                         list entries by lines instead of columns\n");
    printf("  -g                         like -l, but do not list owner\n");
    printf("  -o                         like -l, but do not list group\n");
    printf("  -n, --numeric-uid-gid      like -l, but show numeric user/group IDs\n");
    printf("  -r, --reverse              reverse order while sorting\n");
    printf("  -S                         sort by file size, largest first\n");
    printf("  -t                         sort by time, newest first\n");
    printf("  -u                         with -lt: sort by, and show, access time\n");
    printf("  -c                         with -lt: sort by, and show, status change time\n");
    printf("  -U                         do not sort; list entries in directory order\n");
    printf("  -f                         do not sort, enable -aU, disable -ls and color\n");
    printf("  -v                         natural sort of version numbers within text\n");
    printf("  -L, --dereference          follow symlinks and show target metadata\n");
    printf("  -H                         follow symlinks listed on command line only\n");
    printf("  -h, --human-readable       print sizes in human readable format\n");
    printf("  -k, --kibibytes            default to 1KiB blocks\n");
    printf("  -s, --size                 print allocated size in blocks\n");
    printf("      --block-size=SIZE      scale sizes by SIZE\n");
    printf("      --si                   use powers of 1000 not 1024\n");
    printf("  -F, --classify             append file type indicator\n");
    printf("  -p                         append / indicator to directories\n");
    printf("      --file-type            like -F, but do not append *\n");
    printf("  -i, --inode                print inode number of each file\n");
    printf("  -R, --recursive            list subdirectories recursively\n");
    printf("      --hide=PATTERN         hide implied entries matching PATTERN in -R\n");
    printf("      --color=WHEN           colorize output; WHEN=never,auto,always\n");
    printf("  -w, --width=COLS           set output width\n");
    printf("      --time=WORD            use specified time: atime, ctime, mtime\n");
    printf("      --time-style=STYLE     style: full-iso,long-iso,iso,locale,+FORMAT\n");
    printf("  -q, --hide-control-chars   print ? for nongraphic chars\n");
    printf("      --show-control-chars   show control chars as-is (default)\n");
    printf("  -N, --literal              print raw entry names\n");
    printf("  -Q, --quote-name           enclose entry names in double quotes\n");
    printf("      --quoting-style=WORD   literal,shell,shell-always,c,escape\n");
    printf("      --help                 display this help and exit\n");
    printf("      --version              output version information and exit\n");
}

int ls_parse_opts(int argc, char **argv, ls_config_t *config, char ***files, int *file_count) {
    int i;
    bool stop_options = false;

    memset(config, 0, sizeof(*config));

    config->multi_column = isatty(STDOUT_FILENO) != 0;
    config->color = LS_COLOR_AUTO;
    config->time_type = TIME_MTIME;
    config->time_style = TIME_STYLE_LOCALE;
    config->time_style_format = NULL;
    config->quoting_style = LS_QUOTE_LITERAL;

    *file_count = 0;
    *files = (char **)malloc(sizeof(char *) * (size_t)(argc > 0 ? argc : 1));
    if (*files == NULL) {
        fprintf(stderr, "ls: out of memory\n");
        return -1;
    }

    for (i = 1; i < argc; i++) {
        char *arg = argv[i];

        if (!stop_options && strcmp(arg, "--") == 0) {
            stop_options = true;
            continue;
        }

        if (!stop_options && arg[0] == '-' && arg[1] != '\0') {
            if (arg[1] == '-') {
                char *value = NULL;
                char *eq = strchr(arg + 2, '=');
                size_t name_len = eq ? (size_t)(eq - (arg + 2)) : strlen(arg + 2);

                if (eq != NULL) {
                    value = eq + 1;
                }

                if (name_len == 3 && strncmp(arg + 2, "all", 3) == 0) {
                    config->all = true;
                } else if (name_len == 10 && strncmp(arg + 2, "almost-all", 10) == 0) {
                    config->almost_all = true;
                } else if (name_len == 9 && strncmp(arg + 2, "directory", 9) == 0) {
                    config->directory = true;
                } else if (name_len == 6 && strncmp(arg + 2, "ignore", 6) == 0) {
                    if (value == NULL && i + 1 < argc) {
                        value = argv[++i];
                    }
                    if (value == NULL || *value == '\0') {
                        fprintf(stderr, "ls: option '--ignore' requires an argument\n");
                        return -1;
                    }
                    config->ignore_pattern = value;
                } else if (name_len == 8 && strncmp(arg + 2, "classify", 8) == 0) {
                    config->classify = true;
                } else if (name_len == 14 && strncmp(arg + 2, "human-readable", 14) == 0) {
                    config->human_readable = true;
                } else if (name_len == 5 && strncmp(arg + 2, "inode", 5) == 0) {
                    config->inode = true;
                } else if (name_len == 9 && strncmp(arg + 2, "kibibytes", 9) == 0) {
                    config->kibibytes = true;
                } else if (name_len == 11 && strncmp(arg + 2, "dereference", 11) == 0) {
                    config->dereference = true;
                } else if (name_len == 15 && strncmp(arg + 2, "numeric-uid-gid", 15) == 0) {
                    config->numeric_ids = true;
                    config->long_fmt = true;
                } else if (name_len == 10 && strncmp(arg + 2, "quote-name", 10) == 0) {
                    config->quote_names = true;
                } else if (name_len == 7 && strncmp(arg + 2, "reverse", 7) == 0) {
                    config->reverse = true;
                } else if (name_len == 9 && strncmp(arg + 2, "recursive", 9) == 0) {
                    config->recursive = true;
                } else if (name_len == 4 && strncmp(arg + 2, "hide", 4) == 0) {
                    if (value == NULL && i + 1 < argc) {
                        value = argv[++i];
                    }
                    if (value == NULL || *value == '\0') {
                        fprintf(stderr, "ls: option '--hide' requires an argument\n");
                        return -1;
                    }
                    config->hide_pattern = value;
                } else if (name_len == 4 && strncmp(arg + 2, "size", 4) == 0) {
                    config->show_blocks = true;
                } else if (name_len == 2 && strncmp(arg + 2, "si", 2) == 0) {
                    config->si_units = true;
                } else if (name_len == 9 && strncmp(arg + 2, "file-type", 9) == 0) {
                    config->file_type = true;
                } else if (name_len == 10 && strncmp(arg + 2, "block-size", 10) == 0) {
                    long size;
                    if (value == NULL && i + 1 < argc) {
                        value = argv[++i];
                    }
                    if (parse_block_size(value, &size) != 0) {
                        fprintf(stderr, "ls: invalid --block-size value '%s'\n", value ? value : "");
                        return -1;
                    }
                    config->block_size = size;
                } else if (name_len == 5 && strncmp(arg + 2, "width", 5) == 0) {
                    if (value == NULL && i + 1 < argc) {
                        value = argv[++i];
                    }
                    if (value == NULL) {
                        fprintf(stderr, "ls: option '--width' requires an argument\n");
                        return -1;
                    }
                    config->term_width = atoi(value);
                } else if (name_len == 4 && strncmp(arg + 2, "time", 4) == 0) {
                    if (value == NULL && i + 1 < argc) {
                        value = argv[++i];
                    }
                    if (value == NULL || parse_time_type(value, &config->time_type) != 0) {
                        fprintf(stderr, "ls: invalid --time value '%s'\n", value ? value : "");
                        return -1;
                    }
                } else if (name_len == 10 && strncmp(arg + 2, "time-style", 10) == 0) {
                    if (value == NULL && i + 1 < argc) {
                        value = argv[++i];
                    }
                    if (value == NULL || parse_time_style(value, config) != 0) {
                        fprintf(stderr, "ls: invalid --time-style value '%s'\n", value ? value : "");
                        return -1;
                    }
                } else if (name_len == 5 && strncmp(arg + 2, "color", 5) == 0) {
                    if (value == NULL) {
                        config->color = LS_COLOR_ALWAYS;
                    } else if (strcmp(value, "always") == 0) {
                        config->color = LS_COLOR_ALWAYS;
                    } else if (strcmp(value, "never") == 0) {
                        config->color = LS_COLOR_NEVER;
                    } else if (strcmp(value, "auto") == 0) {
                        config->color = LS_COLOR_AUTO;
                    } else {
                        fprintf(stderr, "ls: invalid --color value '%s'\n", value);
                        return -1;
                    }
                } else if (name_len == 7 && strncmp(arg + 2, "literal", 7) == 0) {
                    config->literal = true;
                } else if (name_len == 18 && strncmp(arg + 2, "hide-control-chars", 18) == 0) {
                    config->hide_control_chars = true;
                } else if (name_len == 18 && strncmp(arg + 2, "show-control-chars", 18) == 0) {
                    config->hide_control_chars = false;
                } else if (name_len == 13 && strncmp(arg + 2, "quoting-style", 13) == 0) {
                    ls_quote_mode_t qm;
                    if (value == NULL && i + 1 < argc) {
                        value = argv[++i];
                    }
                    if (value == NULL || parse_quoting_style(value, &qm) != 0) {
                        fprintf(stderr, "ls: invalid --quoting-style value '%s'\n", value ? value : "");
                        return -1;
                    }
                    config->quoting_style = qm;
                } else if (name_len == 4 && strncmp(arg + 2, "help", 4) == 0) {
                    ls_print_usage(argv[0]);
                    config->show_help = true;
                    return 1;
                } else if (name_len == 7 && strncmp(arg + 2, "version", 7) == 0) {
                    printf("ls (Substrate coreutils) 1.1\n");
                    config->show_version = true;
                    return 1;
                } else {
                    fprintf(stderr, "ls: unrecognized option '%s'\n", arg);
                    return -1;
                }
            } else {
                size_t j;
                for (j = 1; arg[j] != '\0'; j++) {
                    char opt = arg[j];
                    switch (opt) {
                        case 'a':
                            config->all = true;
                            break;
                        case 'A':
                            config->almost_all = true;
                            break;
                        case 'C':
                            set_output_mode(config, 'C');
                            break;
                        case 'c':
                            config->time_type = TIME_CTIME;
                            break;
                        case 'd':
                            config->directory = true;
                            break;
                        case 'f':
                            config->no_sort = true;
                            config->all = true;
                            config->long_fmt = false;
                            config->show_blocks = false;
                            config->color = LS_COLOR_NEVER;
                            break;
                        case 'F':
                            config->classify = true;
                            break;
                        case 'g':
                            config->long_fmt = true;
                            config->no_owner = true;
                            break;
                        case 'h':
                            config->human_readable = true;
                            break;
                        case 'H':
                            config->dereference_args = true;
                            break;
                        case 'i':
                            config->inode = true;
                            break;
                        case 'I': {
                            const char *v = NULL;
                            if (arg[j + 1] != '\0') {
                                v = &arg[j + 1];
                                j = strlen(arg) - 1;
                            } else if (i + 1 < argc) {
                                v = argv[++i];
                            }
                            if (v == NULL || *v == '\0') {
                                fprintf(stderr, "ls: option requires an argument -- 'I'\n");
                                return -1;
                            }
                            config->ignore_pattern = v;
                            break;
                        }
                        case 'k':
                            config->kibibytes = true;
                            break;
                        case 'l':
                            config->long_fmt = true;
                            break;
                        case 'L':
                            config->dereference = true;
                            break;
                        case 'm':
                            set_output_mode(config, 'm');
                            break;
                        case 'n':
                            config->numeric_ids = true;
                            config->long_fmt = true;
                            break;
                        case 'N':
                            config->literal = true;
                            break;
                        case 'o':
                            config->long_fmt = true;
                            config->no_group = true;
                            break;
                        case 'p':
                            config->slash_dirs = true;
                            break;
                        case 'q':
                            config->hide_control_chars = true;
                            break;
                        case 'Q':
                            config->quote_names = true;
                            break;
                        case 'r':
                            config->reverse = true;
                            break;
                        case 'R':
                            config->recursive = true;
                            break;
                        case 's':
                            config->show_blocks = true;
                            break;
                        case 'S':
                            config->sort_size = true;
                            break;
                        case 't':
                            config->sort_time = true;
                            break;
                        case 'u':
                            config->time_type = TIME_ATIME;
                            break;
                        case 'U':
                            config->no_sort = true;
                            break;
                        case 'v':
                            config->version_sort = true;
                            break;
                        case 'w': {
                            const char *v = NULL;
                            if (arg[j + 1] != '\0') {
                                v = &arg[j + 1];
                                j = strlen(arg) - 1;
                            } else if (i + 1 < argc) {
                                v = argv[++i];
                            }
                            if (v == NULL || *v == '\0') {
                                fprintf(stderr, "ls: option requires an argument -- 'w'\n");
                                return -1;
                            }
                            config->term_width = atoi(v);
                            break;
                        }
                        case 'x':
                            set_output_mode(config, 'x');
                            break;
                        case '1':
                            set_output_mode(config, '1');
                            break;
                        default:
                            fprintf(stderr, "ls: invalid option -- '%c'\n", opt);
                            return -1;
                    }
                }
            }
        } else {
            (*files)[(*file_count)++] = arg;
        }
    }

    if (config->all) {
        config->almost_all = false;
    }

    if (config->dereference) {
        config->dereference_args = false;
    }

    if (config->literal) {
        config->quoting_style = LS_QUOTE_LITERAL;
        config->quote_names = false;
    }

    if (config->kibibytes && config->block_size == 0) {
        config->block_size = 1024;
    }

    return 0;
}
