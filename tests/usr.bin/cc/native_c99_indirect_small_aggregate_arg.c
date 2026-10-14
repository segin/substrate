#include <stdbool.h>
#include <stdio.h>

typedef struct {
    unsigned ch;
    unsigned char len;
} unit_t;

static unit_t scan(const char *s) {
    return (unit_t){(unsigned char)*s, 1};
}

static bool is_blank(unit_t u) {
    return u.ch == ' ';
}

static char *skip_not(const char *buf, const char *lim, bool (*pred)(unit_t)) {
    const char *s = buf;
    for (unit_t u; s < lim && pred(u = scan(s)) == false; s += u.len) {
    }
    return (char *)s;
}

int main(void) {
    char s[] = "a x\n";
    long off = skip_not(s, s + 4, is_blank) - s;
    if (off != 1) {
        printf("off=%ld\n", off);
        return 1;
    }
    return 0;
}
