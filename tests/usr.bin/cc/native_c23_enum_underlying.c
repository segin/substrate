enum small : unsigned char {
    S_A = 1,
    S_B = 255
};

int main(void) {
    enum small v = S_B;
    return v == 255 ? 0 : 1;
}
