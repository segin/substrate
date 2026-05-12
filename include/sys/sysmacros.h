/*
 * <sys/sysmacros.h> — major()/minor()/makedev() for split dev_t.
 *
 * Substrate currently uses a 16-bit dev_t laid out as
 *   ((major & 0xFF) << 8) | (minor & 0xFF)
 * matching the original Unix / pre-glibc-2.6 encoding.  Encoders and
 * decoders MUST go through these macros so the day we widen dev_t to
 * 32 or 64 bits we only have one place to touch.
 */
#ifndef _SYS_SYSMACROS_H
#define _SYS_SYSMACROS_H

#include <sys/types.h>

#define major(dev)        ((unsigned)(((dev) >> 8) & 0xFF))
#define minor(dev)        ((unsigned)((dev) & 0xFF))
#define makedev(maj, min) ((dev_t)((((maj) & 0xFF) << 8) | ((min) & 0xFF)))

#endif /* _SYS_SYSMACROS_H */
