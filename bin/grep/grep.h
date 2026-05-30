/*
 * grep.h - shared declarations for the Substrate grep/egrep/fgrep family.
 *
 * See docs/specs/grep-spec.md for the requirement IDs (REQ-GREP-*) cited
 * throughout this implementation.
 */
#ifndef GREP_H
#define GREP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <regex.h>

#define GREP_VERSION "grep (Substrate) 1.0"

/* Pattern dialect (REQ-GREP-020..022). */
enum grep_dialect {
    GREP_BRE = 0,   /* POSIX Basic Regular Expression (grep default) */
    GREP_ERE,       /* POSIX Extended Regular Expression (-E / egrep)  */
    GREP_FIXED      /* literal fixed strings (-F / fgrep)              */
};

/* Binary-file disposition (REQ-GREP-090..094). */
enum grep_binary {
    GREP_BIN_BINARY = 0,    /* default: report "Binary file X matches" */
    GREP_BIN_TEXT,          /* -a / --binary-files=text                */
    GREP_BIN_WITHOUT_MATCH  /* -I / --binary-files=without-match       */
};

/* Directory-operand disposition (REQ-GREP-084/085). */
enum grep_diraction {
    GREP_DIR_SKIP = 0,      /* warn and skip (BSD default w/o -r)      */
    GREP_DIR_READ,          /* attempt to read as a file              */
    GREP_DIR_RECURSE        /* descend (implied by -r/-R)             */
};

/* Colorization mode (REQ-GREP-101/102). */
enum grep_color {
    GREP_COLOR_NEVER = 0,
    GREP_COLOR_ALWAYS,
    GREP_COLOR_AUTO
};

/* One compiled/stored pattern. */
struct grep_pattern {
    char    *text;      /* fixed-string bytes, or (translated) regex src */
    size_t   len;       /* byte length (fixed strings; may contain NUL?  */
    regex_t *re;        /* compiled regex, or NULL in fixed-string mode  */
    size_t  *caps;      /* scratch capture-offset buffer for regex_match */
    size_t   capslots;  /* number of size_t slots in caps                */
};

struct grep_ctx {
    const char *progname;
    enum grep_dialect dialect;

    /* matching control (REQ-GREP-040..046) */
    bool ignore_case;
    bool invert;
    bool word;
    bool line_regexp;

    /* general output (REQ-GREP-050..058) */
    bool count;
    bool files_with;
    bool files_without;
    bool only_matching;
    bool quiet;
    bool no_messages;
    long max_count;             /* -1 == unlimited */

    /* prefixing (REQ-GREP-060..066) */
    bool line_number;
    bool byte_offset;
    bool with_filename;         /* -H */
    bool no_filename;           /* -h */
    const char *label;          /* --label for stdin */

    /* selection / recursion (REQ-GREP-080..085) */
    bool recursive;
    enum grep_diraction diraction;
    const char **include;
    size_t ninclude;
    const char **exclude;
    size_t nexclude;

    /* binary (REQ-GREP-090..094) */
    enum grep_binary binary;

    /* delimiters / color / info (REQ-GREP-100..105) */
    bool null_data;             /* -z */
    int  delim;                 /* '\n' or '\0' */
    enum grep_color color;
    bool color_active;          /* resolved against isatty */

    /* context (REQ-GREP-110..114) */
    long before;
    long after;

    /* pattern set */
    struct grep_pattern *patterns;
    size_t npat;
    size_t cappat;

    /* info requests */
    bool show_help;
    bool show_version;

    /* runtime state */
    bool multiple_files;        /* >1 input or recursive => prefix names */
    bool any_error;             /* drives exit status 2 */
    bool usage_error;           /* parse error warrants a usage line */
};

/* --- grep_opts.c --- */
void grep_ctx_init(struct grep_ctx *g, const char *argv0);
void grep_print_usage(FILE *out, const char *progname);
void grep_print_help(const char *progname);
void grep_print_version(void);
/* Parse argv.  On success fills the files/nfiles outputs with the
 * (heap-allocated) file operand vector, having already consumed the pattern
 * operand when no -e/-f was supplied.  The caller frees the vector.  Returns
 * 0 on success or -1 on error with errmsg set. */
int grep_parse_args(struct grep_ctx *g, int argc, char **argv,
                    char ***files, int *nfiles, const char **errmsg);

/* --- grep_pattern.c --- */
/* Add patterns from a buffer, splitting on '\n' into separate patterns. */
int grep_add_patterns(struct grep_ctx *g, const char *buf, size_t len,
                      const char **errmsg);
/* Add patterns read from a file (or "-" for stdin). */
int grep_add_pattern_file(struct grep_ctx *g, const char *path,
                          const char **errmsg);
/* Compile/prepare all patterns once option parsing is complete. */
int grep_compile_patterns(struct grep_ctx *g, const char **errmsg);
void grep_free_patterns(struct grep_ctx *g);
/* Whole-line selection test (pre-invert handled by caller). Returns 1 if the
 * line matches any pattern under the active -i/-w/-x rules, else 0. */
int grep_line_match(struct grep_ctx *g, const char *line, size_t len);
/* Find the leftmost qualifying match at or after `from`; used by -o.
 * On success sets the ms and me outputs and returns 1, else returns 0. */
int grep_find_match(struct grep_ctx *g, const char *line, size_t len,
                    size_t from, size_t *ms, size_t *me);

#endif /* GREP_H */
