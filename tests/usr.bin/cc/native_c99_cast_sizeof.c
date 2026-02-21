int main(void) {
    double d = 3.75;
    int x = (int)d;
    long long y = (long long)(x + 2);
    int s1 = sizeof(int);
    int s2 = sizeof d;
    int s3 = sizeof(char);
    int s4 = sizeof(long long);
    int s5 = sizeof(float);
    return x + (int)y + s1 + s2 + s3 + s4 + s5 - (3 + 5 + 4 + 8 + 1 + 8 + 4);
}
