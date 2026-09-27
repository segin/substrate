/*
 * hostname — print or set the system hostname.
 *
 *   hostname               print current hostname
 *   hostname -f            print FQDN
 *   hostname -s            print short form (strip first dot onwards)
 *   hostname -F file       load hostname from a file (typically /etc/hostname)
 *   hostname <name>        set hostname
 *
 * Printing never sets the name: /etc/hostname is applied by -F, which
 * rc.d/00-hostname runs at boot.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int set_from_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "hostname: %s: %s\n", path, strerror(errno));
        return 1;
    }
    char buf[256];
    if (!fgets(buf, sizeof(buf), f)) {
        fclose(f);
        fprintf(stderr, "hostname: %s: empty\n", path);
        return 1;
    }
    fclose(f);
    size_t n = strlen(buf);
    while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r' ||
                     buf[n-1] == ' '  || buf[n-1] == '\t')) {
        buf[--n] = '\0';
    }
    if (n == 0) {
        fprintf(stderr, "hostname: %s: blank\n", path);
        return 1;
    }
    if (sethostname(buf, n) < 0) {
        perror("hostname: sethostname");
        return 1;
    }
    return 0;
}

static int print_current(int fqdn)
{
    char buf[256];
    if (gethostname(buf, sizeof(buf)) < 0) {
        perror("hostname: gethostname");
        return 1;
    }
    if (!fqdn) {
        char *dot = strchr(buf, '.');
        if (dot) *dot = '\0';
    }
    printf("%s\n", buf);
    return 0;
}

int main(int argc, char *argv[])
{
    int fqdn = 0;
    int i = 1;
    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "-f") == 0)      { fqdn = 1; i++; }
        else if (strcmp(argv[i], "-s") == 0) { fqdn = 0; i++; }
        else if (strcmp(argv[i], "-F") == 0 && i + 1 < argc) {
            return set_from_file(argv[i + 1]);
        }
        else if (strcmp(argv[i], "--") == 0) { i++; break; }
        else {
            fprintf(stderr, "usage: hostname [-fs] [-F file] [name]\n");
            return 1;
        }
    }
    if (i >= argc) {
        return print_current(fqdn);
    }
    if (sethostname(argv[i], strlen(argv[i])) < 0) {
        perror("hostname: sethostname");
        return 1;
    }
    return 0;
}
