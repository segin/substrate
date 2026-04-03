#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ls_opts.h"

typedef struct {
    int argc;
    char **argv;
    int index;
    bool stop_options;
} ls_parse_state_t;

static void set_output_mode(ls_config_t *config, char mode) {
    config->one_per_line = false;
    config->multi_column = false;
    config->comma_sep = false;
    config->by_lines = false;

    switch (mode) {
        case '1':
            config->one_per_line = true;
            break;
        case 'C':
            config->multi_column = true;
            break;
        case 'm':
            config->comma_sep = true;
            break;
        case 'x':
            config->multi_column = true;
            config->by_lines = true;
            break;
        default:
            break;
    }
}

static void init_config(ls_config_t *config) {
    memset(config, 0, sizeof(*config));
    config->multi_column = isatty(STDOUT_FILENO) != 0;
    config->color = LS_COLOR_AUTO;
    config->time_type = TIME_MTIME;
    config->time_style = TIME_STYLE_LOCALE;
    config->quoting_style = LS_QUOTE_LITERAL;
}

static int parse_positive_int(const char *text, int *out_value) {
    char *end = NULL;
    long value;

    if (text == NULL || *text == '\0') {
        return -1;
    }

    value = strtol(text, &end, 10);
    if (*end != '\0' || value <= 0 || value > INT_MAX) {
        return -1;
    }

    *out_value = (int)value;
    return 0;
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

    if (*end != '\0') {
        if (end[1] != '\0') {
            return -1;
        }
        switch (*end) {
            case 'K':
            case 'k':
                mult = 1024LL;
                break;
            case 'M':
            case 'm':
                mult = 1024LL * 1024LL;
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
    return 0;
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

static const char *consume_next_arg(ls_parse_state_t *state, const char *opt_name) {
    if (state->index + 1 >= state->argc) {
        fprintf(stderr, "ls: option '%s' requires an argument\n", opt_name);
        return NULL;
    }
    state->index++;
    return state->argv[state->index];
}

static const char *consume_long_arg(ls_parse_state_t *state, const char *opt_name, const char *inline_value) {
    if (inline_value != NULL) {
        return inline_value;
    }
    return consume_next_arg(state, opt_name);
}

static const char *consume_short_arg(ls_parse_state_t *state, const char *opt_name,
                                     const char *short_arg, size_t *pos) {
    if (short_arg[*pos + 1] != '\0') {
        const char *value = short_arg + *pos + 1;
        *pos = strlen(short_arg) - 1;
        return value;
    }
    return consume_next_arg(state, opt_name);
}

static int parse_and_set_width(ls_config_t *config, const char *text, const char *opt_name) {
    int width;
    if (parse_positive_int(text, &width) != 0) {
        fprintf(stderr, "ls: invalid %s value '%s'\n", opt_name, text ? text : "");
        return -1;
    }
    config->term_width = width;
    return 0;
}

static int parse_long_option(ls_parse_state_t *state, ls_config_t *config, const char *arg) {
    const char *name = arg + 2;
    const char *value = NULL;
    const char *eq = strchr(name, '=');
    size_t name_len = eq ? (size_t)(eq - name) : strlen(name);

    if (eq != NULL) {
        value = eq + 1;
    }

    if (name_len == 3 && strncmp(name, "all", 3) == 0) {
        config->all = true;
        return 0;
    }
    if (name_len == 10 && strncmp(name, "almost-all", 10) == 0) {
        config->almost_all = true;
        return 0;
    }
    if (name_len == 9 && strncmp(name, "directory", 9) == 0) {
        config->directory = true;
        return 0;
    }
    if (name_len == 6 && strncmp(name, "ignore", 6) == 0) {
        value = consume_long_arg(state, "--ignore", value);
        if (value == NULL || *value == '\0') {
            if (value != NULL) {
                fprintf(stderr, "ls: option '--ignore' requires an argument\n");
            }
            return -1;
        }
        config->ignore_pattern = value;
        return 0;
    }
    if (name_len == 8 && strncmp(name, "classify", 8) == 0) {
        config->classify = true;
        return 0;
    }
    if (name_len == 14 && strncmp(name, "human-readable", 14) == 0) {
        config->human_readable = true;
        return 0;
    }
    if (name_len == 5 && strncmp(name, "inode", 5) == 0) {
        config->inode = true;
        return 0;
    }
    if (name_len == 9 && strncmp(name, "kibibytes", 9) == 0) {
        config->kibibytes = true;
        return 0;
    }
    if (name_len == 11 && strncmp(name, "dereference", 11) == 0) {
        config->dereference = true;
        return 0;
    }
    if (name_len == 15 && strncmp(name, "numeric-uid-gid", 15) == 0) {
        config->numeric_ids = true;
        config->long_fmt = true;
        return 0;
    }
    if (name_len == 10 && strncmp(name, "quote-name", 10) == 0) {
        config->quote_names = true;
        return 0;
    }
    if (name_len == 7 && strncmp(name, "reverse", 7) == 0) {
        config->reverse = true;
        return 0;
    }
    if (name_len == 9 && strncmp(name, "recursive", 9) == 0) {
        config->recursive = true;
        return 0;
    }
    if (name_len == 15 && strncmp(name, "one-file-system", 15) == 0) {
        config->one_file_system = true;
        return 0;
    }
    if (name_len == 4 && strncmp(name, "hide", 4) == 0) {
        value = consume_long_arg(state, "--hide", value);
        if (value == NULL || *value == '\0') {
            if (value != NULL) {
                fprintf(stderr, "ls: option '--hide' requires an argument\n");
            }
            return -1;
        }
        config->hide_pattern = value;
        return 0;
    }
    if (name_len == 4 && strncmp(name, "size", 4) == 0) {
        config->show_blocks = true;
        return 0;
    }
    if (name_len == 2 && strncmp(name, "si", 2) == 0) {
        config->si_units = true;
        return 0;
    }
    if (name_len == 9 && strncmp(name, "file-type", 9) == 0) {
        config->file_type = true;
        return 0;
    }
    if (name_len == 11 && strncmp(name, "ignore-case", 11) == 0) {
        config->sort_ignore_case = true;
        return 0;
    }
    if (name_len == 23 && strncmp(name, "group-directories-first", 23) == 0) {
        config->dirs_first = true;
        return 0;
    }
    if (name_len == 10 && strncmp(name, "block-size", 10) == 0) {
        long size;
        value = consume_long_arg(state, "--block-size", value);
        if (value == NULL || parse_block_size(value, &size) != 0) {
            fprintf(stderr, "ls: invalid --block-size value '%s'\n", value ? value : "");
            return -1;
        }
        config->block_size = size;
        return 0;
    }
    if (name_len == 5 && strncmp(name, "width", 5) == 0) {
        value = consume_long_arg(state, "--width", value);
        if (value == NULL) {
            return -1;
        }
        return parse_and_set_width(config, value, "--width");
    }
    if (name_len == 4 && strncmp(name, "time", 4) == 0) {
        value = consume_long_arg(state, "--time", value);
        if (value == NULL || parse_time_type(value, &config->time_type) != 0) {
            fprintf(stderr, "ls: invalid --time value '%s'\n", value ? value : "");
            return -1;
        }
        return 0;
    }
    if (name_len == 10 && strncmp(name, "time-style", 10) == 0) {
        value = consume_long_arg(state, "--time-style", value);
        if (value == NULL || parse_time_style(value, config) != 0) {
            fprintf(stderr, "ls: invalid --time-style value '%s'\n", value ? value : "");
            return -1;
        }
        return 0;
    }
    if (name_len == 5 && strncmp(name, "color", 5) == 0) {
        if (value == NULL || *value == '\0') {
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
        return 0;
    }
    if (name_len == 7 && strncmp(name, "literal", 7) == 0) {
        config->literal = true;
        return 0;
    }
    if (name_len == 18 && strncmp(name, "hide-control-chars", 18) == 0) {
        config->hide_control_chars = true;
        return 0;
    }
    if (name_len == 18 && strncmp(name, "show-control-chars", 18) == 0) {
        config->hide_control_chars = false;
        return 0;
    }
    if (name_len == 13 && strncmp(name, "quoting-style", 13) == 0) {
        ls_quote_mode_t mode;
        value = consume_long_arg(state, "--quoting-style", value);
        if (value == NULL || parse_quoting_style(value, &mode) != 0) {
            fprintf(stderr, "ls: invalid --quoting-style value '%s'\n", value ? value : "");
            return -1;
        }
        config->quoting_style = mode;
        return 0;
    }
    if (name_len == 4 && strncmp(name, "help", 4) == 0) {
        ls_print_usage(state->argv[0]);
        config->show_help = true;
        return 1;
    }
    if (name_len == 7 && strncmp(name, "version", 7) == 0) {
        printf("ls (Substrate coreutils) 1.1\n");
        config->show_version = true;
        return 1;
    }

    fprintf(stderr, "ls: unrecognized option '%s'\n", arg);
    return -1;
}

static int parse_short_options(ls_parse_state_t *state, ls_config_t *config, const char *arg) {
    size_t pos;

    for (pos = 1; arg[pos] != '\0'; pos++) {
        switch (arg[pos]) {
            case '1':
                set_output_mode(config, '1');
                break;
            case '@':
                config->list_xattr_names = true;
                break;
            case 'A':
                config->almost_all = true;
                break;
            case 'C':
                set_output_mode(config, 'C');
                break;
            case 'F':
                config->classify = true;
                break;
            case 'H':
                config->dereference_args = true;
                break;
            case 'I': {
                const char *value = consume_short_arg(state, "-I", arg, &pos);
                if (value == NULL || *value == '\0') {
                    if (value != NULL) {
                        fprintf(stderr, "ls: option requires an argument -- 'I'\n");
                    }
                    return -1;
                }
                config->ignore_pattern = value;
                break;
            }
            case 'L':
                config->dereference = true;
                break;
            case 'N':
                config->literal = true;
                break;
            case 'Q':
                config->quote_names = true;
                break;
            case 'R':
                config->recursive = true;
                break;
            case 'S':
                config->sort_size = true;
                break;
            case 'U':
                config->no_sort = true;
                break;
            case 'X':
                config->sort_extension = true;
                break;
            case 'a':
                config->all = true;
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
            case 'g':
                config->long_fmt = true;
                config->no_owner = true;
                break;
            case 'h':
                config->human_readable = true;
                break;
            case 'i':
                config->inode = true;
                break;
            case 'k':
                config->kibibytes = true;
                break;
            case 'l':
                config->long_fmt = true;
                break;
            case 'm':
                set_output_mode(config, 'm');
                break;
            case 'n':
                config->numeric_ids = true;
                config->long_fmt = true;
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
            case 'r':
                config->reverse = true;
                break;
            case 's':
                config->show_blocks = true;
                break;
            case 't':
                config->sort_time = true;
                break;
            case 'u':
                config->time_type = TIME_ATIME;
                break;
            case 'v':
                config->version_sort = true;
                break;
            case 'w': {
                const char *value = consume_short_arg(state, "-w", arg, &pos);
                if (value == NULL) {
                    return -1;
                }
                if (parse_and_set_width(config, value, "-w") != 0) {
                    return -1;
                }
                break;
            }
            case 'x':
                set_output_mode(config, 'x');
                break;
            default:
                fprintf(stderr, "ls: invalid option -- '%c'\n", arg[pos]);
                return -1;
        }
    }

    return 0;
}

static void normalize_config(ls_config_t *config) {
    if (config->quote_names) {
        config->quoting_style = LS_QUOTE_C;
    }

    if (config->literal) {
        config->quoting_style = LS_QUOTE_LITERAL;
    }

    if (config->file_type) {
        config->classify = false;
    }
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
    printf("  -X                         sort by file extension\n");
    printf("  -t                         sort by time, newest first\n");
    printf("  -u                         with -lt: sort by, and show, access time\n");
    printf("  -c                         with -lt: sort by, and show, status change time\n");
    printf("  -U                         do not sort; list entries in directory order\n");
    printf("  -f                         do not sort, enable -aU, disable -ls and color\n");
    printf("  -v                         natural sort of version numbers within text\n");
    printf("      --ignore-case          ignore case while sorting names\n");
    printf("      --group-directories-first\n");
    printf("                             group directories before files\n");
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
    printf("      --one-file-system      do not recurse across filesystem boundaries\n");
    printf("      --hide=PATTERN         hide implied entries matching PATTERN in -R\n");
    printf("      --color=WHEN           colorize output; WHEN=never,auto,always\n");
    printf("  -w, --width=COLS           set output width\n");
    printf("      --time=WORD            use specified time: atime, ctime, mtime\n");
    printf("      --time-style=STYLE     style: full-iso,long-iso,iso,locale,+FORMAT\n");
    printf("  -q, --hide-control-chars   print ? for nongraphic chars\n");
    printf("      --show-control-chars   show control chars as-is (default)\n");
    printf("  -N, --literal              print raw entry names\n");
    printf("  -Q, --quote-name           enclose entry names in double quotes\n");
    printf("  -@                         display extended attribute names\n");
    printf("      --quoting-style=WORD   literal,shell,shell-always,c,escape\n");
    printf("      --help                 display this help and exit\n");
    printf("      --version              output version information and exit\n");
}

int ls_parse_opts(int argc, char **argv, ls_config_t *config, char ***files, int *file_count) {
    ls_parse_state_t state;

    state.argc = argc;
    state.argv = argv;
    state.index = 1;
    state.stop_options = false;

    init_config(config);

    *file_count = 0;
    *files = (char **)malloc(sizeof(char *) * (size_t)(argc > 0 ? argc : 1));
    if (*files == NULL) {
        fprintf(stderr, "ls: out of memory\n");
        return -1;
    }

    while (state.index < state.argc) {
        char *arg = state.argv[state.index];
        int rc;

        if (!state.stop_options && strcmp(arg, "--") == 0) {
            state.stop_options = true;
            state.index++;
            continue;
        }

        if (!state.stop_options && arg[0] == '-' && arg[1] != '\0') {
            if (arg[1] == '-') {
                rc = parse_long_option(&state, config, arg);
            } else {
                rc = parse_short_options(&state, config, arg);
            }
            if (rc != 0) {
                return rc;
            }
        } else {
            (*files)[(*file_count)++] = arg;
        }

        state.index++;
    }

    normalize_config(config);
    return 0;
}
