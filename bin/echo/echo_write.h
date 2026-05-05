#ifndef ECHO_WRITE_H
#define ECHO_WRITE_H

#include <stddef.h>

int echo_write_all(int fd, const unsigned char *data, size_t len);

#endif