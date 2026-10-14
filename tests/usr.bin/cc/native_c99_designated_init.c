struct inner {
    int x;
    int y;
};

union val {
    int i;
    char c;
};

struct outer {
    int tag;
    struct inner in;
    union val u;
    int arr[4];
};

int main(void) {
    struct outer o = {
        .arr = {[2] = 9, [0] = 4},
        .in = {.y = 5, .x = 3},
        .u = {.c = 7},
        .tag = 11
    };

    if (o.tag != 11) {
        return 1;
    }
    if (o.in.x != 3 || o.in.y != 5) {
        return 2;
    }
    if (o.u.c != 7) {
        return 3;
    }
    if (o.arr[0] != 4 || o.arr[1] != 0 || o.arr[2] != 9 || o.arr[3] != 0) {
        return 4;
    }
    return 0;
}
