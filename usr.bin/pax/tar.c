/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 */

#include "pax.h"

int
ustar_id(char *blk, int size)
{
    HD_USTAR *hd;
    if (size < BLKMULT)
        return -1;
    hd = (HD_USTAR *)blk;

    /* Check magic */
    if (strncmp(hd->magic, "ustar", 5) != 0)
        return -1;

    return 0;
}

int
ustar_rd(ARCH_HDR *arcn, char *buf)
{
    HD_USTAR *hd = (HD_USTAR *)buf;
    char namebuf[MAXPATHLEN];

    if (hd->prefix[0] != '\0') {
        snprintf(namebuf, sizeof(namebuf), "%.*s/%.*s",
            (int)sizeof(hd->prefix), hd->prefix,
            (int)sizeof(hd->name), hd->name);
    } else {
        snprintf(namebuf, sizeof(namebuf), "%.*s",
            (int)sizeof(hd->name), hd->name);
    }
    arcn->name = strdup(namebuf);

    arcn->mode = strtol(hd->mode, NULL, 8);
    arcn->uid = strtol(hd->uid, NULL, 8);
    arcn->gid = strtol(hd->gid, NULL, 8);
    arcn->size = strtol(hd->size, NULL, 8);
    arcn->mtime = strtol(hd->mtime, NULL, 8);

    /* Typeflag */
    switch (hd->typeflag) {
    case '5':
        arcn->type = S_IFDIR;
        break;
    default:
        arcn->type = S_IFREG;
        break;
    }

    return 0;
}

int
ustar_wr(ARCH_HDR *arcn)
{
    (void)arcn;
    return 0;
}
