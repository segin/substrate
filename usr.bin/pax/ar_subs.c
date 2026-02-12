/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 */

#include "pax.h"

void
list_archive(void)
{
    fprintf(stderr, "List mode not implemented\n");
}

void
ar_read(void)
{
    fprintf(stderr, "Read mode not implemented\n");
}

void
ar_write(int argc, char **argv)
{
    ARCH_HDR arcn;
    int res;

    ftree_start(argc, argv);
    while ((res = ftree_next(&arcn)) != -1) {
        if (res == 1) continue;
        printf("%s\n", arcn.name);
    }
}

void
ar_append(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    fprintf(stderr, "Append mode not implemented\n");
}

void
copy_file(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    fprintf(stderr, "Copy mode not implemented\n");
}
