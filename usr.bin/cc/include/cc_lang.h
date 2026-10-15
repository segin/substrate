#ifndef CC_LANG_H
#define CC_LANG_H

/*
 * The C language mode a compilation follows: which standard, and
 * whether GNU extensions are on.  Decoded once from -std= by
 * cc_lang_from_std(); the preprocessor, parser and sema read the
 * selected mode with cc_lang_current().
 */

/** A C language mode. */
typedef struct {
    const char *name; /**< the -std= value as given, e.g. "gnu17" */
    int year;         /**< the standard: 1990, 1995, 1999, 2011, 2017 or 2023 */
    int gnu;          /**< nonzero for a GNU dialect (gnu89, gnu17, ...) */
} cc_lang_t;

/**
 * Decode a -std= value.  The names are gcc's -- c89, c90, c99, c9x,
 * c11, c1x, c17, c18, c23, c2x, the gnu* forms of each and the
 * iso9899:* forms -- and cc's own c95 and gnu95.
 *
 * @param std  the value after "-std=", or NULL or "" for the default,
 *             gnu17
 * @param out  receives the mode
 * @return     0, or -1 if @p std names no mode
 */
int cc_lang_from_std(const char *std, cc_lang_t *out);

/**
 * Make @p lang the mode of the compilation in progress.
 *
 * @param lang  the mode; it is copied
 */
void cc_lang_select(const cc_lang_t *lang);

/**
 * The mode of the compilation in progress: gnu17 until cc_lang_select()
 * says otherwise.
 *
 * @return the selected mode; never NULL
 */
const cc_lang_t *cc_lang_current(void);

/**
 * Whether the selected mode is at least a given standard.
 *
 * @param year  1990, 1995, 1999, 2011, 2017 or 2023
 * @return      1 if it is, else 0
 */
int cc_lang_at_least(int year);

/**
 * Whether the selected mode is a GNU dialect.
 *
 * @return 1 if it is, else 0
 */
int cc_lang_gnu(void);

#endif
