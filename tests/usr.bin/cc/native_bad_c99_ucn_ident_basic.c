/*
 * A universal character name may not name a character of the basic
 * character set (C11 6.4.3p2): \u0041 is 'A'.  gcc rejects it; cc took it.
 */
int main(void) {
    int \u0041 = 7;
    return \u0041 - 7;
}
