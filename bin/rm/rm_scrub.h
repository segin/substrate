#ifndef RM_SCRUB_H
#define RM_SCRUB_H

#include <stddef.h>
#include <sys/types.h>

/* BSD-style 3-pass overwrite of a regular file open at `fd` whose
 * total length is `len`.  Returns 0 on success, -1 with errno on
 * write failure.  Non-regular files SHALL be skipped by the caller.
 *
 * Pass 1: 0xFF
 * Pass 2: 0x00
 * Pass 3: 0xFF
 * fsync after each pass.
 */
int rm_scrub_file(int fd, off_t len);

#endif
