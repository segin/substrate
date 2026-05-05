#ifndef ECHO_OPTS_H
#define ECHO_OPTS_H

#include "echo.h"

void echo_options_init(struct echo_options *options, const char *progname);
int echo_parse_options(struct echo_options *options, int argc, char **argv);

#endif