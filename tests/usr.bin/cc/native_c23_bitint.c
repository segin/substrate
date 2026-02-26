int main(void) {
    _BitInt(17) a = 100;
    unsigned _BitInt(40) b = 5;
    return (int)(a + (_BitInt(17))b) - 105;
}
