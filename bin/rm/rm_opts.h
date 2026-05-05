#ifndef RM_OPTS_H
#define RM_OPTS_H

#include "rm.h"

void rm_options_init(struct rm_options *opts, const char *progname);
int rm_parse_options(struct rm_options *opts, int argc, char **argv,
    const char **err_msg);

#endif