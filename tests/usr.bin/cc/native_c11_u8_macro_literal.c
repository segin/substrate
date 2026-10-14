#define u8 broken_macro_should_not_expand

int main(void) {
    const char *s = u8"ok";
    return (s[0] == 'o' && s[1] == 'k' && s[2] == '\0') ? 0 : 1;
}
