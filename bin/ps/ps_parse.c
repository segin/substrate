#include <stddef.h>
#include <string.h>

#include "ps_impl.h"

static int parse_cluster(ps_options_t *opts, const char *text, const char **error) {
    size_t i;

    for (i = 0; text[i] != '\0'; i++) {
        switch (text[i]) {
            case 'a':
                opts->flag_a = true;
                break;
            case 'u':
                opts->flag_u = true;
                break;
            case 'x':
                opts->flag_x = true;
                break;
            case 'l':
                opts->flag_l = true;
                break;
            case 'e':
                opts->flag_e = true;
                break;
            case 'b':
                opts->flag_b = true;
                break;
            default:
                *error = "unknown option letter";
                return -1;
        }
    }

    return 0;
}

int ps_parse_options(int argc, char **argv, ps_options_t *opts, const char **error) {
    int i;

    memset(opts, 0, sizeof(*opts));
    *error = NULL;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (arg[0] == '-' && arg[1] == '-') {
            if (strcmp(arg, "--help") == 0 || strcmp(arg, "--version") == 0) {
                *error = "unsupported long option";
                return -1;
            }
            if (strcmp(arg, "--bitness") == 0) {
                opts->flag_b = true;
                continue;
            }
            *error = "unknown long option";
            return -1;
        }

        if (arg[0] == '-') {
            if (arg[1] == '\0') {
                *error = "invalid option";
                return -1;
            }
            if (parse_cluster(opts, arg + 1, error) != 0) {
                return -1;
            }
            continue;
        }

        if (parse_cluster(opts, arg, error) != 0) {
            return -1;
        }
    }

    return 0;
}
