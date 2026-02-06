#ifndef LS_H
#define LS_H

#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool all;
    bool almost_all;
    bool long_fmt;
    bool human_readable;
    bool recursive;
    bool reverse;
    bool sort_size;
    bool sort_time;
    bool classify;       // -F
    bool inode;          // -i
    bool directory;      // -d
    bool numeric_ids;    // -n
    bool no_group;       // -G
    bool one_per_line;   // -1
    int color; // 0=never, 1=auto, 2=always
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
