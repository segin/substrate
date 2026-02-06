#ifndef LS_H
#define LS_H

#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TIME_MTIME = 0,
    TIME_ATIME = 1,
    TIME_CTIME = 2
} ls_time_type_t;

typedef enum {
    TIME_STYLE_LOCALE = 0,
    TIME_STYLE_ISO,
    TIME_STYLE_LONG_ISO,
    TIME_STYLE_FULL_ISO
} ls_time_style_t;

typedef struct {
    // Filtering
    bool all;            // -a
    bool almost_all;     // -A
    bool directory;      // -d
    char *ignore_pattern; // -I PATTERN

    // Output Format
    bool long_fmt;       // -l
    bool one_per_line;   // -1
    bool multi_column;   // -C
    bool comma_sep;      // -m
    bool by_lines;       // -x
    bool no_owner;       // -g
    bool no_group;       // -o, -G
    bool numeric_ids;    // -n
    bool inode;          // -i
    bool classify;       // -F
    bool slash_dirs;     // -p
    bool file_type;      // --file-type (like -F but no * for exec)
    bool quote_names;    // -Q

    // Sorting
    bool reverse;        // -r
    bool sort_size;      // -S
    bool sort_time;      // -t
    bool no_sort;        // -U
    bool version_sort;   // -v
    ls_time_type_t time_type; // -u (atime), -c (ctime), --time=WORD
    ls_time_style_t time_style; // --time-style=STYLE

    // Symlink Handling
    bool dereference;    // -L
    bool dereference_args; // -H

    // Size & Units
    bool human_readable; // -h
    bool kibibytes;      // -k
    bool show_blocks;    // -s
    bool si_units;       // --si
    long block_size;     // --block-size=SIZE (0 = default)

    // Recursion & Color
    bool recursive;      // -R
    char *hide_pattern;  // --hide=PATTERN (for -R)
    int color;           // 0=never, 1=auto, 2=always
    int term_width;      // --width=N, -w N (0 = auto-detect)

    // Miscellaneous
    bool hide_control_chars; // -q
    bool literal;            // -N, --literal
    int quoting_style;       // 0=literal, 1=shell, 2=shell-always, 3=c, 4=escape
} ls_config_t;

typedef struct {
    char *name;
    struct stat st;
    char *full_path;
} file_info_t;

// Colors
#define COLOR_RESET   "\033[0m"
#define COLOR_DIR     "\033[1;34m"
#define COLOR_EXE     "\033[1;32m"
#define COLOR_LINK    "\033[1;36m"
#define COLOR_DEV     "\033[1;33m"

#endif // LS_H
