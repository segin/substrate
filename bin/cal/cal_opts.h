#ifndef CAL_OPTS_H
#define CAL_OPTS_H

#include "cal.h"

void cal_options_init(struct cal_options *opts, const char *progname);
int cal_parse_options(struct cal_options *opts, int argc, char **argv,
    const char **err_msg);

#endif