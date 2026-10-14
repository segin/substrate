#include <stddef.h>

typedef struct {
    int a;
    int b;
    void *p;
} elem_t;

typedef elem_t elem_array_t[1];

struct holder {
    elem_array_t value;
    unsigned long count;
};

int main(void) {
    if (sizeof(elem_array_t) != sizeof(elem_t)) {
        return 1;
    }
    if (offsetof(struct holder, count) != sizeof(elem_t)) {
        return 2;
    }
    if (sizeof(struct holder) != sizeof(elem_t) + sizeof(unsigned long)) {
        return 3;
    }
    return 0;
}
