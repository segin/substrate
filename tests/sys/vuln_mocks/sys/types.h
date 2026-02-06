#ifndef _MOCK_SYS_TYPES_H
#define _MOCK_SYS_TYPES_H

// Delegate to host sys/types.h
#include_next <sys/types.h>

#include <stdint.h>
#include <stddef.h>

// Ensure types used by kernel headers are defined
// Host sys/types.h usually defines off_t, uid_t, gid_t, dev_t, ino_t, mode_t.
// But check for differences.

#endif
