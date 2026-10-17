/*
 * ++ and -- on 64-bit integers wherever they live (CC-104): a local, a
 * global, an array element, a member, through a pointer.  On i386 the
 * step was a 32-bit add, so 0xffffffff + 1 lost its carry.  The values
 * come through a function, so nothing is folded.
 */
#include <stdio.h>

static unsigned long long id(unsigned long long x) {
    return x;
}

unsigned long long g = 0xffffffffULL;
struct s {
    long long a;
    unsigned long long b;
} st;

int main(void) {
    unsigned long long n = id(0xffffffffULL);
    long long m = (long long)id(0x7fffffffffffffffULL) - 0x100000000LL;
    long long arr[2];
    long long *p;
    unsigned long long *q = &n;
    unsigned long long post;

    n++;
    ++n;
    m++;
    --m;
    g++;
    arr[0] = (long long)id(0x1ffffffffULL);
    arr[0]++;
    p = &arr[0];
    (*p)++;
    st.a = (long long)id(0xffffffffULL);
    st.a++;
    st.b = id(0x100000000ULL);
    st.b--;
    post = (*q)++;
    printf("%llx %llx %llx %llx %llx %llx %llx\n", n, (unsigned long long)m, g, (unsigned long long)arr[0],
           (unsigned long long)st.a, st.b, post);
    return 0;
}
