/*
 * GNU attributes apply to an enumerated type wherever gcc accepts them:
 * after `enum` or after its list.  `packed` makes the type as small as its
 * values allow.
 */
#include <stdio.h>

enum __attribute__((packed)) e1 { A1, B1 = 200 };
enum __attribute__((packed)) e2 { A2 = -1, B2 = 100 };
enum e3 { A3, B3 = 40000 } __attribute__((packed));
enum __attribute__((packed)) { C4 = 1 } c4;
typedef enum __attribute__((packed)) { E5 = 70000 } big5;
enum __attribute__((unused)) e6 { A6 };

int main(void) {
    enum e1 x = B1;
    enum e2 y = A2;

    printf("%u %u %u %u %u %u\n", (unsigned)sizeof(enum e1), (unsigned)sizeof(enum e2), (unsigned)sizeof(enum e3),
           (unsigned)sizeof c4, (unsigned)sizeof(big5), (unsigned)sizeof(enum e6));
    printf("%d %d %d\n", (int)x, (int)y, (int)(y < 0));
    return 0;
}
