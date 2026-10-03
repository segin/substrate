/*
 * hello.c - a dynamically linked 64-bit program: printf, argv and envp
 * through /sbin/ld64.so and the shared libc.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv, char **envp) {
    int nenv = 0;

    printf("hello from a 64-bit dynamic program (%zu-bit pointers)\n",
           sizeof(void *) * 8);
    printf("argc=%d\n", argc);
    for (int i = 0; i < argc; i++)
        printf("argv[%d]=%s\n", i, argv[i]);
    for (char **e = envp; *e; e++)
        nenv++;
    printf("envp entries=%d\n", nenv);

    /* The linker publishes environ before constructors run, and crt0
     * assigns the same object. */
    const char *v = getenv("LD64_TEST");
    printf("getenv(LD64_TEST)=%s\n", v ? v : "(null)");
    if (argc != 3 || strcmp(argv[1], "one") != 0 ||
        strcmp(argv[2], "two") != 0 || !v || strcmp(v, "yes") != 0) {
        printf("hello: FAIL\n");
        return 1;
    }
    printf("hello: OK\n");
    return 0;
}
