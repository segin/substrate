/*
 * A block-scope static is in scope in its own initializer (C11 6.2.1p7),
 * and the address of a static of the same block is a constant: cc lowered
 * the initializer before naming the static, so `&self` and `&other[1]`
 * were addresses of globals of the bare names, which nothing defines
 * (Linux's register_nmi_handler(), a static struct nmiaction whose list
 * head points at itself, in a statement expression, at the vmlinux link).
 */
#include <stdio.h>

struct list_head {
    struct list_head *next, *prev;
};

struct action {
    struct list_head list;
    int (*handler)(int);
    const char *name;
};

static int run(struct action *a) {
    return a->handler(a->list.next == &a->list && a->list.prev == &a->list);
}

#define register_handler(fn, n)                                                                                      \
    ({                                                                                                                 \
        static struct action fn##_na = {                                                                               \
            .list = {&(fn##_na.list), &(fn##_na.list)},                                                                \
            .handler = (fn),                                                                                           \
            .name = (n),                                                                                               \
        };                                                                                                             \
        run(&fn##_na);                                                                                                 \
    })

static int handler(int x) {
    return x + 40;
}

int main(void) {
    static void *self = &self;
    static int other[2] = {1, 2};
    static int *second = &other[1];
    static struct list_head head = {&head, &head};

    printf("%d %d %d %d\n", register_handler(handler, "PMI"), self == (void *)&self, *second,
           head.next == &head && head.prev == &head);
    return 0;
}
