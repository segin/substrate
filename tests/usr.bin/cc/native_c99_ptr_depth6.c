int main(void) {
    int v = 17;
    int *p1 = &v;
    int **p2 = &p1;
    int ***p3 = &p2;
    int ****p4 = &p3;
    int *****p5 = &p4;
    int ******p6 = &p5;
    return ******p6 - 17;
}
