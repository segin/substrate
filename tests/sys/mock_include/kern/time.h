/*
 * Minimal mock of <kern/time.h> for the acct_compress host test.
 *
 * acct.c only needs get_time(); the test supplies its own mock definition
 * (returning a uint32_t timestamp).  Declare just that to avoid pulling the
 * real kern/time.h (which lives under sys/kern/, not on this test's include
 * path, and drags in further kernel time/itimer plumbing).
 */
#ifndef _MOCK_KERN_TIME_H
#define _MOCK_KERN_TIME_H

#include <stdint.h>

uint32_t get_time(void);

#endif /* _MOCK_KERN_TIME_H */
