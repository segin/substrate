/*
 * Wide string literals: L"" (wchar_t, int), u"" (char16_t, unsigned short)
 * and U"" (char32_t, unsigned int), with universal character names and
 * narrow pieces joined on.  Every one was emitted as its narrow bytes, u""
 * and U"" both had int elements, and L"ab" "cd" became narrow "abcd".
 */
#include <stdio.h>
#include <stddef.h>
#include <uchar.h>
const wchar_t *gw = L"glé";
static const char16_t *g16 = u"x€";
struct s { int n; const char32_t *p; } gs = {1, U"q\U0001F600"};
wchar_t warr[] = L"abé";
char16_t harr[] = u"hi";
int main(void) {
    const wchar_t *s = L"ab" "cd";
    const char32_t *u = U"\U0001F600z";
    const char16_t *h = u"h€";
    printf("%d %d %d %d %d | %x %d %d | %d %d %d\n", (int)s[0], (int)s[1], (int)s[2], (int)s[3], (int)s[4],
           (unsigned)u[0], (int)u[1], (int)u[2], h[0], h[1], h[2]);
    printf("%d %d %d %d | %x %x | %d %d %d %u | %d %d %u\n", (int)gw[0], (int)gw[2], (int)gw[3], g16[1], (unsigned)gs.p[0],
           (unsigned)gs.p[1], (int)warr[0], (int)warr[2], (int)warr[3], (unsigned)sizeof warr, harr[0], harr[2],
           (unsigned)sizeof harr);
    printf("%u %u %u %u\n", (unsigned)sizeof(L"ab" "cd"), (unsigned)sizeof(u"ab"), (unsigned)sizeof(U"\U0001F600z"),
           (unsigned)sizeof(L"é"));
    return 0;
}
