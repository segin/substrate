#ifndef SUBSTRATE_REGEX_FLAGS_H
#define SUBSTRATE_REGEX_FLAGS_H

#include <stddef.h>
#include <stdint.h>

/* Compile and match flags */
enum {
    REGEX_FLAG_UTF8        = 0x0001u, /* treat input as UTF-8 codepoints */
    REGEX_FLAG_ICASE       = 0x0002u, /* case-insensitive (ASCII only by default) */
    REGEX_FLAG_EXTENDED    = 0x0004u, /* ERE-style syntax */
    REGEX_FLAG_PCRE_COMPAT = 0x0008u, /* enable PCRE extensions (if available) */
    REGEX_FLAG_SAFE_ENGINE = 0x0010u, /* use the safe engine (default) */
    REGEX_FLAG_ANCHORED    = 0x0020u, /* anchor pattern automatically */
    REGEX_FLAG_MULTILINE   = 0x0040u, /* ^ and $ match line boundaries */
    REGEX_FLAG_DOTALL      = 0x0080u, /* dot matches newline */
    REGEX_FLAG_LITERAL     = 0x0100u  /* literal fixed-string mode */
};

/* Iterator options */
enum {
    REGEX_ITER_DEFAULT     = 0x0000u,
    REGEX_ITER_NON_OVERLAP = 0x0001u /* default: non-overlapping matches */
};

/* Errors */
typedef enum {
    REGEX_OK = 0,
    REGEX_ERR_SYNTAX,
    REGEX_ERR_NOMEM,
    REGEX_ERR_COMPILE_LIMIT,
    REGEX_ERR_MATCH_TIMEOUT,
    REGEX_ERR_INVALID_ARGUMENT,
    REGEX_ERR_UNSUPPORTED,
    REGEX_ERR_INTERNAL
} regex_err_t;

#endif /* SUBSTRATE_REGEX_FLAGS_H */
