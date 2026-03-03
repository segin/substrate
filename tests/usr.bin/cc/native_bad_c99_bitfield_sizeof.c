struct bad_sizeof {
    unsigned value:3;
};

int bad_sizeof_fn(void) {
    struct bad_sizeof v;
    return (int)sizeof(v.value);
}
