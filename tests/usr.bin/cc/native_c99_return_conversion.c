static long long ret_ll(void) {
    return 7;
}

static int *ret_null_ptr(void) {
    return 0;
}

static int ret_from_char(void) {
    char c = 5;
    return c;
}

int main(void) {
    if (ret_ll() != 7) {
        return 1;
    }
    if (ret_null_ptr() != 0) {
        return 2;
    }
    if (ret_from_char() != 5) {
        return 3;
    }
    return 0;
}
