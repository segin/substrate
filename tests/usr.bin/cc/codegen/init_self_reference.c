/*
 * A local is in scope from the end of its declarator (C11 6.2.1p7), so
 * in its own initializer.  Linux relies on it twice: kzalloc_obj is
 * `struct probe *base = ({ ... typeof(*base) ... })`, which cc refused
 * ("cannot determine the type of typeof's operand"), and LIST_HEAD(x) is
 * `struct list_head x = { &(x), &(x) }`, where cc's &x named no object
 * yet -- as did `void *p = &p`, which stored an unset value.
 */
#include <stdio.h>

struct list_head {
    struct list_head *next, *prev;
};

struct probe {
    int a;
    long b;
};

int main(void) {
    struct probe *base = (typeof(*base) *)0;
    unsigned long n = sizeof(*base) + sizeof(n);
    void *self = &self;
    struct list_head head = {&(head), &(head)};
    void *arr[2] = {arr, &arr[1]};
    int k = sizeof(k) * 2;

    printf("%lu %d %d\n", n, base == 0, self == (void *)&self);
    printf("%d %d\n", head.next == &head, head.prev == &head);
    printf("%d %d %d\n", arr[0] == (void *)arr, arr[1] == (void *)&arr[1], k);
    return 0;
}
