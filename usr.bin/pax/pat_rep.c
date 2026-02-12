/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 */

#include "pax.h"

int
rep_add(char *str)
{
    /* Parse -s replacement string */
    /* Stub */
    if (str)
        fprintf(stderr, "pax: substitution -s %s ignored (regex not implemented)\n", str);
    return 0;
}

int
rep_name(char *name, int *nlen, int *prnt)
{
    /* Perform substitution */
    /* Stub: return 0 (no match/substitution) */
    /* If match, return 1 and modify name */
    (void)name;
    (void)nlen;
    *prnt = 1; /* Print name */
    return 0;
}

int
pat_add(char *str, char *chd)
{
    /* Add file pattern */
    /* Stub */
    (void)str;
    (void)chd;
    return 0;
}

int
pat_match(ARCH_HDR *arcn)
{
    /* Check if file matches pattern */
    /* If no patterns, match all */
    /* Stub: return 0 (success) */
    (void)arcn;
    return 0;
}
