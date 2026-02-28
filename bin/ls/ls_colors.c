#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "ls_colors.h"

static char color_di[32] = "\033[1;34m";
static char color_ln[32] = "\033[1;36m";
static char color_ex[32] = "\033[1;32m";
static char color_fi[32] = "";
static char color_so[32] = "\033[1;35m";
static char color_pi[32] = "\033[33m";
static char color_bd[32] = "\033[1;33m";
static char color_cd[32] = "\033[1;33m";
static char color_or[32] = "\033[1;31m";
static char color_reset[16] = "\033[0m";

static bool initialized;

static void write_ansi_code(char *dst, size_t dstsz, const char *val, size_t vlen) {
    if (dst == NULL || dstsz < 4) {
        return;
    }

    if (vlen + 4 > dstsz) {
        vlen = dstsz - 4;
    }

    dst[0] = '\033';
    dst[1] = '[';
    memcpy(dst + 2, val, vlen);
    dst[2 + vlen] = 'm';
    dst[3 + vlen] = '\0';
}

static void assign_color(const char *key, size_t klen, const char *val, size_t vlen) {
    if (klen == 2 && strncmp(key, "di", 2) == 0) {
        write_ansi_code(color_di, sizeof(color_di), val, vlen);
    } else if (klen == 2 && strncmp(key, "ln", 2) == 0) {
        write_ansi_code(color_ln, sizeof(color_ln), val, vlen);
    } else if (klen == 2 && strncmp(key, "ex", 2) == 0) {
        write_ansi_code(color_ex, sizeof(color_ex), val, vlen);
    } else if (klen == 2 && strncmp(key, "fi", 2) == 0) {
        write_ansi_code(color_fi, sizeof(color_fi), val, vlen);
    } else if (klen == 2 && strncmp(key, "so", 2) == 0) {
        write_ansi_code(color_so, sizeof(color_so), val, vlen);
    } else if (klen == 2 && strncmp(key, "pi", 2) == 0) {
        write_ansi_code(color_pi, sizeof(color_pi), val, vlen);
    } else if (klen == 2 && strncmp(key, "bd", 2) == 0) {
        write_ansi_code(color_bd, sizeof(color_bd), val, vlen);
    } else if (klen == 2 && strncmp(key, "cd", 2) == 0) {
        write_ansi_code(color_cd, sizeof(color_cd), val, vlen);
    } else if (klen == 2 && strncmp(key, "or", 2) == 0) {
        write_ansi_code(color_or, sizeof(color_or), val, vlen);
    } else if (klen == 2 && strncmp(key, "rs", 2) == 0) {
        write_ansi_code(color_reset, sizeof(color_reset), val, vlen);
    }
}

void ls_colors_init(void) {
    const char *spec;

    if (initialized) {
        return;
    }
    initialized = true;

    spec = getenv("LS_COLORS");
    if (spec == NULL || *spec == '\0') {
        return;
    }

    while (*spec != '\0') {
        const char *eq = strchr(spec, '=');
        const char *colon;
        size_t klen;
        size_t vlen;

        if (eq == NULL) {
            break;
        }

        colon = strchr(eq + 1, ':');
        klen = (size_t)(eq - spec);
        vlen = colon ? (size_t)(colon - (eq + 1)) : strlen(eq + 1);

        if (klen > 0 && vlen > 0) {
            assign_color(spec, klen, eq + 1, vlen);
        }

        if (colon == NULL) {
            break;
        }
        spec = colon + 1;
    }
}

const char *ls_colors_get(const char *name, mode_t mode) {
    (void)name;

    if (S_ISDIR(mode)) {
        return color_di;
    }
    if (S_ISLNK(mode)) {
        return color_ln;
    }
    if (S_ISSOCK(mode)) {
        return color_so;
    }
    if (S_ISFIFO(mode)) {
        return color_pi;
    }
    if (S_ISBLK(mode)) {
        return color_bd;
    }
    if (S_ISCHR(mode)) {
        return color_cd;
    }
    if ((mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) {
        return color_ex;
    }

    return color_fi;
}

const char *ls_colors_reset(void) {
    return color_reset;
}
