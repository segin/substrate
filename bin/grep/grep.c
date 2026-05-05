#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define GREP_VERSION "grep (Substrate) 0.1"

struct grep_options {
    const char *progname;
    const char **patterns;
    size_t pattern_count;
    size_t pattern_cap;
    int first_file;
    bool count_only;
    bool extended;
    bool fixed_strings;
    bool ignore_case;
    bool invert_match;
    bool line_number;
    bool quiet;
    bool show_help;
    bool show_version;
    bool suppress_errors;
};

static void
grep_print_usage(FILE *stream, const char *progname)
{
    fprintf(stream,
        "Usage: %s [OPTION]... PATTERN [FILE]...\n"
        "       %s [OPTION]... -e PATTERN [FILE]...\n",
        progname, progname);
}

static void
grep_print_help(const char *progname)
{
    grep_print_usage(stdout, progname);
    fputs(
        "\n"
        "Options:\n"
        "  -E, --extended-regexp   PATTERN is an extended regular expression\n"
        "  -F, --fixed-strings     PATTERN is a literal string\n"
        "  -e, --regexp=PATTERN    use PATTERN for matching\n"
        "  -i, --ignore-case       ignore case distinctions\n"
        "  -v, --invert-match      select non-matching lines\n"
        "  -n, --line-number       print line numbers\n"
        "  -c, --count             print only the number of matching lines\n"
        "  -q, --quiet             suppress normal output and stop after first match\n"
        "  -s, --no-messages       suppress file error messages\n"
        "      --help              display this help and exit\n"
        "      --version           output version information and exit\n",
        stdout);
}

static void
grep_print_version(void)
{
    puts(GREP_VERSION);
}

static void
grep_options_init(struct grep_options *opts, const char *progname)
{
    memset(opts, 0, sizeof(*opts));
    opts->progname = (progname != NULL && progname[0] != '\0') ? progname :
        "grep";
}

static int
grep_add_pattern(struct grep_options *opts, const char *pattern,
    const char **err_msg)
{
    const char **new_patterns;
    size_t new_cap;

    if (pattern == NULL) {
        *err_msg = "missing pattern";
        return -1;
    }
    if (opts->pattern_count == opts->pattern_cap) {
        new_cap = opts->pattern_cap == 0 ? 4 : opts->pattern_cap * 2;
        new_patterns = (const char **)realloc(opts->patterns,
            new_cap * sizeof(*new_patterns));
        if (new_patterns == NULL) {
            *err_msg = "out of memory";
            return -1;
        }
        opts->patterns = new_patterns;
        opts->pattern_cap = new_cap;
    }
    opts->patterns[opts->pattern_count++] = pattern;
    return 0;
}

static int
grep_parse_long_option(struct grep_options *opts, const char *arg,
    int argc, char **argv, int *index, const char **err_msg)
{
    const char *name;
    const char *eq;
    size_t name_len;

    name = arg + 2;
    eq = strchr(name, '=');
    name_len = eq ? (size_t)(eq - name) : strlen(name);

    if (name_len == 4 && strncmp(name, "help", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--help does not accept an argument";
            return -1;
        }
        opts->show_help = true;
        return 0;
    }
    if (name_len == 7 && strncmp(name, "version", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--version does not accept an argument";
            return -1;
        }
        opts->show_version = true;
        return 0;
    }
    if (name_len == 15 && strncmp(name, "extended-regexp", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--extended-regexp does not accept an argument";
            return -1;
        }
        opts->extended = true;
        opts->fixed_strings = false;
        return 0;
    }
    if (name_len == 13 && strncmp(name, "fixed-strings", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--fixed-strings does not accept an argument";
            return -1;
        }
        opts->fixed_strings = true;
        return 0;
    }
    if (name_len == 6 && strncmp(name, "regexp", name_len) == 0) {
        const char *pattern;

        if (eq != NULL) {
            pattern = eq + 1;
        } else {
            if (*index + 1 >= argc) {
                *err_msg = "--regexp requires an argument";
                return -1;
            }
            ++(*index);
            pattern = argv[*index];
        }
        return grep_add_pattern(opts, pattern, err_msg);
    }
    if (name_len == 11 && strncmp(name, "ignore-case", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--ignore-case does not accept an argument";
            return -1;
        }
        opts->ignore_case = true;
        return 0;
    }
    if (name_len == 12 && strncmp(name, "invert-match", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--invert-match does not accept an argument";
            return -1;
        }
        opts->invert_match = true;
        return 0;
    }
    if (name_len == 11 && strncmp(name, "line-number", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--line-number does not accept an argument";
            return -1;
        }
        opts->line_number = true;
        return 0;
    }
    if (name_len == 5 && strncmp(name, "count", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--count does not accept an argument";
            return -1;
        }
        opts->count_only = true;
        return 0;
    }
    if (name_len == 5 && strncmp(name, "quiet", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--quiet does not accept an argument";
            return -1;
        }
        opts->quiet = true;
        return 0;
    }
    if (name_len == 6 && strncmp(name, "silent", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--silent does not accept an argument";
            return -1;
        }
        opts->quiet = true;
        opts->suppress_errors = true;
        return 0;
    }
    if (name_len == 11 && strncmp(name, "no-messages", name_len) == 0) {
        if (eq != NULL) {
            *err_msg = "--no-messages does not accept an argument";
            return -1;
        }
        opts->suppress_errors = true;
        return 0;
    }

    *err_msg = "invalid option";
    return -1;
}

static int
grep_parse_short_options(struct grep_options *opts, const char *arg,
    int argc, char **argv, int *index, const char **err_msg)
{
    size_t j;

    for (j = 1; arg[j] != '\0'; ++j) {
        switch (arg[j]) {
        case 'E':
            opts->extended = true;
            opts->fixed_strings = false;
            break;
        case 'F':
            opts->fixed_strings = true;
            break;
        case 'c':
            opts->count_only = true;
            break;
        case 'e':
            if (arg[j + 1] != '\0') {
                return grep_add_pattern(opts, &arg[j + 1], err_msg);
            }
            if (*index + 1 >= argc) {
                *err_msg = "option requires an argument -- 'e'";
                return -1;
            }
            ++(*index);
            return grep_add_pattern(opts, argv[*index], err_msg);
        case 'i':
            opts->ignore_case = true;
            break;
        case 'n':
            opts->line_number = true;
            break;
        case 'q':
            opts->quiet = true;
            break;
        case 's':
            opts->suppress_errors = true;
            break;
        case 'v':
            opts->invert_match = true;
            break;
        default:
            *err_msg = "invalid option";
            return -1;
        }
    }

    return 0;
}

static int
grep_parse_options(struct grep_options *opts, int argc, char **argv,
    const char **err_msg)
{
    bool end_of_options;
    int index;

    *err_msg = NULL;
    end_of_options = false;

    for (index = 1; index < argc; ++index) {
        const char *arg;

        arg = argv[index];
        if (!end_of_options && strcmp(arg, "--") == 0) {
            end_of_options = true;
            continue;
        }
        if (!end_of_options && arg[0] == '-' && arg[1] != '\0') {
            if (arg[1] == '-') {
                if (grep_parse_long_option(opts, arg, argc, argv, &index,
                        err_msg) != 0) {
                    return -1;
                }
            } else if (grep_parse_short_options(opts, arg, argc, argv, &index,
                    err_msg) != 0) {
                return -1;
            }
            continue;
        }
        if (opts->pattern_count == 0) {
            if (grep_add_pattern(opts, arg, err_msg) != 0) {
                return -1;
            }
            ++index;
        }
        break;
    }

    if (!opts->show_help && !opts->show_version && opts->pattern_count == 0) {
        *err_msg = "missing pattern";
        return -1;
    }

    opts->first_file = index;
    return 0;
}

static int
grep_fixed_match_at(const char *haystack, const char *needle,
    bool ignore_case)
{
    size_t idx;

    for (idx = 0; needle[idx] != '\0'; ++idx) {
        unsigned char left;
        unsigned char right;

        if (haystack[idx] == '\0') {
            return 0;
        }
        left = (unsigned char)haystack[idx];
        right = (unsigned char)needle[idx];
        if (ignore_case) {
            left = (unsigned char)tolower(left);
            right = (unsigned char)tolower(right);
        }
        if (left != right) {
            return 0;
        }
    }
    return 1;
}

static int
grep_fixed_match(const char *line, const char *pattern, bool ignore_case)
{
    size_t idx;

    if (pattern[0] == '\0') {
        return 1;
    }
    for (idx = 0; line[idx] != '\0'; ++idx) {
        if (grep_fixed_match_at(line + idx, pattern, ignore_case)) {
            return 1;
        }
    }
    return grep_fixed_match_at(line + idx, pattern, ignore_case);
}

static int
grep_read_line(FILE *stream, char **buffer, size_t *cap)
{
    int ch;
    size_t len;

    if (*buffer == NULL) {
        *cap = 256;
        *buffer = (char *)malloc(*cap);
        if (*buffer == NULL) {
            return -1;
        }
    }

    len = 0;
    while ((ch = fgetc(stream)) != EOF) {
        if (len + 2 > *cap) {
            char *new_buffer;
            size_t new_cap;

            new_cap = *cap * 2;
            new_buffer = (char *)realloc(*buffer, new_cap);
            if (new_buffer == NULL) {
                return -1;
            }
            *buffer = new_buffer;
            *cap = new_cap;
        }
        (*buffer)[len++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }

    if (ferror(stream)) {
        return -1;
    }
    if (ch == EOF && len == 0) {
        return 0;
    }

    (*buffer)[len] = '\0';
    return 1;
}

static const char *
grep_regex_error_string(regex_err_t err)
{
    switch (err) {
    case REGEX_OK:
        return "success";
    case REGEX_ERR_SYNTAX:
        return "invalid regular expression";
    case REGEX_ERR_NOMEM:
        return "out of memory";
    case REGEX_ERR_COMPILE_LIMIT:
        return "regex compile limit exceeded";
    case REGEX_ERR_MATCH_TIMEOUT:
        return "regex match timeout";
    case REGEX_ERR_INVALID_ARGUMENT:
        return "invalid regex argument";
    case REGEX_ERR_UNSUPPORTED:
        return "unsupported regex feature";
    case REGEX_ERR_INTERNAL:
        return "internal regex error";
    default:
        return "unknown regex error";
    }
}

static int
grep_line_matches(const struct grep_options *opts, regex_t **regexes,
    const char *line)
{
    size_t idx;

    for (idx = 0; idx < opts->pattern_count; ++idx) {
        int matched;

        if (opts->fixed_strings) {
            matched = grep_fixed_match(line, opts->patterns[idx],
                opts->ignore_case);
        } else {
            matched = regex_match(regexes[idx], line, strlen(line), NULL, 0,
                NULL) >= 0;
        }
        if (matched) {
            return !opts->invert_match;
        }
    }
    return opts->invert_match;
}

static void
grep_print_match(const struct grep_options *opts, const char *name,
    int multiple_files, size_t line_no, const char *line)
{
    if (multiple_files) {
        printf("%s:", name);
    }
    if (opts->line_number) {
        printf("%lu:", (unsigned long)line_no);
    }
    fputs(line, stdout);
    if (line[0] != '\0' && line[strlen(line) - 1] != '\n') {
        putchar('\n');
    }
}

static int
grep_process_stream(const struct grep_options *opts, regex_t **regexes,
    FILE *stream, const char *name, int multiple_files, int *had_error)
{
    char *line;
    size_t cap;
    size_t line_no;
    size_t match_count;
    int saw_match;
    int status;

    line = NULL;
    cap = 0;
    line_no = 0;
    match_count = 0;
    saw_match = 0;
    status = 0;

    for (;;) {
        int read_status;

        read_status = grep_read_line(stream, &line, &cap);
        if (read_status == 0) {
            break;
        }
        if (read_status < 0) {
            if (!opts->suppress_errors) {
                fprintf(stderr, "%s: %s: %s\n", opts->progname, name,
                    strerror(errno));
            }
            *had_error = 1;
            status = -1;
            break;
        }

        ++line_no;
        if (!grep_line_matches(opts, regexes, line)) {
            continue;
        }
        saw_match = 1;
        ++match_count;
        if (opts->quiet) {
            status = 1;
            break;
        }
        if (!opts->count_only) {
            grep_print_match(opts, name, multiple_files, line_no, line);
        }
    }

    if (opts->count_only) {
        if (multiple_files) {
            printf("%s:%lu\n", name, (unsigned long)match_count);
        } else {
            printf("%lu\n", (unsigned long)match_count);
        }
    }

    free(line);
    return status > 0 ? 1 : saw_match;
}

static int
grep_compile_patterns(const struct grep_options *opts, regex_t ***regexes_out)
{
    regex_t **regexes;
    regex_err_t err;
    unsigned flags;
    size_t idx;

    *regexes_out = NULL;
    if (opts->fixed_strings) {
        return 0;
    }

    flags = 0;
    if (opts->extended) {
        flags |= REGEX_FLAG_EXTENDED;
    }
    if (opts->ignore_case) {
        flags |= REGEX_FLAG_ICASE;
    }

    regexes = (regex_t **)calloc(opts->pattern_count, sizeof(*regexes));
    if (regexes == NULL) {
        return -1;
    }
    for (idx = 0; idx < opts->pattern_count; ++idx) {
        regexes[idx] = regex_compile(opts->patterns[idx], flags, &err);
        if (regexes[idx] != NULL) {
            continue;
        }
        fprintf(stderr, "%s: invalid pattern '%s': %s\n",
            opts->progname, opts->patterns[idx],
            grep_regex_error_string(err));
        while (idx != 0) {
            --idx;
            regex_free(regexes[idx]);
        }
        free(regexes);
        return -1;
    }

    *regexes_out = regexes;
    return 0;
}

static void
grep_free_regexes(regex_t **regexes, size_t count)
{
    size_t idx;

    if (regexes == NULL) {
        return;
    }
    for (idx = 0; idx < count; ++idx) {
        regex_free(regexes[idx]);
    }
    free(regexes);
}

int
main(int argc, char *argv[])
{
    const char *err_msg;
    regex_t **regexes;
    struct grep_options opts;
    int had_error;
    int index;
    int multiple_files;
    int saw_match;

    err_msg = NULL;
    regexes = NULL;
    had_error = 0;
    saw_match = 0;

    grep_options_init(&opts, argv[0]);
    if (grep_parse_options(&opts, argc, argv, &err_msg) != 0) {
        grep_print_usage(stderr, opts.progname);
        if (err_msg != NULL) {
            fprintf(stderr, "%s: %s\n", opts.progname, err_msg);
        }
        free(opts.patterns);
        return 2;
    }
    if (opts.show_help) {
        grep_print_help(opts.progname);
        free(opts.patterns);
        return 0;
    }
    if (opts.show_version) {
        grep_print_version();
        free(opts.patterns);
        return 0;
    }
    if (grep_compile_patterns(&opts, &regexes) != 0) {
        free(opts.patterns);
        return 2;
    }

    multiple_files = argc - opts.first_file > 1;
    if (opts.first_file >= argc) {
        saw_match = grep_process_stream(&opts, regexes, stdin, "-", 0,
            &had_error);
        grep_free_regexes(regexes, opts.pattern_count);
        free(opts.patterns);
        if (had_error) {
            return 2;
        }
        return saw_match ? 0 : 1;
    }

    for (index = opts.first_file; index < argc; ++index) {
        FILE *stream;
        const char *name;
        int result;

        name = argv[index];
        if (strcmp(name, "-") == 0) {
            stream = stdin;
        } else {
            stream = fopen(name, "r");
            if (stream == NULL) {
                if (!opts.suppress_errors) {
                    fprintf(stderr, "%s: %s: %s\n", opts.progname, name,
                        strerror(errno));
                }
                had_error = 1;
                continue;
            }
        }

        result = grep_process_stream(&opts, regexes, stream, name,
            multiple_files, &had_error);
        if (result > 0) {
            saw_match = 1;
            if (opts.quiet) {
                if (stream != stdin) {
                    fclose(stream);
                }
                grep_free_regexes(regexes, opts.pattern_count);
                free(opts.patterns);
                return had_error ? 2 : 0;
            }
        }

        if (stream != stdin) {
            fclose(stream);
        }
    }

    grep_free_regexes(regexes, opts.pattern_count);
    free(opts.patterns);
    if (had_error) {
        return 2;
    }
    return saw_match ? 0 : 1;
}