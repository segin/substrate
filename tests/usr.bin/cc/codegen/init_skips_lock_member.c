/*
 * A designated initializer that skips a member whose first scalar lies
 * through nested aggregates leaves it zero.  cc filled such a gap with
 * `{0}`, and when the first part leads to an empty structure there is no
 * scalar for the 0 to initialize, so the global was refused ("too many
 * nested struct initializers").  A uniprocessor Linux's spinlock_t is
 * that: a union of a raw_spinlock of an empty arch_spinlock_t, skipped by
 * init_struct_pid and others.  The gap now gets the zero braced to its
 * first scalar, `{}` where there is none.
 */
#include <stdio.h>

typedef struct {
} arch_spinlock_t;

struct raw_spinlock {
    arch_spinlock_t raw_lock;
};

typedef struct spinlock {
    union {
        struct raw_spinlock rlock;
    };
} spinlock_t;

struct hlist_head {
    void *first;
};

struct nested {
    struct {
        struct {
            int deep;
        } in;
        int after;
    } first;
    int last;
};

struct pid {
    int count;
    unsigned int level;
    spinlock_t lock;
    struct nested n;
    struct hlist_head tasks[3];
    int tail;
};

struct pid init_struct_pid = {
    .count = 1,
    .tasks = {{.first = (void *)0}, {.first = (void *)0}, {.first = (void *)0}},
    .level = 2,
    .tail = 9,
};

static spinlock_t pidmap_lock = (spinlock_t){{.rlock = {.raw_lock = {}}}};

int main(int argc, char **argv) {
    struct pid local = {.count = argc, .tail = 4};

    (void)argv;
    (void)pidmap_lock;
    printf("%d %u %d %d %d\n", init_struct_pid.count, init_struct_pid.level, init_struct_pid.n.first.in.deep,
           init_struct_pid.n.last, init_struct_pid.tail);
    printf("%d %d %d %d\n", local.count, local.n.first.after, local.tasks[2].first == (void *)0, local.tail);
    return 0;
}
