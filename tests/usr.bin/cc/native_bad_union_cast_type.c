/* A GNU cast to union type needs a member of the operand's type: a
 * short does not become the int member, nor 0 the pointer member. */
union value {
    int i;
    char *p;
};

union value from_short(short s) {
    return (union value)s;
}

int main(void) {
    return from_short(1).i;
}
