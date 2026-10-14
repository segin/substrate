int ptr_size(void) {
    return (int)sizeof(void *);
}

int ptrptr_size(void) {
    return (int)sizeof(int **);
}

int main(void) {
    if (ptr_size() != (int)sizeof(int *)) {
        return 1;
    }
    if (ptr_size() != 8) {
        return 2;
    }
    if (ptrptr_size() != 8) {
        return 3;
    }
    return 0;
}
