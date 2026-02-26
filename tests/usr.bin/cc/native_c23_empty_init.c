struct point {
    int x;
    int y;
};

int main(void) {
    int scalar = {};
    struct point p = {};
    int arr[2] = {};
    return scalar + p.x + p.y + arr[0] + arr[1];
}
