/*
 * <memory.h> — historical SVR4 / SUS-legacy alias for <string.h>.
 *
 * Older code (pre-POSIX-2001 Unix utilities, ported BSD daemons,
 * OpenSSL's pkcs7 module) still references this header for memcpy /
 * memset / memcmp.  POSIX deprecated it in favor of <string.h> but
 * every libc that wants to compile that code still ships it as a
 * one-line forwarder.
 */
#ifndef _MEMORY_H
#define _MEMORY_H

#include <string.h>

#endif
