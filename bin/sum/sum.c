#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
    if (argc < 2) return 1;
    
    for (int i = 1; i < argc; i++) {
        FILE *fp = fopen(argv[i], "rb");
        if (!fp) { perror(argv[i]); continue; }
        
        unsigned int sum = 0;
        int c;
        long blocks = 0;
        while ((c = fgetc(fp)) != EOF) {
            sum += c;
            blocks++;
        }
        printf("%05u %5ld %s\n", sum % 65536, (blocks + 1023) / 1024, argv[i]);
        fclose(fp);
    }
    return 0;
}

