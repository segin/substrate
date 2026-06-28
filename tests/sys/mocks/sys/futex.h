#ifndef _SYS_FUTEX_H
#define _SYS_FUTEX_H
/* Constants from futex.h */
#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_FD 2
#define FUTEX_REQUEUE 3
#define FUTEX_CMP_REQUEUE 4
#define FUTEX_WAKE_OP 5
#define FUTEX_LOCK_PI 6
#define FUTEX_UNLOCK_PI 7
#define FUTEX_TRYLOCK_PI 8
#define FUTEX_WAIT_BITSET 9
#define FUTEX_WAKE_BITSET 10

#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CMD_MASK ~FUTEX_PRIVATE_FLAG

#define FUTEX_WAITERS 0x80000000
#define FUTEX_OWNER_DIED 0x40000000
#define FUTEX_TID_MASK 0x3fffffff

/* Bitset match-any value for FUTEX_WAIT_BITSET / FUTEX_WAKE_BITSET. */
#define FUTEX_BITSET_MATCH_ANY  0xffffffff

/* FUTEX_WAKE_OP operations (val3 encoding) */
#define FUTEX_OP_SET        0
#define FUTEX_OP_ADD        1
#define FUTEX_OP_OR         2
#define FUTEX_OP_ANDN       3
#define FUTEX_OP_XOR        4
#define FUTEX_OP_OPARG_SHIFT 8

/* Comparison ops for FUTEX_WAKE_OP */
#define FUTEX_OP_CMP_EQ     0
#define FUTEX_OP_CMP_NE     1
#define FUTEX_OP_CMP_LT     2
#define FUTEX_OP_CMP_LE     3
#define FUTEX_OP_CMP_GT     4
#define FUTEX_OP_CMP_GE     5

/* Encode/decode macros for val3 */
#define FUTEX_OP(op, oparg, cmp, cmparg) \
    (((op) << 28) | ((cmp) << 24) | (((oparg) & 0xFFF) << 12) | ((cmparg) & 0xFFF))

#define FUTEX_OP_OP(val3)       (((val3) >> 28) & 0xF)
#define FUTEX_OP_CMP(val3)      (((val3) >> 24) & 0xF)
#define FUTEX_OP_OPARG(val3)    (((val3) >> 12) & 0xFFF)
#define FUTEX_OP_CMPARG(val3)   ((val3) & 0xFFF)

struct robust_list {
    struct robust_list *next;
};
struct robust_list_head {
    struct robust_list list;
    long futex_offset;
    struct robust_list *list_op_pending;
};

int sys_set_robust_list(struct robust_list_head *head, size_t len);
int sys_get_robust_list(int pid, struct robust_list_head **head_ptr, size_t *len_ptr);
int futex_lock_pi(int *uaddr, int detect, int trylock, int private);
int futex_unlock_pi(int *uaddr, int private);

#endif
