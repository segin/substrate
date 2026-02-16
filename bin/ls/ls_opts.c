#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ls_opts.h"

void ls_print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [OPTION]... [FILE]...\n", prog);
    fprintf(stderr, "List information about the FILEs (the current directory by default).\n\n");
    fprintf(stderr, "  -a, --all             do not ignore entries starting with .\n");
    fprintf(stderr, "  -A, --almost-all      do not list implied . and ..\n");
    fprintf(stderr, "  -C                    list entries by columns\n");
    fprintf(stderr, "  -d, --directory       list directories themselves, not their contents\n");
    fprintf(stderr, "  -F, --classify        append indicator (one of */=>@|) to entries\n");
    fprintf(stderr, "  -g                    like -l, but do not list owner\n");
    fprintf(stderr, "  -h, --human-readable  print sizes in human readable format\n");
    fprintf(stderr, "  -H                    follow symbolic links listed on command line\n");
    fprintf(stderr, "  -i, --inode           print inode numbers\n");
    fprintf(stderr, "  -I, --ignore=PATTERN  do not list entries matching PATTERN\n");
    fprintf(stderr, "  -k, --kibibytes       use 1024-byte blocks\n");
    fprintf(stderr, "  -l                    use a long listing format\n");
    fprintf(stderr, "  -L, --dereference     show info for linked target, not link itself\n");
    fprintf(stderr, "  -m                    fill width with comma-separated entries\n");
    fprintf(stderr, "  -n, --numeric-uid-gid like -l, but list numeric uid/gid\n");
    fprintf(stderr, "  -o                    like -l, but do not list group\n");
    fprintf(stderr, "  -p                    append / indicator to directories\n");
    fprintf(stderr, "  -Q, --quote-name      enclose entry names in double quotes\n");
    fprintf(stderr, "  -r, --reverse         reverse order while sorting\n");
    fprintf(stderr, "  -R, --recursive       list subdirectories recursively\n");
    fprintf(stderr, "  -s, --size            print allocated size of each file, in blocks\n");
    fprintf(stderr, "  -S                    sort by file size, largest first\n");
    fprintf(stderr, "      --si              use powers of 1000 not 1024\n");
    fprintf(stderr, "  -t                    sort by modification time, newest first\n");
    fprintf(stderr, "  -u                    with -lt: sort by and show atime\n");
    fprintf(stderr, "  -U                    do not sort; list entries in directory order\n");
    fprintf(stderr, "  -v                    natural sort of (version) numbers within text\n");
    fprintf(stderr, "  -x                    list entries by lines instead of by columns\n");
    fprintf(stderr, "  -1                    list one file per line\n");
    fprintf(stderr, "      --color[=WHEN]    colorize output; WHEN=always/never/auto\n");
}

int ls_parse_opts(int argc, char **argv, ls_config_t *config, char ***files, int *file_count) {
    memset(config, 0, sizeof(ls_config_t));
    config->color = isatty(STDOUT_FILENO) ? 1 : 0;
    config->multi_column = isatty(STDOUT_FILENO);

    *files = NULL;
    *file_count = 0;
    size_t capacity = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            if (argv[i][1] == '-') {
                // Long options
                if (strncmp(argv[i], "--color", 7) == 0) {
                    char *val = strchr(argv[i], '=');
                    if (val) {
                        val++;
                        if (strcmp(val, "always") == 0) config->color = 2;
                        else if (strcmp(val, "never") == 0) config->color = 0;
                        else if (strcmp(val, "auto") == 0) config->color = isatty(STDOUT_FILENO) ? 1 : 0;
                    } else {
                        config->color = 2;
                    }
                } else if (strncmp(argv[i], "--ignore=", 9) == 0) {
                    config->ignore_pattern = argv[i] + 9;
                } else if (strcmp(argv[i], "--all") == 0) config->all = true;
                else if (strcmp(argv[i], "--almost-all") == 0) config->almost_all = true;
                else if (strcmp(argv[i], "--directory") == 0) config->directory = true;
                else if (strcmp(argv[i], "--classify") == 0) config->classify = true;
                else if (strcmp(argv[i], "--human-readable") == 0) config->human_readable = true;
                else if (strcmp(argv[i], "--inode") == 0) config->inode = true;
                else if (strcmp(argv[i], "--kibibytes") == 0) config->kibibytes = true;
                else if (strcmp(argv[i], "--dereference") == 0) config->dereference = true;
                else if (strcmp(argv[i], "--numeric-uid-gid") == 0) { config->numeric_ids = true; config->long_fmt = true; }
                else if (strcmp(argv[i], "--quote-name") == 0) config->quote_names = true;
                else if (strcmp(argv[i], "--reverse") == 0) config->reverse = true;
                else if (strcmp(argv[i], "--recursive") == 0) config->recursive = true;
                else if (strncmp(argv[i], "--hide=", 7) == 0) config->hide_pattern = argv[i] + 7;
                else if (strcmp(argv[i], "--size") == 0) config->show_blocks = true;
                else if (strcmp(argv[i], "--si") == 0) config->si_units = true;
                else if (strcmp(argv[i], "--file-type") == 0) config->file_type = true;
                else if (strncmp(argv[i], "--block-size=", 13) == 0) {
                    char *val = argv[i] + 13;
                    long size = 1;
                    if (*val >= '0' && *val <= '9') {
                        size = atol(val);
                    } else {
                        // Parse suffix: K, M, G, T
                        switch (*val) {
                            case 'K': case 'k': size = 1024; break;
                            case 'M': case 'm': size = 1024 * 1024; break;
                            case 'G': case 'g': size = 1024L * 1024 * 1024; break;
                            default: size = 1; break;
                        }
                    }
                    config->block_size = size > 0 ? size : 1;
                }
                else if (strncmp(argv[i], "--width=", 8) == 0) {
                    config->term_width = atoi(argv[i] + 8);
                }
                else if (strncmp(argv[i], "--time=", 7) == 0) {
                    char *val = argv[i] + 7;
                    if (strcmp(val, "atime") == 0 || strcmp(val, "access") == 0 || strcmp(val, "use") == 0)
                        config->time_type = TIME_ATIME;
                    else if (strcmp(val, "ctime") == 0 || strcmp(val, "status") == 0)
                        config->time_type = TIME_CTIME;
                    else if (strcmp(val, "mtime") == 0 || strcmp(val, "modification") == 0)
                        config->time_type = TIME_MTIME;
                }
                else if (strncmp(argv[i], "--time-style=", 13) == 0) {
                    char *val = argv[i] + 13;
                    if (strcmp(val, "full-iso") == 0) config->time_style = TIME_STYLE_FULL_ISO;
                    else if (strcmp(val, "long-iso") == 0) config->time_style = TIME_STYLE_LONG_ISO;
                    else if (strcmp(val, "iso") == 0) config->time_style = TIME_STYLE_ISO;
                    else if (strcmp(val, "locale") == 0) config->time_style = TIME_STYLE_LOCALE;
                }
                else if (strcmp(argv[i], "--literal") == 0) config->literal = true;
                else if (strcmp(argv[i], "--hide-control-chars") == 0) config->hide_control_chars = true;
                else if (strcmp(argv[i], "--show-control-chars") == 0) config->hide_control_chars = false;
                else if (strncmp(argv[i], "--quoting-style=", 16) == 0) {
                    char *val = argv[i] + 16;
                    if (strcmp(val, "literal") == 0) config->quoting_style = 0;
                    else if (strcmp(val, "shell") == 0) config->quoting_style = 1;
                    else if (strcmp(val, "shell-always") == 0) config->quoting_style = 2;
                    else if (strcmp(val, "c") == 0) config->quoting_style = 3;
                    else if (strcmp(val, "escape") == 0) config->quoting_style = 4;
                }
                else if (strcmp(argv[i], "--help") == 0) {
                    printf("Usage: ls [OPTION]... [FILE]...\n");
                    printf("List information about the FILEs (the current directory by default).\n\n");
                    printf("  -a, --all              do not ignore entries starting with .\n");
                    printf("  -A, --almost-all       do not list implied . and ..\n");
                    printf("  -l                     use a long listing format\n");
                    printf("  -h, --human-readable   print sizes in human readable format\n");
                    printf("  -R, --recursive        list subdirectories recursively\n");
                    printf("  --color=WHEN           colorize output (never, auto, always)\n");
                    printf("  --help                 display this help and exit\n");
                    printf("  --version              output version information and exit\n");
                    return 1; // Signal to exit after help
                }
                else if (strcmp(argv[i], "--version") == 0) {
                    printf("ls (Substrate coreutils) 1.0\n");
                    return 1; // Signal to exit after version
                }
                else {
                    fprintf(stderr, "ls: unrecognized option '%s'\n", argv[i]);
                    return -1;
                }
            } else {
                // Short options
                for (int j = 1; argv[i][j]; j++) {
                    char opt = argv[i][j];
                    switch (opt) {
                        case 'a': config->all = true; break;
                        case 'A': config->almost_all = true; break;
                        case 'C': config->multi_column = true; config->one_per_line = false; break;
                        case 'c': config->time_type = TIME_CTIME; break;
                        case 'd': config->directory = true; break;
                        case 'f': config->no_sort = true; config->all = true; config->color = 0; break;
                        case 'F': config->classify = true; break;
                        case 'g': config->long_fmt = true; config->no_owner = true; break;
                        case 'h': config->human_readable = true; break;
                        case 'H': config->dereference_args = true; break;
                        case 'i': config->inode = true; break;
                        case 'I':
                            if (argv[i][j+1]) {
                                config->ignore_pattern = &argv[i][j+1];
                                j = strlen(argv[i]) - 1; // skip rest
                            } else if (i + 1 < argc) {
                                config->ignore_pattern = argv[++i];
                            }
                            break;
                        case 'k': config->kibibytes = true; break;
                        case 'l': config->long_fmt = true; break;
                        case 'L': config->dereference = true; break;
                        case 'm': config->comma_sep = true; break;
                        case 'n': config->numeric_ids = true; config->long_fmt = true; break;
                        case 'N': config->literal = true; break;
                        case 'o': config->long_fmt = true; config->no_group = true; break;
                        case 'p': config->slash_dirs = true; break;
                        case 'q': config->hide_control_chars = true; break;
                        case 'Q': config->quote_names = true; break;
                        case 'r': config->reverse = true; break;
                        case 'R': config->recursive = true; break;
                        case 's': config->show_blocks = true; break;
                        case 'S': config->sort_size = true; break;
                        case 't': config->sort_time = true; break;
                        case 'u': config->time_type = TIME_ATIME; break;
                        case 'U': config->no_sort = true; break;
                        case 'v': config->version_sort = true; break;
                        case 'w':
                            if (argv[i][j+1]) {
                                config->term_width = atoi(&argv[i][j+1]);
                                j = strlen(argv[i]) - 1;
                            } else if (i + 1 < argc) {
                                config->term_width = atoi(argv[++i]);
                            }
                            break;
                        case 'x': config->by_lines = true; break;
                        case '1': config->one_per_line = true; config->multi_column = false; break;
                        default:
                            fprintf(stderr, "ls: invalid option -- '%c'\n", opt);
                            return -1;
                    }
                }
            }
        } else {
            if (*file_count >= capacity) {
                size_t new_capacity = capacity == 0 ? 16 : capacity * 2;
                char **temp = realloc(*files, sizeof(char*) * new_capacity);
                if (!temp) return -1; // OOM
                *files = temp;
                capacity = new_capacity;
            }
            (*files)[(*file_count)++] = argv[i];
        }
    }
    return 0;
}
