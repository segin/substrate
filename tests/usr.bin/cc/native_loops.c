int sum_until(int n) {
    int i = 0;
    int s = 0;

    while (i < n) {
        if (i == 5) {
            i = i + 1;
            continue;
        }
        s = s + i;
        if (s > 50) {
            break;
        }
        i = i + 1;
    }

    return s;
}

int sum_for(void) {
    int i = 0;
    int s = 0;

    for (i = 0; i < 10; i = i + 1) {
        if (i == 3) {
            continue;
        }
        s = s + i;
    }

    return s;
}

int main(void) {
    int a = sum_until(12);
    int b = sum_for();
    return (a - 61) + (b - 42);
}
