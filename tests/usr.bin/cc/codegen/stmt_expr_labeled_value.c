/*
 * A statement expression whose last statement is labeled still has that
 * statement's value: `({ ...; goto out; ...; out: sz; })`.  cc lowered
 * the labeled statement as a statement and gave the expression 0.  Linux's
 * FIND_NEXT_BIT() ends so: every find_next_bit() was 0, and the per-CPU
 * allocator searched for a free area forever.
 */
#include <stdio.h>

static int clamp_up(int x) {
    return ({
        int r = x;
        if (r < 0)
            goto out;
        r++;
    out:
        r;
    });
}

static int twice_labeled(int x) {
    return ({
        int r = x * 3;
    first:
    second:
        r - 1;
    });
}

static unsigned long next_set(const unsigned long *map, unsigned long nbits, unsigned long start) {
    return ({
        unsigned long idx = start / 32, tmp, sz = nbits;
        if (start >= sz)
            goto out;
        for (tmp = map[idx] & (~0UL << (start & 31)); !tmp; tmp = map[idx]) {
            if ((idx + 1) * 32 >= sz)
                goto out;
            idx++;
        }
        sz = idx * 32 + (unsigned long)__builtin_ctzl(tmp);
        if (sz > nbits)
            sz = nbits;
    out:
        sz;
    });
}

int main(void) {
    unsigned long map[3] = {0x00000000UL, 0x00010000UL, 0x80000000UL};

    printf("%d %d %d\n", clamp_up(5), clamp_up(-3), twice_labeled(4));
    printf("%lu %lu %lu %lu\n", next_set(map, 96, 0), next_set(map, 96, 49), next_set(map, 96, 96),
           next_set(map, 80, 49));
    return 0;
}
