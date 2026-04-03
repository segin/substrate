/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 */

#include "pax.h"

static void
parse_args(int argc, char **argv, int *rflag, int *wflag)
{
    int c;

    /*
     * Parse arguments
     */
    while ((c = getopt(argc, argv, "rwb:cf:diks:tuvx:Lnp:X")) != -1) {
        switch (c) {
        case 'r':
            *rflag = 1;
            break;
        case 'w':
            *wflag = 1;
            break;
        case 'f':
            arfile = optarg;
            break;
        case 'x':
            artype = optarg;
            break;
        case 'v':
            vflag = 1;
            break;
        case 'n':
            nflag = 1;
            break;
        case 'c':
            cflag = 1;
            break;
        case 'd':
            dflag = 1;
            break;
        case 'k':
            kflag = 1;
            break;
        case 'i':
            iflag = 1;
            break;
        case 'u':
            uflag = 1;
            break;
        case 't':
            tflag = 1;
            break;
        case 's':
            if (rep_add(optarg) < 0) {
                usage();
            }
            sflag = 1;
            break;
        case 'p':
            /* preserve attributes - stubs */
            break;
        case '?':
        default:
            usage();
        }
    }
}

void
options(int argc, char **argv)
{
    int rflag = 0;
    int wflag = 0;

    parse_args(argc, argv, &rflag, &wflag);

    /* Set mode */
    if (rflag && wflag) {
        act = OP_COPY;
        /* For copy mode, last arg is destination directory */
        if (optind >= argc) {
            fprintf(stderr, "pax: destination directory required for copy\n");
            usage();
        }
        dirptr = argv[argc - 1];
        /* Reduce argc so we don't treat dest dir as source file */
        /* But optind handling is tricky here. We usually iterate argv[optind]... */
    } else if (rflag) {
        act = OP_READ;
    } else if (wflag) {
        act = OP_WRITE;
    } else {
        act = OP_LIST;
    }
}
