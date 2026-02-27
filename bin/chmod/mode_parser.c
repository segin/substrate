#include "mode_parser.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>

#ifndef S_ISUID
#define S_ISUID 0004000
#endif
#ifndef S_ISGID
#define S_ISGID 0002000
#endif
#ifndef S_ISVTX
#define S_ISVTX 0001000
#endif

#define WHO_U 0x1u
#define WHO_G 0x2u
#define WHO_O 0x4u
#define WHO_ALL (WHO_U | WHO_G | WHO_O)

#define CHMOD_MODE_BITS (S_ISUID | S_ISGID | S_ISVTX | S_IRWXU | S_IRWXG | S_IRWXO)

enum clause_op {
    CLAUSE_ADD,
    CLAUSE_REMOVE,
    CLAUSE_SET
};

struct mode_clause {
    unsigned who;
    bool who_specified;
    enum clause_op op;
    char *perm;
};

struct chmod_mode {
    bool numeric;
    mode_t numeric_mode;
    mode_t umask_value;
    struct mode_clause *clauses;
    size_t clause_count;
};

static void
set_error(char *errbuf, size_t errbuf_len, const char *msg)
{
    size_t i;

    if (errbuf == NULL || errbuf_len == 0) {
        return;
    }
    for (i = 0; msg[i] != '\0' && i + 1 < errbuf_len; ++i) {
        errbuf[i] = msg[i];
    }
    errbuf[i] = '\0';
}

static bool
is_octal_string(const char *s)
{
    size_t i;

    if (s == NULL || s[0] == '\0') {
        return false;
    }

    for (i = 0; s[i] != '\0'; ++i) {
        if (s[i] < '0' || s[i] > '7') {
            return false;
        }
    }
    return true;
}

static mode_t
who_to_rwx_mask(unsigned who)
{
    mode_t mask = 0;

    if ((who & WHO_U) != 0) {
        mask |= S_IRWXU;
    }
    if ((who & WHO_G) != 0) {
        mask |= S_IRWXG;
    }
    if ((who & WHO_O) != 0) {
        mask |= S_IRWXO;
    }

    return mask;
}

static mode_t
who_to_special_mask(unsigned who)
{
    mode_t mask = 0;

    if ((who & WHO_U) != 0) {
        mask |= S_ISUID;
    }
    if ((who & WHO_G) != 0) {
        mask |= S_ISGID;
    }
    if ((who & WHO_O) != 0) {
        mask |= S_ISVTX;
    }

    return mask;
}

static mode_t
class_perm(char perm, unsigned who)
{
    mode_t mask = 0;

    if (perm == 'r') {
        if ((who & WHO_U) != 0) {
            mask |= S_IRUSR;
        }
        if ((who & WHO_G) != 0) {
            mask |= S_IRGRP;
        }
        if ((who & WHO_O) != 0) {
            mask |= S_IROTH;
        }
    } else if (perm == 'w') {
        if ((who & WHO_U) != 0) {
            mask |= S_IWUSR;
        }
        if ((who & WHO_G) != 0) {
            mask |= S_IWGRP;
        }
        if ((who & WHO_O) != 0) {
            mask |= S_IWOTH;
        }
    } else if (perm == 'x') {
        if ((who & WHO_U) != 0) {
            mask |= S_IXUSR;
        }
        if ((who & WHO_G) != 0) {
            mask |= S_IXGRP;
        }
        if ((who & WHO_O) != 0) {
            mask |= S_IXOTH;
        }
    }

    return mask;
}

static mode_t
copy_class_bits(mode_t mode, char src_class, unsigned dst_who)
{
    mode_t src_bits;
    mode_t out = 0;

    if (src_class == 'u') {
        src_bits = (mode_t)((mode & S_IRWXU) >> 6);
    } else if (src_class == 'g') {
        src_bits = (mode_t)((mode & S_IRWXG) >> 3);
    } else {
        src_bits = (mode_t)(mode & S_IRWXO);
    }

    if ((dst_who & WHO_U) != 0) {
        out |= (mode_t)(src_bits << 6);
    }
    if ((dst_who & WHO_G) != 0) {
        out |= (mode_t)(src_bits << 3);
    }
    if ((dst_who & WHO_O) != 0) {
        out |= src_bits;
    }

    return out;
}

static bool
is_perm_char(char c)
{
    return c == 'r' || c == 'w' || c == 'x' || c == 'X' || c == 's' ||
        c == 't' || c == 'u' || c == 'g' || c == 'o';
}

static bool
parse_symbolic(const char *mode_string, struct chmod_mode *out,
    char *errbuf, size_t errbuf_len)
{
    size_t i = 0;
    size_t len;

    len = strlen(mode_string);
    if (len == 0) {
        set_error(errbuf, errbuf_len, "empty mode string");
        return false;
    }

    while (i < len) {
        struct mode_clause clause;
        size_t who_start = i;
        size_t perm_start;
        size_t perm_len;
        struct mode_clause *grown;

        clause.who = 0;
        clause.who_specified = false;
        clause.perm = NULL;

        while (i < len) {
            if (mode_string[i] == 'u') {
                clause.who |= WHO_U;
            } else if (mode_string[i] == 'g') {
                clause.who |= WHO_G;
            } else if (mode_string[i] == 'o') {
                clause.who |= WHO_O;
            } else if (mode_string[i] == 'a') {
                clause.who |= WHO_ALL;
            } else {
                break;
            }
            clause.who_specified = true;
            ++i;
        }

        if (!clause.who_specified) {
            clause.who = WHO_ALL;
        }

        if (i >= len) {
            set_error(errbuf, errbuf_len, "missing operation in symbolic mode");
            return false;
        }

        if (mode_string[i] == '+') {
            clause.op = CLAUSE_ADD;
        } else if (mode_string[i] == '-') {
            clause.op = CLAUSE_REMOVE;
        } else if (mode_string[i] == '=') {
            clause.op = CLAUSE_SET;
        } else {
            if (who_start == i) {
                set_error(errbuf, errbuf_len,
                    "symbolic mode must contain an operation (+,-,=)");
            } else {
                set_error(errbuf, errbuf_len,
                    "invalid operation in symbolic mode");
            }
            return false;
        }
        ++i;

        perm_start = i;
        while (i < len && mode_string[i] != ',') {
            if (!is_perm_char(mode_string[i])) {
                set_error(errbuf, errbuf_len,
                    "invalid permission token in symbolic mode");
                return false;
            }
            ++i;
        }

        perm_len = i - perm_start;
        if (perm_len == 0 && clause.op != CLAUSE_SET) {
            set_error(errbuf, errbuf_len,
                "missing permissions after + or - operation");
            return false;
        }

        clause.perm = (char *)malloc(perm_len + 1);
        if (clause.perm == NULL) {
            set_error(errbuf, errbuf_len, "out of memory");
            return false;
        }
        if (perm_len > 0) {
            memcpy(clause.perm, &mode_string[perm_start], perm_len);
        }
        clause.perm[perm_len] = '\0';

        grown = (struct mode_clause *)realloc(out->clauses,
            (out->clause_count + 1) * sizeof(*grown));
        if (grown == NULL) {
            free(clause.perm);
            set_error(errbuf, errbuf_len, "out of memory");
            return false;
        }
        out->clauses = grown;
        out->clauses[out->clause_count] = clause;
        out->clause_count++;

        if (i < len && mode_string[i] == ',') {
            ++i;
            if (i >= len) {
                set_error(errbuf, errbuf_len,
                    "trailing comma in symbolic mode");
                return false;
            }
        }
    }

    if (out->clause_count == 0) {
        set_error(errbuf, errbuf_len, "empty symbolic mode");
        return false;
    }

    return true;
}

struct chmod_mode *
chmod_setmode(const char *mode_string, char *errbuf, size_t errbuf_len)
{
    struct chmod_mode *mode;

    if (mode_string == NULL || mode_string[0] == '\0') {
        set_error(errbuf, errbuf_len, "mode string is empty");
        return NULL;
    }

    mode = (struct chmod_mode *)calloc(1, sizeof(*mode));
    if (mode == NULL) {
        set_error(errbuf, errbuf_len, "out of memory");
        return NULL;
    }

    if (is_octal_string(mode_string)) {
        char *end = NULL;
        long parsed = strtol(mode_string, &end, 8);

        if (end == NULL || *end != '\0') {
            set_error(errbuf, errbuf_len, "invalid numeric mode");
            chmod_freemode(mode);
            return NULL;
        }
        if (parsed < 0 || parsed > (long)CHMOD_MODE_BITS) {
            set_error(errbuf, errbuf_len,
                "numeric mode out of range (expected 0000-7777)");
            chmod_freemode(mode);
            return NULL;
        }

        mode->numeric = true;
        mode->numeric_mode = (mode_t)parsed;
        return mode;
    }

    mode->numeric = false;
    mode->umask_value = umask(0);
    (void)umask(mode->umask_value);

    if (!parse_symbolic(mode_string, mode, errbuf, errbuf_len)) {
        chmod_freemode(mode);
        return NULL;
    }

    return mode;
}

mode_t
chmod_getmode(const struct chmod_mode *mode, mode_t old_mode)
{
    size_t i;
    mode_t new_mode;

    if (mode == NULL) {
        return old_mode;
    }

    if (mode->numeric) {
        return (old_mode & ~CHMOD_MODE_BITS) | (mode->numeric_mode & CHMOD_MODE_BITS);
    }

    new_mode = old_mode & CHMOD_MODE_BITS;

    for (i = 0; i < mode->clause_count; ++i) {
        const struct mode_clause *clause = &mode->clauses[i];
        const unsigned who = clause->who_specified ? clause->who : WHO_ALL;
        mode_t rwx_scope = who_to_rwx_mask(who);
        mode_t special_scope = who_to_special_mask(who);
        mode_t perm_bits = 0;
        mode_t rwx_apply;
        mode_t special_apply;
        size_t j;

        if (!clause->who_specified) {
            rwx_scope &= ~(mode->umask_value & (S_IRWXU | S_IRWXG | S_IRWXO));
        }

        for (j = 0; clause->perm[j] != '\0'; ++j) {
            const char tok = clause->perm[j];

            if (tok == 'r' || tok == 'w' || tok == 'x') {
                perm_bits |= class_perm(tok, who);
            } else if (tok == 'X') {
                if (S_ISDIR(old_mode) ||
                    (new_mode & (S_IXUSR | S_IXGRP | S_IXOTH)) != 0) {
                    perm_bits |= class_perm('x', who);
                }
            } else if (tok == 's') {
                if ((who & WHO_U) != 0) {
                    perm_bits |= S_ISUID;
                }
                if ((who & WHO_G) != 0) {
                    perm_bits |= S_ISGID;
                }
            } else if (tok == 't') {
                if ((who & WHO_O) != 0) {
                    perm_bits |= S_ISVTX;
                }
            } else {
                perm_bits |= copy_class_bits(new_mode, tok, who);
            }
        }

        rwx_apply = perm_bits & (S_IRWXU | S_IRWXG | S_IRWXO) & rwx_scope;
        special_apply = perm_bits & (S_ISUID | S_ISGID | S_ISVTX);

        if (clause->op == CLAUSE_ADD) {
            new_mode |= (rwx_apply | special_apply);
        } else if (clause->op == CLAUSE_REMOVE) {
            new_mode &= ~(rwx_apply | special_apply);
        } else {
            new_mode &= ~(rwx_scope | special_scope);
            new_mode |= (rwx_apply | special_apply);
        }
    }

    return (old_mode & ~CHMOD_MODE_BITS) | (new_mode & CHMOD_MODE_BITS);
}

void
chmod_freemode(struct chmod_mode *mode)
{
    size_t i;

    if (mode == NULL) {
        return;
    }

    for (i = 0; i < mode->clause_count; ++i) {
        free(mode->clauses[i].perm);
    }
    free(mode->clauses);
    free(mode);
}
