int main(void) {
    int x = 1;
#if __has_builtin(__builtin_prefetch)
    __builtin_prefetch(&x, 1, 3);
    __builtin_prefetch(&x);
#endif
    return(x - 1);
}
