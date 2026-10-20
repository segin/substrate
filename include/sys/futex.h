#ifndef _SYS_FUTEX_H
#define _SYS_FUTEX_H

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

/* Priority-inheritance futexes: the word holds the owner's thread id. */
#define FUTEX_LOCK_PI    6
#define FUTEX_UNLOCK_PI  7
#define FUTEX_TRYLOCK_PI 8

/* Waits and wakes filtered by a bitset; FUTEX_WAIT_BITSET's timeout is an
 * absolute CLOCK_REALTIME time. */
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10
#define FUTEX_BITSET_MATCH_ANY 0xffffffff

#define FUTEX_PRIVATE_FLAG 128

/* Bits in a PI futex word. */
#define FUTEX_WAITERS    0x80000000  /* there are waiters */
#define FUTEX_OWNER_DIED 0x40000000  /* the previous owner died */
#define FUTEX_TID_MASK   0x3FFFFFFF  /* owner thread id */

#endif
