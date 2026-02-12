/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 */

#include "pax.h"

int act = OP_LIST;  /* operation mode, default list */
int vflag;          /* verbose */
int dflag;          /* directory list mode */
int cflag;          /* complement match */
int iflag;          /* interactive rename */
int kflag;          /* prevent overwrite */
int lflag;          /* link files */
int nflag;          /* select first match */
int oflag;          /* open tty */
int pflag;          /* preserve attributes */
int sflag;          /* substitution */
int tflag;          /* reset access time */
int uflag;          /* ignore older */
int xflag;          /* read/write format */
int zflag;          /* gzip */

char *dirptr;       /* destination directory */
char *ltemp;        /* temp name */
char *artype;       /* archive type string */
char *arfile;       /* archive file name */

int
main(int argc, char **argv)
{
    /* Initialize default values */
    arfile = NULL;

    options(argc, argv);

    /* dispatch */
    switch (act) {
    case OP_LIST:
        list_archive();
        break;
    case OP_READ:
        ar_read();
        break;
    case OP_WRITE:
        ar_write(argc, argv);
        break;
    case OP_COPY:
        copy_file(argc, argv);
        break;
    case OP_APPEND:
        ar_append(argc, argv);
        break;
    default:
        usage();
    }

    return 0;
}

void
usage(void)
{
    fprintf(stderr, "usage: pax [-cdnv] [-f archive] [-s replstr] ... [pattern ...]\n");
    fprintf(stderr, "       pax -r [-cdiknuv] [-f archive] [-p string] [-s replstr] ... [pattern ...]\n");
    fprintf(stderr, "       pax -w [-dituvX] [-b blocksize] [-f archive] [-x format] ... [file ...]\n");
    fprintf(stderr, "       pax -r -w [-diklntuvX] [-p string] [-s replstr] ... [file ...] directory\n");
    exit(1);
}
