typedef int (*__compar_fn_t)(const void *, const void *);

int main(void) {
    __compar_fn_t fn = 0;
    return fn != 0;
}
