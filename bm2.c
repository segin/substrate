#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

void benchmark_linear() {
    int iterations = 10000;
    int *arr = NULL;
    int count = 0;
    clock_t start = clock();
    for (int i = 0; i < iterations; i++) {
        int *next = (int *)realloc(arr, (count + 1) * sizeof(*next));
        if (next == NULL) {
            printf("OOM\n");
            return;
        }
        arr = next;
        arr[count] = i;
        count++;
    }
    clock_t end = clock();
    printf("Linear time: %f\n", (double)(end - start) / CLOCKS_PER_SEC);
    free(arr);
}

void benchmark_geometric() {
    int iterations = 10000;
    int *arr = NULL;
    int count = 0;
    int cap = 0;
    clock_t start = clock();
    for (int i = 0; i < iterations; i++) {
        if (count == cap) {
            size_t ncap = cap == 0 ? 8 : cap * 2;
            int *next = (int *)realloc(arr, ncap * sizeof(*next));
            if (next == NULL) {
                printf("OOM\n");
                return;
            }
            arr = next;
            cap = ncap;
        }
        arr[count] = i;
        count++;
    }
    clock_t end = clock();
    printf("Geometric time: %f\n", (double)(end - start) / CLOCKS_PER_SEC);
    free(arr);
}

int main() {
    benchmark_linear();
    benchmark_geometric();
    return 0;
}
