/*
 * cc-std: c2x
 * The builtins named after a C library function that gcc calls the
 * function for: lowering knew them all, but the frontend only some, and
 * took __builtin_strncpy (the Substrate kernel's FAT driver) for an
 * unsupported builtin.
 */
#include <stdio.h>

static char buf[32];

int main(void) {
    char *p = __builtin_strncpy(buf, "hello world", 5);

    printf("%s %d\n", buf, p == buf);
    __builtin_strcat(buf, ", there");
    printf("%s %d\n", buf, (int)__builtin_strnlen(buf, 6));
    printf("%s|%s\n", __builtin_strchr(buf, 'l'), __builtin_strrchr(buf, 'e'));
    printf("%d %d\n", __builtin_strncmp(buf, "help", 3), __builtin_strncmp(buf, "help", 4) < 0);
    printf("%s\n", (char *)__builtin_memchr(buf, 't', sizeof(buf)));
    printf("%s\n", __builtin_strcpy(buf, "done"));
    return 0;
}
