#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    FILE *fp = stdin;
    if (argc > 1) {
        fp = fopen(argv[1], "rb");
        if (!fp) return 1;
    }

    unsigned char buf[16];
    size_t n;
    long offset = 0;
    while ((n = fread(buf, 1, 16, fp)) > 0) {
        printf("%07lo ", offset);
        for (size_t i = 0; i < n; i++) {
            printf("%03o ", buf[i]);
        }
        printf("\n");
        offset += n;
    }

    if (fp != stdin) fclose(fp);
    return 0;
}
