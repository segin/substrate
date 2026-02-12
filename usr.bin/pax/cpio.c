/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 */

#include "pax.h"

int
cpio_id(char *blk, int size)
{
    HD_CPIO *hd;
    if (size < (int)sizeof(HD_CPIO))
        return -1;
    hd = (HD_CPIO *)blk;
    if (strncmp(hd->c_magic, "070707", 6) == 0)
        return 0;
    return -1;
}

int
cpio_rd(ARCH_HDR *arcn, char *buf)
{
    (void)arcn;
    (void)buf;
    return 0;
}

int
cpio_wr(ARCH_HDR *arcn)
{
    (void)arcn;
    return 0;
}
