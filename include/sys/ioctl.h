#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include <termios.h>

int ioctl(int fd, unsigned long request, ...);

#endif
