#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "ls_colors.h"

// LS_COLORS format: "key=value:key=value:..."
// Keys: di=directory, ln=link, ex=executable, fi=file, so=socket, pi=pipe, bd=block, cd=char

static char color_di[32] = "\033[1;34m"; // directory - blue
static char color_ln[32] = "\033[1;36m"; // symlink - cyan
static char color_ex[32] = "\033[1;32m"; // executable - green
static char color_fi[32] = "";           // regular file
static char color_so[32] = "\033[1;35m"; // socket - magenta
static char color_pi[32] = "\033[33m";   // pipe - brown/yellow
static char color_bd[32] = "\033[1;33m"; // block device
static char color_cd[32] = "\033[1;33m"; // char device
static char color_or[32] = "\033[1;31m"; // orphan link - red
static char color_reset[16] = "\033[0m";

static int initialized = 0;

static void parse_color(const char *key, const char *val, int vlen) {
    char buf[32];
    if (vlen >= 31) vlen = 30;
    
    // Convert LS_COLORS value (like "01;34") to ANSI escape
    buf[0] = '\033';
    buf[1] = '[';
    memcpy(buf + 2, val, vlen);
    buf[2 + vlen] = 'm';
    buf[3 + vlen] = '\0';
    
    if (strncmp(key, "di", 2) == 0) memcpy(color_di, buf, 32);
    else if (strncmp(key, "ln", 2) == 0) memcpy(color_ln, buf, 32);
    else if (strncmp(key, "ex", 2) == 0) memcpy(color_ex, buf, 32);
    else if (strncmp(key, "fi", 2) == 0) memcpy(color_fi, buf, 32);
    else if (strncmp(key, "so", 2) == 0) memcpy(color_so, buf, 32);
    else if (strncmp(key, "pi", 2) == 0) memcpy(color_pi, buf, 32);
    else if (strncmp(key, "bd", 2) == 0) memcpy(color_bd, buf, 32);
    else if (strncmp(key, "cd", 2) == 0) memcpy(color_cd, buf, 32);
    else if (strncmp(key, "or", 2) == 0) memcpy(color_or, buf, 32);
    else if (strncmp(key, "rs", 2) == 0) { // reset
        if (vlen < 14) {
            color_reset[0] = '\033';
            color_reset[1] = '[';
            memcpy(color_reset + 2, val, vlen);
            color_reset[2 + vlen] = 'm';
            color_reset[3 + vlen] = '\0';
        }
    }
}

void ls_colors_init(void) {
    if (initialized) return;
    initialized = 1;
    
    char *lscolors = getenv("LS_COLORS");
    if (!lscolors) return;
    
    char *p = lscolors;
    while (*p) {
        // Find key
        char *eq = strchr(p, '=');
        if (!eq) break;
        
        // Find value end
        char *colon = strchr(eq + 1, ':');
        int vlen = colon ? (colon - eq - 1) : (int)strlen(eq + 1);
        
        parse_color(p, eq + 1, vlen);
        
        if (!colon) break;
        p = colon + 1;
    }
}

const char *ls_colors_get(const char *name, mode_t mode) {
    (void)name; // Can be used for extension-based coloring later
    
    if (S_ISDIR(mode)) return color_di;
    if (S_ISLNK(mode)) return color_ln;
    if (S_ISSOCK(mode)) return color_so;
    if (S_ISFIFO(mode)) return color_pi;
    if (S_ISBLK(mode)) return color_bd;
    if (S_ISCHR(mode)) return color_cd;
    if (mode & (S_IXUSR | S_IXGRP | S_IXOTH)) return color_ex;
    return color_fi;
}

const char *ls_colors_reset(void) {
    return color_reset;
}
