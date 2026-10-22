/*
 * From -O1 cc follows what a function keeps in its own stack slots whose
 * addresses do not escape: a slot stored once is read as its value, a
 * small structure is split into one slot per field (its zeroing memset
 * into a 0 stored to each), and a compare of a slot only stored
 * constants folds where the set it can hold decides it.  Linux's
 * scoped_seqlock_read() needs all three for the canary in the state that
 * cannot occur to go (run_inline_dead_call.sh).  The values must be what
 * the program computes: a structure initialized and read by field, one
 * updated through a pointer by an inlined function, a state machine
 * across a loop like the seqlock's, a slot whose state the loop leaves
 * undecided, and a structure whose address escapes, which stays whole.
 */
#include <stdio.h>

struct pair {
    int a;
    long b;
    short c;
};

struct st {
    int state;
    unsigned long data;
    int *lock;
};

enum { DONE = 0, LOCKED = 1, LOCKLESS = 3 };

static int retries;

static int retry(unsigned long seq) {
    return retries-- > 0 && seq != 0;
}

static inline __attribute__((__always_inline__)) void next(struct st *s, int *lock) {
    switch (s->state) {
    case DONE:
        printf(" never");
        return;
    case LOCKED:
        s->state = DONE;
        return;
    case LOCKLESS:
        if (!retry(s->data)) {
            s->state = DONE;
            return;
        }
        break;
    }
    s->lock = lock;
    s->state = LOCKED;
}

static void escape(struct pair *p) {
    p->a += 100;
}

int main(int argc, char **argv) {
    int k = argc - 1; /* 0, unknown to the compiler */
    struct pair p = {.a = 1 + k, .c = 3};
    struct pair q = {.b = 7};
    struct st s = {.state = LOCKLESS, .data = 5 + (unsigned long)k};
    int lock = 0;
    int passes = 0;
    int undecided = k ? 1 : 2;
    int i;

    (void)argv;
    printf("p %d %ld %d\n", p.a, p.b, p.c);
    retries = 2;
    for (; s.state != DONE; next(&s, &lock)) {
        passes++;
    }
    printf("seq %d %d %lu %d\n", passes, s.state, s.data, s.lock == &lock);
    for (i = 0; i < 3; ++i) {
        if (undecided == 2) {
            undecided = 4;
        } else if (undecided == 4) {
            undecided = 2;
        }
    }
    printf("undecided %d\n", undecided);
    escape(&q);
    printf("q %d %ld %d\n", q.a, q.b, q.c);
    return 0;
}
