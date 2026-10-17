/* nullptr converts only to a pointer, bool or nullptr_t (C23 6.3.2.4) */
int main(void) {
    int n = nullptr;
    return n;
}
