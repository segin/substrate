/*
 * futex.h - Fast Userspace Mutex Support
 *
 * Futexes are the basis for userspace synchronization primitives.
 * They provide an efficient wait/wake mechanism where the kernel
 * is only involved when there is contention.
 */

#ifndef _SYS_FUTEX_H
#define _SYS_FUTEX_H

#include <stdint.h>
#include <stddef.h>

/*
 * Futex Operation Codes (Linux ABI compatible)
 */
#define FUTEX_WAIT              0
#define FUTEX_WAKE              1
#define FUTEX_FD                2   /* Deprecated */
#define FUTEX_REQUEUE           3
#define FUTEX_CMP_REQUEUE       4
#define FUTEX_WAKE_OP           5
#define FUTEX_LOCK_PI           6
#define FUTEX_UNLOCK_PI         7
#define FUTEX_TRYLOCK_PI        8
#define FUTEX_WAIT_BITSET       9
#define FUTEX_WAKE_BITSET       10
#define FUTEX_WAIT_REQUEUE_PI   11
#define FUTEX_CMP_REQUEUE_PI    12

/* 
 * Operation Flags (OR'd with operation code)
 */
#define FUTEX_PRIVATE_FLAG      128
#define FUTEX_CLOCK_REALTIME    256
#define FUTEX_CMD_MASK          (~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME))

/* Convenience macros */
#define FUTEX_WAIT_PRIVATE      (FUTEX_WAIT | FUTEX_PRIVATE_FLAG)
#define FUTEX_WAKE_PRIVATE      (FUTEX_WAKE | FUTEX_PRIVATE_FLAG)
#define FUTEX_REQUEUE_PRIVATE   (FUTEX_REQUEUE | FUTEX_PRIVATE_FLAG)

/*
 * Robust Futex Constants
 *
 * Robust futexes handle owner death - when a thread dies holding a mutex,
 * the kernel marks the futex with FUTEX_OWNER_DIED so waiters can recover.
 */

/* Bits in the futex word */
#define FUTEX_WAITERS           0x80000000  /* There are waiters */
#define FUTEX_OWNER_DIED        0x40000000  /* Previous owner died */
#define FUTEX_TID_MASK          0x3FFFFFFF  /* Owner TID mask */

/*
 * Robust List Head Structure (Linux ABI compatible)
 *
 * Each thread can register a list of robust futexes. When the thread exits
 * without unlocking them, the kernel walks this list and marks each futex
 * with FUTEX_OWNER_DIED.
 */
struct robust_list {
    struct robust_list *next;
};

struct robust_list_head {
    struct robust_list  list;          /* Circular list of locks */
    long                futex_offset;  /* Offset of futex word from entry */
    struct robust_list *list_op_pending; /* Entry being locked/unlocked */
};

/*
 * Priority Inheritance (PI) Futex State
 */
#define FUTEX_PI_MAX_BOOST      99  /* Maximum RT priority boost */

/*
 * FUTEX_WAKE_OP operations (val3 encoding)
 */
#define FUTEX_OP_SET        0   /* *(int *)uaddr2 = oparg */
#define FUTEX_OP_ADD        1   /* *(int *)uaddr2 += oparg */
#define FUTEX_OP_OR         2   /* *(int *)uaddr2 |= oparg */
#define FUTEX_OP_ANDN       3   /* *(int *)uaddr2 &= ~oparg */
#define FUTEX_OP_XOR        4   /* *(int *)uaddr2 ^= oparg */

/* Comparison ops for FUTEX_WAKE_OP */
#define FUTEX_OP_CMP_EQ     0   /* oldval == cmparg */
#define FUTEX_OP_CMP_NE     1   /* oldval != cmparg */
#define FUTEX_OP_CMP_LT     2   /* oldval < cmparg */
#define FUTEX_OP_CMP_LE     3   /* oldval <= cmparg */
#define FUTEX_OP_CMP_GT     4   /* oldval > cmparg */
#define FUTEX_OP_CMP_GE     5   /* oldval >= cmparg */

/* Encode/decode macros for val3 */
#define FUTEX_OP(op, oparg, cmp, cmparg) \
    (((op) << 28) | ((cmp) << 24) | (((oparg) & 0xFFF) << 12) | ((cmparg) & 0xFFF))

#define FUTEX_OP_OP(val3)       (((val3) >> 28) & 0xF)
#define FUTEX_OP_CMP(val3)      (((val3) >> 24) & 0xF)
#define FUTEX_OP_OPARG(val3)    (((val3) >> 12) & 0xFFF)
#define FUTEX_OP_CMPARG(val3)   ((val3) & 0xFFF)

/*
 * Syscall Interface
 */

/* Main futex syscall */
int sys_futex(int *uaddr, int op, int val, void *timeout, int *uaddr2, int val3);

/* Robust list management */
int sys_set_robust_list(struct robust_list_head *head, size_t len);
int sys_get_robust_list(int pid, struct robust_list_head **head_ptr, size_t *len_ptr);

/* Called by exit path to cleanup robust futexes */
void futex_exit_cleanup(void);

/* PI futex operations */
int futex_lock_pi(int *uaddr, int detect, int trylock, int private);
int futex_unlock_pi(int *uaddr, int private);

#endif /* _SYS_FUTEX_H */
