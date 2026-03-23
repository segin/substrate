#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

// Redefine main to avoid conflict with the original file
#define main bm2_main
#include "../../bm2.c"
#undef main

int main() {
    printf("Running benchmark_linear test...\n");
    benchmark_linear();
    printf("benchmark_linear passed\n");

    printf("Running benchmark_geometric test...\n");
    benchmark_geometric();
    printf("benchmark_geometric passed\n");

    return 0;
}
