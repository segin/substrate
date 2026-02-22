#include <ctype.h>

enum test_flags {
    FLAG_SHIFT = ((1 << (5)) << 8),
    FLAG_COND = ((5) < 8 ? ((1 << (5)) << 8) : ((1 << (5)) >> 8)),
    FLAG_OTHER = ((2) < 8 ? ((1 << (2)) << 8) : ((1 << (2)) >> 8))
};

int main(void) {
    if (FLAG_SHIFT != FLAG_COND) {
        return 1;
    }
    if (FLAG_COND != 8192) {
        return 2;
    }
    if (FLAG_OTHER != 1024) {
        return 3;
    }
    if (!isspace('\n')) {
        return 4;
    }
    if (isspace('=')) {
        return 5;
    }
    if (isspace('$')) {
        return 6;
    }
    return 0;
}
