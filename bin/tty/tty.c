#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
tty_usage(const char *progname)
{
    fprintf(stderr, "usage: %s [-s]\n", progname);
}

int
main(int argc, char **argv)
{
    int silent = 0;
    const char *name;

    if (argc == 2 && strcmp(argv[1], "-s") == 0) {
        silent = 1;
    } else if (argc != 1) {
        tty_usage(argv[0]);
        return 2;
    }

    if (!isatty(STDIN_FILENO)) {
        if (!silent) {
            puts("not a tty");
        }
        return 1;
    }

    name = ttyname(STDIN_FILENO);
    if (name == NULL) {
        if (!silent) {
            fprintf(stderr, "%s: %s\n", argv[0], strerror(errno));
        }
        return 2;
    }

    if (!silent) {
        puts(name);
    }

    return 0;
}
