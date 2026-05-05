#ifndef _SYS_RANDOM_H
#define _SYS_RANDOM_H

#include <sys/types.h>

#define GRND_NONBLOCK   0x0001
#define GRND_RANDOM     0x0002
#define GRND_INSECURE   0x0004

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags);

#endif /* _SYS_RANDOM_H */