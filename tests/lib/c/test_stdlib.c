void test_realloc_edge_cases(void) {
    // Test realloc(NULL, size) behaves like malloc
    void *ptr1 = tested_realloc(NULL, 128);
    assert(ptr1 != NULL);
    struct block_meta *block1 = (struct block_meta *)ptr1 - 1;
    assert(block1->free == 0);

    // Test realloc(ptr, 0) behaves like free and returns NULL
    void *ptr2 = tested_realloc(ptr1, 0);
    assert(ptr2 == NULL);
    assert(block1->free == 1);

    printf("test_realloc_edge_cases passed\n");
}

void test_realloc(void) {
    void *ptr = tested_malloc(10);
    assert(ptr != NULL);

    // realloc with size 0 should free the pointer and return NULL
    void *new_ptr = tested_realloc(ptr, 0);
    assert(new_ptr == NULL);

    printf("test_realloc passed\n");
}

void test_calloc(void) {
    // Test basic allocation
    int *arr = tested_calloc(4, sizeof(int));
    assert(arr != NULL);
    for (int i = 0; i < 4; i++) {
        assert(arr[i] == 0);
    }
    tested_free(arr);

    // Test zero allocation
    void *p = tested_calloc(0, 10);
    assert(p == NULL);

    p = tested_calloc(10, 0);
    assert(p == NULL);

    // Test overflow
    assert(tested_calloc(SIZE_MAX, 2) == NULL);
    assert(tested_calloc(2, SIZE_MAX) == NULL);
    assert(tested_calloc(SIZE_MAX / 2 + 1, 2) == NULL);
    assert(tested_calloc(SIZE_MAX, SIZE_MAX) == NULL);
    assert(tested_calloc(SIZE_MAX / 4, 5) == NULL);

    printf("test_calloc passed\n");
}

int main(void) {
    printf("Running stdlib tests...\n");
    test_atoi_basic();
    test_atoi_whitespace();
    test_atoi_sign();
    test_atoi_invalid();
    test_atol_basic();
    test_strtol();
    test_abs();
    test_labs();
    test_llabs();
    test_realloc_zero_size();
    test_getopt_basic();
    test_getopt_with_args();
    test_getopt_errors();
    test_getopt_end_of_options();
    test_realloc_edge_cases();
    test_realloc();
    test_calloc();
    printf("All tests passed!\n");
    return 0;
}
