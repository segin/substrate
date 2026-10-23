/*
 * Objects a program puts in a section of its own are walked as an array
 * of them, so each is aligned as its type is and no more.  cc aligned
 * every object of 8 bytes or more to 8, padding Linux's 28-byte
 * card_info entries in .videocards to 32; probe_cards() stepped 28 bytes
 * at a time and called into the middle of the next entry.
 */
#include <stdio.h>

struct entry {
    int probe;
    int a, b, c, f;
    unsigned short d, e;
    int g;
};

#define ENTRY __attribute__((section("cc_entries"), used))

static ENTRY struct entry e1 = {1, 1, 2, 3, 4, 5, 6, 7};
static ENTRY struct entry e2 = {2, 7, 8, 9, 10, 11, 12, 13};

int main(void) {
    long gap = (const char *)&e2 - (const char *)&e1;

    if (gap < 0) {
        gap = -gap;
    }
    /* next to each other, as an array of them would be */
    printf("adjacent %d\n", gap == (long)sizeof(struct entry));
    printf("sum %d\n", e1.probe + e2.probe + e1.e + e2.e);
    return 0;
}
