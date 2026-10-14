int main(void) {
    int i = 0;

    ;
    for (i = 0; i < 5; i = i + 1)
        ;
    if (i != 5) {
        return 1;
    }

    while ((i = i + 1) < 9)
        ;
    if (i != 9) {
        return 2;
    }

    if (i == 9)
        ;
    else
        ;

    return 0;
}
