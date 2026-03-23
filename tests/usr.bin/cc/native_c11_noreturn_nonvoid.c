_Noreturn static int never_returns(void) {
    for (;;)
        ;
}

int main(void) {
    if (0)
        return never_returns();
    return 0;
}
