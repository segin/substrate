/*
 * A failed _Static_assert reports its message as text.  cc printed the
 * literal as the lexer stores it, every character an octal escape.
 */
_Static_assert(sizeof(char) == 2, "char is \x31 byte, \101 \"lie\"");

int main(void) {
    return 0;
}
