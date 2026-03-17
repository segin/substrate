/*
 * POSIX regex compatibility layer for the Substrate regex library.
 * Implements regcomp(), regexec(), regfree(), and regerror().
 */
#include <stdlib.h>
#include <string.h>

#include "regex_internal.h"

/*
 * regcomp: compile pattern into preg.
 * The user provides the regex_t storage (may be stack or heap).
 */
int regcomp(regex_t *restrict preg, const char *restrict pattern, int cflags)
{
    unsigned sub_flags = 0;
    regex_err_t err;
    regex_t *tmp;

    if (!preg || !pattern)
        return REG_BADPAT;

    if (cflags & REG_ICASE)
        sub_flags |= REGEX_FLAG_ICASE;

    tmp = regex_compile(pattern, sub_flags, &err);
    if (!tmp) {
        switch (err) {
        case REGEX_ERR_SYNTAX:   return REG_BADPAT;
        case REGEX_ERR_NOMEM:    return REG_ESPACE;
        default:                         return REG_BADPAT;
        }
    }

    /* Copy internal state into caller's struct, then free the wrapper. */
    *preg = *tmp;
    free(tmp);

    /* POSIX: re_nsub = number of parenthesized subexpressions */
    preg->re_nsub = preg->capture_count > 0 ? preg->capture_count - 1 : 0;

    return 0;
}

/*
 * regexec: match string against preg starting at string[0].
 */
int regexec(const regex_t *restrict preg, const char *restrict string,
            size_t nmatch, regmatch_t pmatch[restrict], int eflags)
{
    size_t text_len;
    size_t max_captures;
    size_t *offsets;
    ssize_t rc;
    size_t i;

    (void)eflags;
    if (!preg || !string)
        return REG_BADPAT;

    text_len = strlen(string);
    max_captures = preg->capture_count;
    if (nmatch > 0 && nmatch < max_captures)
        max_captures = nmatch;

    offsets = NULL;
    if (max_captures > 0) {
        offsets = (size_t *)malloc(sizeof(size_t) * 2 * max_captures);
        if (!offsets)
            return REG_ESPACE;
    }

    rc = regex_match(preg, string, text_len, offsets, max_captures, NULL);

    if (rc < 0) {
        free(offsets);
        return REG_NOMATCH;
    }

    /* Fill pmatch array */
    if (pmatch && nmatch > 0 && offsets) {
        for (i = 0; i < nmatch && i < max_captures; i++) {
            pmatch[i].rm_so = (int)offsets[i * 2];
            pmatch[i].rm_eo = (int)offsets[i * 2 + 1];
        }
        /* Mark remaining slots as unmatched */
        for (; i < nmatch; i++) {
            pmatch[i].rm_so = -1;
            pmatch[i].rm_eo = -1;
        }
    }

    free(offsets);
    return 0;
}

/*
 * regfree: release resources associated with preg.
 * Does NOT free the regex_t itself (caller owns storage).
 */
void regfree(regex_t *preg)
{
    if (!preg)
        return;
    if (preg->engine && preg->engine->destroy)
        preg->engine->destroy(preg);
    preg->engine = NULL;
    preg->impl = NULL;
}

/*
 * regerror: convert error code to string.
 */
size_t regerror(int errcode, const regex_t *restrict preg,
                char *restrict errbuf, size_t errbuf_size)
{
    const char *msg;
    size_t len;

    (void)preg;

    switch (errcode) {
    case 0:           msg = "Success"; break;
    case REG_NOMATCH: msg = "No match"; break;
    case REG_BADPAT:  msg = "Invalid regular expression"; break;
    case REG_ECOLLATE:msg = "Invalid collating element"; break;
    case REG_ECTYPE:  msg = "Invalid character class"; break;
    case REG_EESCAPE: msg = "Invalid backslash escape"; break;
    case REG_ESUBREG: msg = "Invalid subexpression reference"; break;
    case REG_EBRACK:  msg = "Unmatched [, [^, [:, [., or [="; break;
    case REG_EPAREN:  msg = "Unmatched ( or \\("; break;
    case REG_EBRACE:  msg = "Unmatched \\{"; break;
    case REG_BADBR:   msg = "Invalid contents of \\{\\}"; break;
    case REG_ERANGE:  msg = "Invalid range end"; break;
    case REG_ESPACE:  msg = "Out of memory"; break;
    case REG_BADRPT:  msg = "?, *, +, or {m,n} not preceded by valid RE"; break;
    default:          msg = "Unknown error"; break;
    }

    len = strlen(msg) + 1;
    if (errbuf && errbuf_size > 0) {
        size_t copy = len < errbuf_size ? len : errbuf_size - 1;
        memcpy(errbuf, msg, copy);
        errbuf[copy] = '\0';
    }
    return len;
}
