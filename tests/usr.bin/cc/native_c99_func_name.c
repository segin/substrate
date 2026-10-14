static int check_name(void) {
    const char *p = __func__;
    const char *q = "check_name";
    while (*p != '\0' && *q != '\0') {
        if (*p != *q) {
            return 1;
        }
        p++;
        q++;
    }
    return (*p == '\0' && *q == '\0') ? 0 : 1;
}

int main(void) {
    if (check_name() != 0) {
        return 1;
    }
    if (__func__[0] != 'm') {
        return 2;
    }
    if (__func__[1] != 'a') {
        return 3;
    }
    return 0;
}
