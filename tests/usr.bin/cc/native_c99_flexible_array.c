struct flex {
    int n;
    int data[];
};

int main(void) {
    return sizeof(struct flex) == (int)sizeof(int) ? 0 : 1;
}
