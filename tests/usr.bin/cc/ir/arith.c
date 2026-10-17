/* integer arithmetic: narrowing to the type, shifts, signed and unsigned compares */
unsigned char narrow(int a, int b) { return (unsigned char)(a * b + 7); }
int shifts(int a, unsigned b) { return (a << 3) + (int)(b >> 2) + (a >> 1); }
int cmp(int a, unsigned b, long c) { return (a < 0) + (b < 5u) + (c >= a); }
