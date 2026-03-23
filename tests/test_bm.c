#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#define main dummy_main
#include "../bm.c"
#undef main

int main() {
    benchmark_linear();
    printf("test_bm passed\n");
    return 0;
}
