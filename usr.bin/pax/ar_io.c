/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 */

#include "pax.h"

int
ar_next(void)
{
    /* stub */
    return 0;
}

int
ar_open(const char *name)
{
    /* stub */
    if (name)
        fprintf(stderr, "ar_open: %s\n", name);
    return 0;
}

void
ar_close(void)
{
    /* stub */
}

void
ar_drain(void)
{
    /* stub */
}

int
ar_fow(off_t sksz, off_t *skipped)
{
    (void)sksz;
    *skipped = 0;
    return 0;
}

int
wr_skip(off_t skip)
{
    (void)skip;
    return 0;
}

int
rd_skip(off_t skip)
{
    (void)skip;
    return 0;
}

void
cp_start(void)
{
}
