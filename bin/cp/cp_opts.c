#include "cp_opts.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void cp_enable_preserve_basic(struct cp_options *opts)
{
    opts->preserve_mode = 1;
    opts->preserve_owner = 1;
    opts->preserve_timestamps = 1;
}

static void cp_enable_preserve_all(struct cp_options *opts)
{
    cp_enable_preserve_basic(opts);
    opts->preserve_links = 1;
    opts->preserve_xattr = 1;
    opts->preserve_acl = 1;
    opts->preserve_flags = 1;
    opts->preserve_all = 1;
}

void cp_options_init(struct cp_options *opts)
{
    memset(opts, 0, sizeof(*opts));
    opts->symlink_mode = CP_SYMLINK_AUTO;
    opts->overwrite_mode = CP_OVERWRITE_FORCE;
    opts->link_mode = CP_LINKMODE_COPY;
    opts->sparse_mode = CP_SPARSE_AUTO;
    opts->buffer_size = CP_DEFAULT_BUFSIZE;
    opts->atomic_replace = 1;
    opts->non_tty_default = CP_PROMPT_DEFAULT_NO;
    opts->backup_control = CP_BACKUP_NONE;
    opts->backup_suffix = "~";
    opts->reflink_mode = CP_REFLINK_AUTO;
}

static int cp_set_preserve_item(struct cp_options *opts, const char *item, int enable)
{
    int v = enable ? 1 : 0;

    if (strcmp(item, "mode") == 0) {
        opts->preserve_mode = v;
        return 0;
    }
    if (strcmp(item, "ownership") == 0 || strcmp(item, "owner") == 0) {
        opts->preserve_owner = v;
        return 0;
    }
    if (strcmp(item, "timestamps") == 0 || strcmp(item, "time") == 0) {
        opts->preserve_timestamps = v;
        return 0;
    }
    if (strcmp(item, "links") == 0) {
        opts->preserve_links = v;
        return 0;
    }
    if (strcmp(item, "xattr") == 0 || strcmp(item, "xattrs") == 0) {
        opts->preserve_xattr = v;
        return 0;
    }
    if (strcmp(item, "acl") == 0 || strcmp(item, "acls") == 0) {
        opts->preserve_acl = v;
        return 0;
    }
    if (strcmp(item, "flags") == 0) {
        opts->preserve_flags = v;
        return 0;
    }
    if (strcmp(item, "all") == 0 || strcmp(item, "a") == 0) {
        if (enable) {
            cp_enable_preserve_all(opts);
        } else {
            opts->preserve_mode = 0;
            opts->preserve_owner = 0;
            opts->preserve_timestamps = 0;
            opts->preserve_links = 0;
            opts->preserve_xattr = 0;
            opts->preserve_acl = 0;
            opts->preserve_flags = 0;
            opts->preserve_all = 0;
        }
        return 0;
    }
    return -1;
}

static int cp_parse_preserve_list(struct cp_options *opts, const char *value, int enable)
{
    char *copy;
    char *tok;
    char *saveptr = NULL;

    copy = strdup(value);
    if (!copy) {
        return -1;
    }

    tok = strtok_r(copy, ",", &saveptr);
    if (!tok) {
        free(copy);
        return -1;
    }

    while (tok) {
        if (cp_set_preserve_item(opts, tok, enable) != 0) {
            free(copy);
            return -1;
        }
        tok = strtok_r(NULL, ",", &saveptr);
    }

    if (!(opts->preserve_mode && opts->preserve_owner && opts->preserve_timestamps &&
          opts->preserve_links && opts->preserve_xattr && opts->preserve_acl &&
          opts->preserve_flags)) {
        opts->preserve_all = 0;
    }

    free(copy);
    return 0;
}

int cp_parse_size(const char *text, size_t *out_size, const char **err_msg)
{
    unsigned long long value;
    long parsed;
    unsigned long long mult = 1;
    char *end = NULL;
    char suffix[8];
    size_t i;

    if (!text || !*text) {
        *err_msg = "empty size";
        return -1;
    }

    errno = 0;
    parsed = strtol(text, &end, 0);
    if (end == text || errno == ERANGE || parsed < 0) {
        *err_msg = "invalid numeric size";
        return -1;
    }
    value = (unsigned long long)parsed;

    if (*end != '\0') {
        if (strlen(end) >= sizeof(suffix)) {
            *err_msg = "size suffix too long";
            return -1;
        }
        for (i = 0; end[i] != '\0'; ++i) {
            suffix[i] = (char)tolower((unsigned char)end[i]);
        }
        suffix[i] = '\0';

        if (strcmp(suffix, "k") == 0 || strcmp(suffix, "ki") == 0 || strcmp(suffix, "kib") == 0) {
            mult = 1024ULL;
        } else if (strcmp(suffix, "m") == 0 || strcmp(suffix, "mi") == 0 || strcmp(suffix, "mib") == 0) {
            mult = 1024ULL * 1024ULL;
        } else if (strcmp(suffix, "g") == 0 || strcmp(suffix, "gi") == 0 || strcmp(suffix, "gib") == 0) {
            mult = 1024ULL * 1024ULL * 1024ULL;
        } else if (strcmp(suffix, "t") == 0 || strcmp(suffix, "ti") == 0 || strcmp(suffix, "tib") == 0) {
            mult = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
        } else if (strcmp(suffix, "p") == 0 || strcmp(suffix, "pi") == 0 || strcmp(suffix, "pib") == 0) {
            mult = 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
        } else if (strcmp(suffix, "kb") == 0) {
            mult = 1000ULL;
        } else if (strcmp(suffix, "mb") == 0) {
            mult = 1000ULL * 1000ULL;
        } else if (strcmp(suffix, "gb") == 0) {
            mult = 1000ULL * 1000ULL * 1000ULL;
        } else if (strcmp(suffix, "tb") == 0) {
            mult = 1000ULL * 1000ULL * 1000ULL * 1000ULL;
        } else {
            *err_msg = "unsupported size suffix";
            return -1;
        }
    }

    if (mult != 0 && value > (unsigned long long)SIZE_MAX / mult) {
        *err_msg = "size overflows platform size_t";
        return -1;
    }

    *out_size = (size_t)(value * mult);
    if (*out_size == 0) {
        *err_msg = "buffer size must be > 0";
        return -1;
    }

    return 0;
}

static int cp_token_looks_size(const char *text)
{
    if (!text || !*text) {
        return 0;
    }
    if (isdigit((unsigned char)text[0])) {
        return 1;
    }
    if (text[0] == '+' || text[0] == '-') {
        return isdigit((unsigned char)text[1]);
    }
    return 0;
}

static int cp_parse_sparse_mode(struct cp_options *opts, const char *value)
{
    if (strcmp(value, "auto") == 0) {
        opts->sparse_mode = CP_SPARSE_AUTO;
        return 0;
    }
    if (strcmp(value, "always") == 0) {
        opts->sparse_mode = CP_SPARSE_ALWAYS;
        return 0;
    }
    if (strcmp(value, "never") == 0) {
        opts->sparse_mode = CP_SPARSE_NEVER;
        return 0;
    }
    return -1;
}

static int cp_parse_backup_control(enum cp_backup_control *out, const char *value)
{
    if (strcmp(value, "none") == 0 || strcmp(value, "off") == 0 || strcmp(value, "never") == 0) {
        *out = CP_BACKUP_NONE;
        return 0;
    }
    if (strcmp(value, "simple") == 0 || strcmp(value, "nil") == 0) {
        *out = CP_BACKUP_SIMPLE;
        return 0;
    }
    if (strcmp(value, "numbered") == 0 || strcmp(value, "t") == 0) {
        *out = CP_BACKUP_NUMBERED;
        return 0;
    }
    if (strcmp(value, "existing") == 0) {
        *out = CP_BACKUP_EXISTING;
        return 0;
    }
    return -1;
}

static int cp_parse_reflink_mode(struct cp_options *opts, const char *value)
{
    if (strcmp(value, "never") == 0) {
        opts->reflink_mode = CP_REFLINK_NEVER;
        return 0;
    }
    if (strcmp(value, "auto") == 0) {
        opts->reflink_mode = CP_REFLINK_AUTO;
        return 0;
    }
    if (strcmp(value, "always") == 0) {
        opts->reflink_mode = CP_REFLINK_ALWAYS;
        return 0;
    }
    return -1;
}

static int cp_parse_long_opt(struct cp_options *opts, const char *arg,
                             int *idx, int argc, char **argv,
                             const char **err_msg)
{
    const char *name = arg + 2;
    const char *value = NULL;
    const char *eq = strchr(name, '=');

    if (eq) {
        size_t keylen = (size_t)(eq - name);
        char *key = (char *)malloc(keylen + 1);
        if (!key) {
            *err_msg = "out of memory";
            return -1;
        }
        memcpy(key, name, keylen);
        key[keylen] = '\0';
        name = key;
        value = eq + 1;

        if (strcmp(name, "buffer-size") == 0) {
            if (cp_parse_size(value, &opts->buffer_size, err_msg) != 0) {
                free((void *)name);
                return -1;
            }
            opts->buffer_size_explicit = 1;
        } else if (strcmp(name, "preserve") == 0) {
            if (cp_parse_preserve_list(opts, value, 1) != 0) {
                *err_msg = "invalid --preserve list";
                free((void *)name);
                return -1;
            }
        } else if (strcmp(name, "no-preserve") == 0) {
            if (cp_parse_preserve_list(opts, value, 0) != 0) {
                *err_msg = "invalid --no-preserve list";
                free((void *)name);
                return -1;
            }
        } else if (strcmp(name, "sparse") == 0) {
            if (cp_parse_sparse_mode(opts, value) != 0) {
                *err_msg = "invalid --sparse mode";
                free((void *)name);
                return -1;
            }
        } else if (strcmp(name, "interactive-default") == 0) {
            if (strcmp(value, "yes") == 0) {
                opts->non_tty_default = CP_PROMPT_DEFAULT_YES;
            } else if (strcmp(value, "no") == 0) {
                opts->non_tty_default = CP_PROMPT_DEFAULT_NO;
            } else {
                *err_msg = "--interactive-default accepts yes|no";
                free((void *)name);
                return -1;
            }
        } else if (strcmp(name, "backup") == 0) {
            if (cp_parse_backup_control(&opts->backup_control, value) != 0) {
                *err_msg = "invalid --backup control";
                free((void *)name);
                return -1;
            }
        } else if (strcmp(name, "suffix") == 0) {
            opts->backup_suffix = value;
        } else if (strcmp(name, "target-directory") == 0) {
            opts->target_directory = value;
        } else if (strcmp(name, "reflink") == 0) {
            if (cp_parse_reflink_mode(opts, value) != 0) {
                *err_msg = "invalid --reflink mode";
                free((void *)name);
                return -1;
            }
        } else {
            *err_msg = "unknown long option";
            free((void *)name);
            return -1;
        }

        free((void *)name);
        return 0;
    }

    if (strcmp(name, "recursive") == 0) {
        opts->recursive = 1;
        return 0;
    }
    if (strcmp(name, "force") == 0) {
        opts->overwrite_mode = CP_OVERWRITE_FORCE;
        opts->force_silent = 1;
        return 0;
    }
    if (strcmp(name, "interactive") == 0) {
        opts->overwrite_mode = CP_OVERWRITE_INTERACTIVE;
        opts->force_silent = 0;
        return 0;
    }
    if (strcmp(name, "no-clobber") == 0) {
        opts->overwrite_mode = CP_OVERWRITE_NOCLOBBER;
        opts->force_silent = 0;
        return 0;
    }
    if (strcmp(name, "verbose") == 0) {
        opts->verbose = 1;
        return 0;
    }
    if (strcmp(name, "archive") == 0) {
        opts->archive = 1;
        opts->recursive = 1;
        opts->symlink_mode = CP_SYMLINK_PHYSICAL;
        opts->preserve_links = 1;
        cp_enable_preserve_all(opts);
        return 0;
    }
    if (strcmp(name, "link") == 0) {
        if (opts->link_mode == CP_LINKMODE_SYM) {
            *err_msg = "-l and -s are mutually exclusive";
            return -1;
        }
        opts->link_mode = CP_LINKMODE_HARD;
        return 0;
    }
    if (strcmp(name, "symbolic-link") == 0) {
        if (opts->link_mode == CP_LINKMODE_HARD) {
            *err_msg = "-l and -s are mutually exclusive";
            return -1;
        }
        opts->link_mode = CP_LINKMODE_SYM;
        return 0;
    }
    if (strcmp(name, "dereference") == 0) {
        opts->symlink_mode = CP_SYMLINK_FOLLOW_ALL;
        return 0;
    }
    if (strcmp(name, "no-dereference") == 0) {
        opts->symlink_mode = CP_SYMLINK_PHYSICAL;
        return 0;
    }
    if (strcmp(name, "follow-command-line") == 0) {
        opts->symlink_mode = CP_SYMLINK_FOLLOW_CMDLINE;
        return 0;
    }
    if (strcmp(name, "buffer-size") == 0) {
        if (*idx + 1 >= argc) {
            *err_msg = "--buffer-size requires an argument";
            return -1;
        }
        *idx += 1;
        if (cp_parse_size(argv[*idx], &opts->buffer_size, err_msg) != 0) {
            return -1;
        }
        opts->buffer_size_explicit = 1;
        return 0;
    }
    if (strcmp(name, "preserve") == 0) {
        if (*idx + 1 >= argc) {
            *err_msg = "--preserve requires an argument";
            return -1;
        }
        *idx += 1;
        if (cp_parse_preserve_list(opts, argv[*idx], 1) != 0) {
            *err_msg = "invalid --preserve list";
            return -1;
        }
        return 0;
    }
    if (strcmp(name, "no-preserve") == 0) {
        if (*idx + 1 >= argc) {
            *err_msg = "--no-preserve requires an argument";
            return -1;
        }
        *idx += 1;
        if (cp_parse_preserve_list(opts, argv[*idx], 0) != 0) {
            *err_msg = "invalid --no-preserve list";
            return -1;
        }
        return 0;
    }
    if (strcmp(name, "sparse") == 0) {
        if (*idx + 1 >= argc) {
            *err_msg = "--sparse requires an argument";
            return -1;
        }
        *idx += 1;
        if (cp_parse_sparse_mode(opts, argv[*idx]) != 0) {
            *err_msg = "invalid --sparse mode";
            return -1;
        }
        return 0;
    }
    if (strcmp(name, "interactive-default") == 0) {
        if (*idx + 1 >= argc) {
            *err_msg = "--interactive-default requires yes|no";
            return -1;
        }
        *idx += 1;
        if (strcmp(argv[*idx], "yes") == 0) {
            opts->non_tty_default = CP_PROMPT_DEFAULT_YES;
        } else if (strcmp(argv[*idx], "no") == 0) {
            opts->non_tty_default = CP_PROMPT_DEFAULT_NO;
        } else {
            *err_msg = "--interactive-default accepts yes|no";
            return -1;
        }
        return 0;
    }
    if (strcmp(name, "atomic-replace") == 0) {
        opts->atomic_replace = 1;
        return 0;
    }
    if (strcmp(name, "no-atomic-replace") == 0) {
        opts->atomic_replace = 0;
        return 0;
    }
    if (strcmp(name, "progress") == 0) {
        opts->progress = 1;
        return 0;
    }
    if (strcmp(name, "backup") == 0) {
        opts->backup_control = CP_BACKUP_SIMPLE;
        if (*idx + 1 < argc && argv[*idx + 1][0] != '-') {
            enum cp_backup_control parsed;
            if (cp_parse_backup_control(&parsed, argv[*idx + 1]) == 0) {
                opts->backup_control = parsed;
                *idx += 1;
            }
        }
        return 0;
    }
    if (strcmp(name, "target-directory") == 0) {
        if (*idx + 1 >= argc) {
            *err_msg = "--target-directory requires argument";
            return -1;
        }
        *idx += 1;
        opts->target_directory = argv[*idx];
        return 0;
    }
    if (strcmp(name, "no-target-directory") == 0) {
        opts->no_target_directory = 1;
        return 0;
    }
    if (strcmp(name, "suffix") == 0) {
        if (*idx + 1 >= argc) {
            *err_msg = "--suffix requires argument";
            return -1;
        }
        *idx += 1;
        opts->backup_suffix = argv[*idx];
        return 0;
    }
    if (strcmp(name, "remove-destination") == 0) {
        opts->remove_destination = 1;
        return 0;
    }
    if (strcmp(name, "update") == 0) {
        opts->update_only = 1;
        return 0;
    }
    if (strcmp(name, "reflink") == 0) {
        if (*idx + 1 >= argc) {
            *err_msg = "--reflink requires mode";
            return -1;
        }
        *idx += 1;
        if (cp_parse_reflink_mode(opts, argv[*idx]) != 0) {
            *err_msg = "invalid --reflink mode";
            return -1;
        }
        return 0;
    }
    if (strcmp(name, "help") == 0) {
        opts->show_help = 1;
        return 0;
    }
    if (strcmp(name, "version") == 0) {
        opts->show_version = 1;
        return 0;
    }

    *err_msg = "unknown long option";
    return -1;
}

static int cp_parse_preserve_short_arg(struct cp_options *opts, const char *arg,
                                       const char **err_msg)
{
    if (!arg || !*arg) {
        cp_enable_preserve_basic(opts);
        return 0;
    }

    if (strcmp(arg, "a") == 0 || strcmp(arg, "all") == 0) {
        cp_enable_preserve_all(opts);
        return 0;
    }

    if (strcmp(arg, "mode") == 0) {
        opts->preserve_mode = 1;
        opts->preserve_owner = 0;
        opts->preserve_timestamps = 0;
        return 0;
    }

    *err_msg = "invalid -p argument, expected a|mode";
    return -1;
}

static int cp_parse_short_opt(struct cp_options *opts, const char *arg,
                              int *idx, int argc, char **argv,
                              const char **err_msg)
{
    size_t j;

    for (j = 1; arg[j] != '\0'; ++j) {
        char c = arg[j];

        switch (c) {
        case 'R':
        case 'r':
            opts->recursive = 1;
            break;
        case 'H':
            opts->symlink_mode = CP_SYMLINK_FOLLOW_CMDLINE;
            break;
        case 'L':
            opts->symlink_mode = CP_SYMLINK_FOLLOW_ALL;
            break;
        case 'P':
            opts->symlink_mode = CP_SYMLINK_PHYSICAL;
            break;
        case 'f':
            opts->overwrite_mode = CP_OVERWRITE_FORCE;
            opts->force_silent = 1;
            break;
        case 'i':
            opts->overwrite_mode = CP_OVERWRITE_INTERACTIVE;
            opts->force_silent = 0;
            break;
        case 'n':
            opts->overwrite_mode = CP_OVERWRITE_NOCLOBBER;
            opts->force_silent = 0;
            break;
        case 'v':
            opts->verbose = 1;
            opts->progress = 1;
            break;
        case 'a':
            opts->archive = 1;
            opts->recursive = 1;
            opts->symlink_mode = CP_SYMLINK_PHYSICAL;
            opts->preserve_links = 1;
            cp_enable_preserve_all(opts);
            break;
        case 'd':
            opts->symlink_mode = CP_SYMLINK_PHYSICAL;
            opts->preserve_links = 1;
            break;
        case 'l':
            if (opts->link_mode == CP_LINKMODE_SYM) {
                *err_msg = "-l and -s are mutually exclusive";
                return -1;
            }
            opts->link_mode = CP_LINKMODE_HARD;
            break;
        case 's':
            if (opts->link_mode == CP_LINKMODE_HARD) {
                *err_msg = "-l and -s are mutually exclusive";
                return -1;
            }
            opts->link_mode = CP_LINKMODE_SYM;
            break;
        case 'b': {
            size_t parsed_size;
            const char *local_err = NULL;

            if (arg[j + 1] != '\0') {
                if (cp_parse_size(&arg[j + 1], &opts->buffer_size, err_msg) != 0) {
                    return -1;
                }
                opts->buffer_size_explicit = 1;
                return 0;
            }

            if (*idx + 1 < argc && cp_token_looks_size(argv[*idx + 1])) {
                if (cp_parse_size(argv[*idx + 1], &parsed_size, &local_err) != 0) {
                    *err_msg = local_err;
                    return -1;
                }
                opts->buffer_size = parsed_size;
                opts->buffer_size_explicit = 1;
                *idx += 1;
            } else {
                opts->backup_control = CP_BACKUP_SIMPLE;
            }
            break;
        }
        case 'S':
            if (arg[j + 1] != '\0') {
                opts->backup_suffix = &arg[j + 1];
            } else {
                if (*idx + 1 >= argc) {
                    *err_msg = "-S requires backup suffix";
                    return -1;
                }
                *idx += 1;
                opts->backup_suffix = argv[*idx];
            }
            return 0;
        case 't':
            if (arg[j + 1] != '\0') {
                opts->target_directory = &arg[j + 1];
            } else {
                if (*idx + 1 >= argc) {
                    *err_msg = "-t requires directory";
                    return -1;
                }
                *idx += 1;
                opts->target_directory = argv[*idx];
            }
            return 0;
        case 'T':
            opts->no_target_directory = 1;
            break;
        case 'u':
            opts->update_only = 1;
            break;
        case 'p':
            cp_enable_preserve_basic(opts);
            if (arg[j + 1] != '\0') {
                if (cp_parse_preserve_short_arg(opts, &arg[j + 1], err_msg) != 0) {
                    return -1;
                }
                return 0;
            }
            if (*idx + 1 < argc &&
                (strcmp(argv[*idx + 1], "a") == 0 ||
                 strcmp(argv[*idx + 1], "mode") == 0 ||
                 strcmp(argv[*idx + 1], "all") == 0)) {
                *idx += 1;
                if (cp_parse_preserve_short_arg(opts, argv[*idx], err_msg) != 0) {
                    return -1;
                }
            }
            break;
        default:
            *err_msg = "unknown option";
            return -1;
        }
    }

    return 0;
}

int cp_parse_options(struct cp_options *opts, int argc, char **argv,
                     const char **err_msg)
{
    int i;
    int end_options = 0;

    cp_options_init(opts);
    *err_msg = NULL;

    for (i = 1; i < argc; ++i) {
        const char *arg = argv[i];

        if (!end_options && strcmp(arg, "--") == 0) {
            end_options = 1;
            continue;
        }

        if (!end_options && arg[0] == '-' && arg[1] != '\0') {
            if (arg[1] == '-') {
                if (cp_parse_long_opt(opts, arg, &i, argc, argv, err_msg) != 0) {
                    return -1;
                }
            } else {
                if (cp_parse_short_opt(opts, arg, &i, argc, argv, err_msg) != 0) {
                    return -1;
                }
            }
            continue;
        }

        break;
    }

    opts->source_start = i;

    if (opts->show_help || opts->show_version) {
        return 0;
    }

    if (opts->target_directory) {
        opts->dest = opts->target_directory;
        opts->source_count = argc - i;
        if (opts->source_count < 1) {
            *err_msg = "missing SOURCE operand for target directory mode";
            return -1;
        }
    } else {
        if (i >= argc - 1) {
            *err_msg = "missing SOURCE and DEST operands";
            return -1;
        }
        opts->source_count = argc - i - 1;
        opts->dest = argv[argc - 1];
    }

    if (opts->no_target_directory && opts->source_count > 1) {
        *err_msg = "-T/--no-target-directory cannot be used with multiple sources";
        return -1;
    }

    if (opts->link_mode == CP_LINKMODE_HARD) {
        opts->symlink_mode = CP_SYMLINK_PHYSICAL;
    }

    if (opts->archive) {
        opts->recursive = 1;
        opts->preserve_links = 1;
        opts->symlink_mode = CP_SYMLINK_PHYSICAL;
    }

    return 0;
}

const char *cp_options_usage(const char *progname)
{
    (void)progname;
    return "Usage: cp [ -R [-H | -L | -P] ] [ -f | -i | -n ] [ -p[a|mode] ] [ -l | -s ] [ -b [bufsize] ] [ -t DIRECTORY | SOURCE... DEST ] [--backup[=CONTROL]] [--sparse=auto|always|never] [--reflink=auto|always|never] [--remove-destination] [--atomic-replace|--no-atomic-replace] [--update] SOURCE... DEST\n";
}
