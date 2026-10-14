#include <stdlib.h>

struct pair {
    const char *name;
    int value;
};

int main(void) {
    struct pair *arr = (struct pair *)calloc(3, sizeof(struct pair));
    int idx = 1;
    if (!arr) {
        return 1;
    }

    arr[idx].name = "ok";
    arr[idx].value = 42;

    if (arr[idx].name[0] != 'o' || arr[idx].name[1] != 'k' || arr[idx].value != 42) {
        free(arr);
        return 2;
    }

    free(arr);
    return 0;
}
