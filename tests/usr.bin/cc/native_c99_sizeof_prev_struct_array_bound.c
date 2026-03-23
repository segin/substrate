#include <stddef.h>

struct flags_alist {
    int name;
    int *value;
};

const struct flags_alist shell_flags[] = {
    {'a', 0},
    {'b', 0},
    {0, 0},
};

#define NUM_SHELL_FLAGS (sizeof(shell_flags) / sizeof(struct flags_alist))

char optflags[NUM_SHELL_FLAGS + 4] = {'+'};
unsigned long long sentinel = 0x1122334455667788ULL;

int main(void) {
    size_t i;

    if (sizeof(optflags) != 7) {
        return 1;
    }
    if (optflags[0] != '+') {
        return 2;
    }
    for (i = 1; i < sizeof(optflags); ++i) {
        if (optflags[i] != 0) {
            return 3;
        }
    }
    if (sentinel != 0x1122334455667788ULL) {
        return 4;
    }
    return 0;
}
