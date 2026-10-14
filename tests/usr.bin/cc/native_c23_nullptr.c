int main(void) {
    int *p = nullptr;
    nullptr_t np = nullptr;
    if (p != np)
        return 1;
    return 0;
}
