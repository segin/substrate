#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/utsname.h>
#include <stdlib.h>
#include <string.h>

#define FLAG_SYSNAME       0x01
#define FLAG_NODENAME      0x02
#define FLAG_RELEASE       0x04
#define FLAG_VERSION       0x08
#define FLAG_MACHINE       0x10
#define FLAG_PROCESSOR     0x20
#define FLAG_OS            0x40

static const struct option longopts[] = {
    {"all", no_argument, NULL, 'a'},
    {"machine", no_argument, NULL, 'm'},
    {"nodename", no_argument, NULL, 'n'},
    {"release", no_argument, NULL, 'r'},
    {"kernel-release", no_argument, NULL, 'r'},
    {"sysname", no_argument, NULL, 's'},
    {"kernel-name", no_argument, NULL, 's'},
    {"version", no_argument, NULL, 'v'},
    {"kernel-version", no_argument, NULL, 'v'},
    {"processor", no_argument, NULL, 'p'},
    {"operating-system", no_argument, NULL, 'o'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}
};

static void usage(void) {
    fprintf(stderr, "Usage: uname [-amnprsvo] [--all] [--machine] [--nodename] [--processor] [--operating-system] [--kernel-name] [--kernel-release] [--kernel-version]\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    int ch;
    unsigned int flags = 0;

    while ((ch = getopt_long(argc, argv, "amnprsvo", longopts, NULL)) != -1) {
        switch (ch) {
            case 'a':
                flags |= (FLAG_SYSNAME | FLAG_NODENAME | FLAG_RELEASE | FLAG_VERSION | FLAG_MACHINE | FLAG_PROCESSOR);
                break;
            case 'm':
                flags |= FLAG_MACHINE;
                break;
            case 'n':
                flags |= FLAG_NODENAME;
                break;
            case 'p':
                flags |= FLAG_PROCESSOR;
                break;
            case 'r':
                flags |= FLAG_RELEASE;
                break;
            case 's':
                flags |= FLAG_SYSNAME;
                break;
            case 'v':
                flags |= FLAG_VERSION;
                break;
            case 'o':
                flags |= FLAG_OS;
                break;
            case 'h':
            case '?':
            default:
                usage();
        }
    }

    if (optind < argc) {
        fprintf(stderr, "uname: extra operand '%s'\n", argv[optind]);
        usage();
    }

    if (flags == 0) {
        flags = FLAG_SYSNAME;
    }

    struct utsname name;
    if (uname(&name) == -1) {
        perror("uname");
        exit(1);
    }

    int space = 0;

    if (flags & FLAG_SYSNAME) {
        printf("%s%s", space ? " " : "", name.sysname);
        space = 1;
    }
    if (flags & FLAG_NODENAME) {
        printf("%s%s", space ? " " : "", name.nodename);
        space = 1;
    }
    if (flags & FLAG_RELEASE) {
        printf("%s%s", space ? " " : "", name.release);
        space = 1;
    }
    if (flags & FLAG_VERSION) {
        printf("%s%s", space ? " " : "", name.version);
        space = 1;
    }
    if (flags & FLAG_MACHINE) {
        printf("%s%s", space ? " " : "", name.machine);
        space = 1;
    }
    if (flags & FLAG_PROCESSOR) {
        // v1 maps processor directly to machine
        printf("%s%s", space ? " " : "", name.machine);
        space = 1;
    }
    if (flags & FLAG_OS) {
        // v1 maps operating-system directly to sysname
        printf("%s%s", space ? " " : "", name.sysname);
        space = 1;
    }

    printf("\n");
    return 0;
}
