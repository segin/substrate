/*
 * Copyright (c) 2024 Substrate OS
 * All rights reserved.
 */

#include "pax.h"
#include <dirent.h>

static int f_argc;
static char **f_argv;
static int f_idx;

int
ftree_start(int argc, char **argv)
{
    f_argc = argc;
    f_argv = argv;
    f_idx = optind;
    return 0;
}

int
ftree_next(ARCH_HDR *arcn)
{
    char *path;
    struct stat sb;

    if (f_idx >= f_argc)
        return -1; /* End of list */

    path = f_argv[f_idx++];
    arcn->name = path;

    if (lstat(path, &sb) < 0) {
        fprintf(stderr, "pax: %s: %s\n", path, strerror(errno));
        return 1; /* Skip */
    }

    arcn->mode = sb.st_mode;
    arcn->uid = sb.st_uid;
    arcn->gid = sb.st_gid;
    arcn->size = sb.st_size;
    arcn->mtime = sb.st_mtime;
    arcn->atime = sb.st_atime;
    arcn->ctime = sb.st_ctime;
    arcn->nlink = sb.st_nlink;
    arcn->dev = sb.st_dev;

    if (S_ISDIR(sb.st_mode))
        arcn->type = S_IFDIR;
    else if (S_ISCHR(sb.st_mode))
        arcn->type = S_IFCHR;
    else if (S_ISBLK(sb.st_mode))
        arcn->type = S_IFBLK;
    else if (S_ISFIFO(sb.st_mode))
        arcn->type = S_IFIFO;
    else if (S_ISLNK(sb.st_mode))
        arcn->type = S_IFLNK;
    else if (S_ISSOCK(sb.st_mode))
        arcn->type = S_IFSOCK;
    else
        arcn->type = S_IFREG;

    return 0;
}
