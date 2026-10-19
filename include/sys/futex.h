#ifndef _SYS_FUTEX_H
#define _SYS_FUTEX_H

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

/* Priority-inheritance futexes: the word holds the owner's thread id. */
#define FUTEX_LOCK_PI    6
#define FUTEX_UNLOCK_PI  7
#define FUTEX_TRYLOCK_PI 8

#define FUTEX_PRIVATE_FLAG 128

/* Bits in a PI futex word. */
#define FUTEX_WAITERS    0x80000000  /* there are waiters */
#define FUTEX_OWNER_DIED 0x40000000  /* the previous owner died */
#define FUTEX_TID_MASK   0x3FFFFFFF  /* owner thread id */

#endif
