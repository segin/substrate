#ifndef LS_H
#define LS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>

typedef enum {
    TIME_MTIME = 0,
    TIME_ATIME = 1,
    TIME_CTIME = 2
} ls_time_type_t;

typedef enum {
    TIME_STYLE_LOCALE = 0,
    TIME_STYLE_ISO,
    TIME_STYLE_LONG_ISO,
    TIME_STYLE_FULL_ISO,
    TIME_STYLE_CUSTOM
} ls_time_style_t;

typedef enum {
    LS_COLOR_NEVER = 0,
    LS_COLOR_AUTO = 1,
    LS_COLOR_ALWAYS = 2
} ls_color_mode_t;

typedef enum {
    LS_QUOTE_LITERAL = 0,
    LS_QUOTE_SHELL,
    LS_QUOTE_SHELL_ALWAYS,
    LS_QUOTE_C,
    LS_QUOTE_ESCAPE
} ls_quote_mode_t;

enum {
    LS_EXIT_OK = 0,
    LS_EXIT_MINOR = 1,
    LS_EXIT_SERIOUS = 2
};

typedef struct {
    bool all;
    bool almost_all;
    bool directory;
    const char *ignore_pattern;

    bool long_fmt;
    bool one_per_line;
    bool multi_column;
    bool comma_sep;
    bool by_lines;
    bool no_owner;
    bool no_group;
    bool numeric_ids;
    bool inode;
    bool classify;
    bool slash_dirs;
    bool file_type;
    bool quote_names;

    bool reverse;
    bool sort_size;
    bool sort_time;
    bool no_sort;
    bool version_sort;
    ls_time_type_t time_type;
    ls_time_style_t time_style;
    const char *time_style_format;

    bool dereference;
    bool dereference_args;

    bool human_readable;
    bool kibibytes;
    bool show_blocks;
    bool si_units;
    long block_size;

    bool recursive;
    const char *hide_pattern;
    ls_color_mode_t color;
    int term_width;

    bool hide_control_chars;
    bool literal;
    ls_quote_mode_t quoting_style;

    bool show_help;
    bool show_version;
} ls_config_t;

typedef struct {
    char *name;
    char *full_path;
    char *link_target;
    struct stat st;
    size_t input_index;

    int stat_error;
    bool stat_ok;
    bool display_as_symlink;
    bool dangling_link;
    bool symlink_loop;
} file_info_t;

#endif
