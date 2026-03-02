static int starts_with_main(const char *s) {
    return s != 0 && s[0] == 'm' && s[1] == 'a' && s[2] == 'i' && s[3] == 'n';
}

int main(void) {
    const char *a = __func__;
    const char *b = __FUNCTION__;
    const char *c = __PRETTY_FUNCTION__;
    if (!starts_with_main(a)) {
        return 1;
    }
    if (!starts_with_main(b)) {
        return 2;
    }
    if (!starts_with_main(c)) {
        return 3;
    }
    return 0;
}
