struct pair {
    int a;
    int b;
};

int main(void) {
    int x = (int){5};
    int y = ((int){2}) + ((int){3});
    int z = ((struct pair){.a = 4, .b = 6}).b;

    if (x != 5) {
        return 1;
    }
    if (y != 5) {
        return 2;
    }
    if (z != 6) {
        return 3;
    }
    return 0;
}
