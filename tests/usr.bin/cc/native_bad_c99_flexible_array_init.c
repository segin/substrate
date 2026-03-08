struct flex {
    int n;
    int data[];
};

int main(void) {
    struct flex bad = {
        .n = 1,
        .data = {2}
    };
    (void)bad;
    return 0;
}
