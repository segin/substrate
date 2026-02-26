[[nodiscard]] int must_use(void) {
    return 1;
}

[[deprecated("use new_api")]] int old_api(void) {
    return 2;
}

[[maybe_unused]] static int spare = 0;

[[noreturn]] void stop_now(void) {
    for (;;)
        ;
}

[[reproducible]] int rep_add(int x) {
    return x + 1;
}

[[unsequenced]] int unseq_mul(int x) {
    return x * 2;
}

int main(void) {
    int x = rep_add(4) + unseq_mul(3);
    old_api();
    must_use();
    switch (x) {
    case 10:
        [[fallthrough]];
    case 11:
        return 0;
    default:
        return 1;
    }
}
