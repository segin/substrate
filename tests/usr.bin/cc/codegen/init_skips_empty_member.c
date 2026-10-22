/*
 * A designated initializer that skips a member with no parts -- an empty
 * structure (GNU) or a zero-length array -- leaves it as it is.  cc filled
 * the gap with `{0}`, an initializer too many for a part with nothing in
 * it, and refused the declaration ("too many initializers for struct
 * vmg"): Linux's vma_merge_struct has a vm_userfaultfd_ctx, empty without
 * userfaultfd, and init_task and init_mm have such members too.
 */
#include <stdio.h>

struct empty {};

struct merge {
    long start;
    union {
        unsigned long vm_flags;
        int narrow;
    };
    struct empty uffd_ctx;
    int tail[0];
    int state;
    _Bool expand : 1;
};

struct merge global_m = {.start = 2, .state = 9};

static struct merge arr[2] = {[1] = {.vm_flags = 5, .expand = 1}};

int main(int argc, char **argv) {
    struct merge m = {.start = argc, .narrow = 3, .state = 7, .expand = 1};
    struct merge n = {.state = argc + 1};

    (void)argv;
    printf("%ld %d %d %d\n", m.start, m.narrow, m.state, (int)m.expand);
    printf("%ld %lu %d %d\n", n.start, n.vm_flags, n.state, (int)n.expand);
    printf("%ld %d\n", global_m.start, global_m.state);
    printf("%lu %d %lu %d\n", arr[1].vm_flags, (int)arr[1].expand, arr[0].vm_flags, arr[0].state);
    return 0;
}
