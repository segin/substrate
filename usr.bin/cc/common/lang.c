/*
 * lang.c: C language modes -- the one decoder of -std= values.
 */

#include "cc_lang.h"

#include <string.h>

/** One accepted -std= name and what it means. */
typedef struct {
    const char *name;
    int year;
    int gnu;
} lang_name_t;

static const lang_name_t g_lang_names[] = {
    {"c89", 1990, 0},           {"c90", 1990, 0},           {"iso9899:1990", 1990, 0},
    {"gnu89", 1990, 1},         {"gnu90", 1990, 1},
    {"c95", 1995, 0},           {"iso9899:199409", 1995, 0}, {"gnu95", 1995, 1},
    {"c99", 1999, 0},           {"c9x", 1999, 0},           {"iso9899:1999", 1999, 0},
    {"iso9899:199x", 1999, 0},  {"gnu99", 1999, 1},         {"gnu9x", 1999, 1},
    {"c11", 2011, 0},           {"c1x", 2011, 0},           {"iso9899:2011", 2011, 0},
    {"gnu11", 2011, 1},         {"gnu1x", 2011, 1},
    {"c17", 2017, 0},           {"c18", 2017, 0},           {"iso9899:2017", 2017, 0},
    {"iso9899:2018", 2017, 0},  {"gnu17", 2017, 1},         {"gnu18", 2017, 1},
    {"c23", 2023, 0},           {"c2x", 2023, 0},           {"iso9899:2024", 2023, 0},
    {"gnu23", 2023, 1},         {"gnu2x", 2023, 1},
};

static cc_lang_t g_current = {"gnu17", 2017, 1};

/**
 * Decode a -std= value.
 *
 * @param std  the value, or NULL or "" for gnu17
 * @param out  receives the mode
 * @return     0, or -1 if @p std names no mode
 */
int cc_lang_from_std(const char *std, cc_lang_t *out) {
    size_t i;

    if (std == NULL || std[0] == '\0') {
        std = "gnu17";
    }
    for (i = 0; i < sizeof(g_lang_names) / sizeof(g_lang_names[0]); ++i) {
        if (strcmp(g_lang_names[i].name, std) == 0) {
            out->name = g_lang_names[i].name;
            out->year = g_lang_names[i].year;
            out->gnu = g_lang_names[i].gnu;
            return 0;
        }
    }
    return -1;
}

/**
 * Make @p lang the mode of the compilation in progress.
 *
 * @param lang  the mode
 */
void cc_lang_select(const cc_lang_t *lang) {
    g_current = *lang;
}

/**
 * The mode of the compilation in progress.
 *
 * @return the selected mode
 */
const cc_lang_t *cc_lang_current(void) {
    return &g_current;
}

/**
 * Whether the selected mode is at least a given standard.
 *
 * @param year  the standard's year
 * @return      1 if it is, else 0
 */
int cc_lang_at_least(int year) {
    return g_current.year >= year;
}

/**
 * Whether the selected mode is a GNU dialect.
 *
 * @return 1 if it is, else 0
 */
int cc_lang_gnu(void) {
    return g_current.gnu;
}
