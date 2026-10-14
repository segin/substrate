static int is_dot_string(const char *s) {
    return s[0] == '.' && s[1] == '\0';
}

int main(void) {
    char decimal_point = '.';
    const char *p = ((char[]){decimal_point, 0});

    if (!is_dot_string(p)) {
        return 1;
    }
    if (!is_dot_string(((char[]){decimal_point, 0}))) {
        return 2;
    }

    return 0;
}
