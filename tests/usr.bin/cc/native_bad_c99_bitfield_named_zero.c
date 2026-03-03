struct bad_named_zero {
    unsigned value:0;
};

int bad_named_zero_fn(void) {
    return 0;
}
