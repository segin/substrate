int *p;

int main(int c, char **v) {
    (void)v;
    return (int)sizeof(c ? p : 1);
}
