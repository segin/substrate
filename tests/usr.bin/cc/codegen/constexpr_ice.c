/*
 * A constexpr object of integer type is a named constant (C23 6.6p7): its
 * name is an integer constant expression wherever one is required -- an
 * array bound, an enumerator, a case label, a static assertion, a static
 * initializer -- with its value converted to its type.  It is still an
 * object: its address may be taken.  A name that shadows it is not a
 * constant.
 *
 * cc-std: c23
 * cc-gcc: 14 (-std=c23)
 */
#include <stdio.h>

constexpr int N = 4;
constexpr long M = N * 2 + 1;
static constexpr unsigned char U = 200;
constexpr _Bool B = 1;
constexpr int Br = {6};
_Static_assert(N == 4, "N");
_Static_assert(M == 9, "M");
_Static_assert(U + 1 == 201, "U is unsigned char");
int arr[N];
int g = M * 3;
static int sg[Br] = {[Br - 1] = 7};
enum { E = N + 1 };

static int bound(int n) {
    int N = n; /* not a constant: a[N] is variably modified */
    int a[N];

    return (int)(sizeof a / sizeof a[0]);
}

int main(void) {
    constexpr int L = 3;
    const int *pn = &N;
    int x = 2;
    char local[L * N];

    switch (x) {
    case N - 2:
        printf("two\n");
        break;
    case L:
        printf("three\n");
        break;
    default:
        printf("other\n");
    }
    _Static_assert(L * N == 12, "local");
    printf("%zu %d %ld %u %d %d %d %d\n", sizeof arr / sizeof arr[0], E, M, U, L, g, sg[5], B);
    printf("%zu %d %d\n", sizeof local, *pn, bound(5));
    return 0;
}
