/*
 * Universal character names in identifiers: \u00e9 is e-acute and
 * \U000003b1 alpha; \u0041 would be 'A', which C11 6.4.3p2 forbids
 * (native_bad_c99_ucn_ident_basic.c).
 */
int main(void) {
    int caf\u00e9 = 7;
    int \U000003b1 = 3;
    return caf\u00e9 + \U000003b1 - 10;
}
