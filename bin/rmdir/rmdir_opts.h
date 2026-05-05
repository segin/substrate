#ifndef RMDIR_OPTS_H
#define RMDIR_OPTS_H

#include "rmdir.h"

void rmdir_options_init(struct rmdir_options *opts, const char *progname);
int rmdir_parse_options(struct rmdir_options *opts, int argc, char **argv,
    const char **err_msg);

#endif