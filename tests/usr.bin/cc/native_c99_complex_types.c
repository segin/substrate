int main(void) {
    double _Complex z = 1.0;
    float _Imaginary y = 2.0f;

    if (_Generic(z, double _Complex : 1, default : 0) != 1)
        return 1;
    if (_Generic(y, float _Imaginary : 1, default : 0) != 1)
        return 2;
    if (sizeof(z) != sizeof(double _Complex))
        return 3;
    if (sizeof(y) != sizeof(float _Imaginary))
        return 4;
    return 0;
}
