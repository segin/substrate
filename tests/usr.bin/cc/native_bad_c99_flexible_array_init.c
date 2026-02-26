struct flex {
    int n;
    int data[];
};

struct flex bad = {
    .n = 1,
    .data = {2}
};

int main(void) {
    return 0;
}
