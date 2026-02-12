/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 */

#include "pax.h"

int
file_creat(ARCH_HDR *arcn)
{
    /* Create file from archive header */
    int fd;

    if (arcn->type == S_IFDIR) {
        if (mkdir(arcn->name, arcn->mode) < 0 && errno != EEXIST) {
            fprintf(stderr, "pax: mkdir %s failed: %s\n", arcn->name, strerror(errno));
            return -1;
        }
        return -1; /* Directory created, no fd needed */
    }

    fd = open(arcn->name, O_WRONLY | O_CREAT | O_TRUNC, arcn->mode);
    if (fd < 0) {
        fprintf(stderr, "pax: create %s failed: %s\n", arcn->name, strerror(errno));
    }
    return fd;
}

void
file_close(ARCH_HDR *arcn, int fd)
{
    if (fd >= 0)
        close(fd);

    /* Restore attributes (mtime, mode) if pflag set */
    /* Stub */
    (void)arcn;
}
