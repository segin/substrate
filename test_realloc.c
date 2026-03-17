#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void test_push_param() {
    int i;
    int **arr = NULL;
    int count = 0;
    clock_t start = clock();
    for (i = 0; i < 10000; i++) {
        int **next = (int **)realloc(arr, (count + 1) * sizeof(int *));
        if (next == NULL) {
            printf("OOM\n");
            return;
        }
        arr = next;
        arr[count] = NULL;
        count++;
    }
    clock_t end = clock();
    printf("Linear time: %f\n", (double)(end - start) / CLOCKS_PER_SEC);
    free(arr);
}

void test_push_param_geometric() {
    int i;
    int **arr = NULL;
    int count = 0;
    int cap = 0;
    clock_t start = clock();
    for (i = 0; i < 10000; i++) {
        if (count == cap) {
            int ncap = cap == 0 ? 8 : cap * 2;
            int **next = (int **)realloc(arr, ncap * sizeof(int *));
            if (next == NULL) {
                printf("OOM\n");
                return;
            }
            arr = next;
            cap = ncap;
        }
        arr[count] = NULL;
        count++;
    }
    clock_t end = clock();
    printf("Geometric time: %f\n", (double)(end - start) / CLOCKS_PER_SEC);
    free(arr);
}

int main() {
    test_push_param();
    test_push_param_geometric();
    return 0;
}
