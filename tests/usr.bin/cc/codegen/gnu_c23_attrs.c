/*
 * Vendor-prefixed C23 attributes mean what their GNU spelling means:
 * [[gnu::aligned]], [[gnu::packed]] and the rest, also spelled __gnu__::,
 * on objects, structures, members and enumerations; another vendor's
 * attributes are read and ignored.
 *
 * cc-std: c23
 */
#include <stdint.h>
#include <stdio.h>

char pad0;
[[gnu::aligned(64)]] char b2[3];
char pad1;
[[__gnu__::aligned(32)]] int i2;

struct [[gnu::packed]] s { char a; int b; };
struct t { char a; [[gnu::aligned(16)]] int b; };
enum [[gnu::packed]] e { A, B = 3 };
[[gnu::unused, __gnu__::noinline]] static int f(int x) { return x + 1; }
[[clang::warn_unused_result]] static int g(void) { return 2; }

int main(void) {
    printf("%d %d\n", (int)((uintptr_t)b2 % 64), (int)((uintptr_t)&i2 % 32));
    printf("%u %u %u %u\n", (unsigned)sizeof(struct s), (unsigned)sizeof(struct t), (unsigned)_Alignof(struct t),
           (unsigned)sizeof(enum e));
    printf("%d\n", f(1) + g());
    return 0;
}
