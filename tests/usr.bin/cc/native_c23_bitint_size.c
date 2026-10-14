int main(void) {
    if (sizeof(_BitInt(1)) != 1)
        return 1;
    if (sizeof(_BitInt(9)) != 2)
        return 2;
    if (sizeof(unsigned _BitInt(33)) != 5)
        return 3;
    return 0;
}
