#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "ls_opts.h"

void ls_print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [-aAlhRrSt] [--color[=always|never|auto]] [FILE...]\n", prog);
}

int ls_parse_opts(int argc, char **argv, ls_config_t *config, char ***files, int *file_count) {
    memset(config, 0, sizeof(ls_config_t));
    config->color = isatty(STDOUT_FILENO) ? 1 : 0;

    *files = NULL;
    *file_count = 0;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-') {
            if (argv[i][1] == '-') {
                if (strncmp(argv[i], "--color", 7) == 0) {
                    char *val = strchr(argv[i], '=');
                    if (val) {
                        val++;
                        if (strcmp(val, "always") == 0) config->color = 2;
                        else if (strcmp(val, "never") == 0) config->color = 0;
                        else if (strcmp(val, "auto") == 0) config->color = isatty(STDOUT_FILENO) ? 1 : 0;
                    } else {
                        config->color = 1;
                    }
                } else if (strcmp(argv[i], "--all") == 0) config->all = true;
                else if (strcmp(argv[i], "--almost-all") == 0) config->almost_all = true;
                else if (strcmp(argv[i], "--human-readable") == 0) config->human_readable = true;
                else if (strcmp(argv[i], "--recursive") == 0) config->recursive = true;
                else if (strcmp(argv[i], "--reverse") == 0) config->reverse = true;
                else {
                    fprintf(stderr, "Unknown option: %s\n", argv[i]);
                    return -1;
                }
            } else {
                for (int j = 1; argv[i][j]; j++) {
                    switch (argv[i][j]) {
                        case 'a': config->all = true; break;
                        case 'A': config->almost_all = true; break;
                        case 'l': config->long_fmt = true; break;
                        case 'h': config->human_readable = true; break;
                        case 'R': config->recursive = true; break;
                        case 'r': config->reverse = true; break;
                        case 'S': config->sort_size = true; break;
                        case 't': config->sort_time = true; break;
                        case 'F': config->classify = true; break;
                        case 'i': config->inode = true; break;
                        case 'd': config->directory = true; break;
                        case 'n': config->numeric_ids = true; config->long_fmt = true; break;
                        case 'G': config->no_group = true; break;
                        case '1': config->one_per_line = true; break;
                        default:
                            fprintf(stderr, "Unknown option: -%c\n", argv[i][j]);
                            return -1;
                    }
                }
            }
        } else {
            *files = realloc(*files, sizeof(char*) * (*file_count + 1));
            if (!*files) return -1; // OOM
            (*files)[(*file_count)++] = argv[i];
        }
    }
    return 0;
}
