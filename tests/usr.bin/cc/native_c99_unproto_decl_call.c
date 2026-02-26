int addmix();

int addmix(int a, double b) {
    return a + (int)b;
}

int main(void) {
    float f = 3.75f;
    return addmix((char)2, f) - 5;
}
