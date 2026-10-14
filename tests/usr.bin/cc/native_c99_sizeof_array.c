static int g_numbers[5];

struct row {
    int values[3];
    char tail;
};

static int param_size(int arr[]) {
    return (int)sizeof(arr);
}

int main(void) {
    int local[7];
    struct row r;

    if ((int)sizeof(local) != (int)(sizeof(int) * 7)) {
        return 1;
    }
    if ((int)sizeof(g_numbers) != (int)(sizeof(int) * 5)) {
        return 2;
    }
    if ((int)sizeof(r.values) != (int)(sizeof(int) * 3)) {
        return 3;
    }
    if (param_size(local) != (int)sizeof(int *)) {
        return 4;
    }
    return 0;
}
