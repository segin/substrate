struct bad_addr {
    unsigned value:3;
};

int *bad_addr_fn(void) {
    struct bad_addr v;
    return &v.value;
}
