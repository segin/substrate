#include <stdio.h>
#include <time.h>
#include <string.h>

int main() {
    char buf[1024];
    clock_t start = clock();
    for (int i = 0; i < 1000000; i++) {
        sprintf(buf, "%g", 123.456000);
        sprintf(buf, "%g", 123000.0);
        sprintf(buf, "%g", 0.000123000);
        sprintf(buf, "%g", 1.234567e-10);
    }
    clock_t end = clock();
    double cpu_time_used = ((double) (end - start)) / CLOCKS_PER_SEC;
    printf("Time: %f\n", cpu_time_used);
    return 0;
}
