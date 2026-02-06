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

#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CMD_MASK ~FUTEX_PRIVATE_FLAG

#define FUTEX_WAITERS 0x80000000
#define FUTEX_OWNER_DIED 0x40000000
#define FUTEX_TID_MASK 0x3fffffff

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
int futex_lock_pi(int *uaddr, int detect, int trylock);
int futex_unlock_pi(int *uaddr);

#endif
