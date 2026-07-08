/*
 * mount(8) — invoke the mount(2) syscall.
 *
 * Usage:
 *     mount [-r] [-o OPTIONS] -t TYPE SOURCE TARGET
 *     mount SOURCE TARGET TYPE         # legacy positional form
 *
 * The -o option string is a comma-separated list of mount options
 * (ro, rw, nosuid, nodev, noexec, sync, async, ...).  It's passed
 * through to mount(2)'s data argument unmodified — the kernel parses
 * the generic options and forwards the rest to the filesystem.
 */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>

static const char *progname = "mount";

static void usage(void) {
    fprintf(stderr,
        "usage: %s [-r] [-o options] -t type source target\n"
        "       %s source target type\n",
        progname, progname);
    exit(1);
}

/*
 * Parse generic option-string flags into the MNT_* bitmap, leaving the
 * full string available for the filesystem to read its own keys.
 */
static unsigned long parse_options(const char *opts) {
    unsigned long flags = 0;
    if (!opts || !*opts) return 0;

    char buf[256];
    strlcpy(buf, opts, sizeof(buf));

    char *save = NULL;
    for (char *tok = strtok_r(buf, ",", &save); tok;
         tok = strtok_r(NULL, ",", &save)) {
        if      (strcmp(tok, "ro")      == 0) flags |= MNT_RDONLY;
        else if (strcmp(tok, "rw")      == 0) flags &= ~MNT_RDONLY;
        else if (strcmp(tok, "remount") == 0) flags |= MNT_UPDATE;
        else if (strcmp(tok, "nosuid")  == 0) flags |= MNT_NOSUID;
        else if (strcmp(tok, "nodev")   == 0) flags |= MNT_NODEV;
        else if (strcmp(tok, "noexec")  == 0) flags |= MNT_NOEXEC;
        else if (strcmp(tok, "sync")    == 0) flags |= MNT_SYNCHRONOUS;
        else if (strcmp(tok, "async")   == 0) flags |= MNT_ASYNC;
        else if (strcmp(tok, "defaults")== 0) { /* rw,suid,dev,exec: no-op */ }
        /* unknown options pass through to the fs in `data`. */
    }
    return flags;
}

int main(int argc, char *argv[]) {
    if (argv[0]) progname = argv[0];

    const char *type = NULL;
    const char *opts = NULL;
    int readonly = 0;

    int opt;
    while ((opt = getopt(argc, argv, "rt:o:")) != -1) {
        switch (opt) {
        case 'r': readonly = 1; break;
        case 't': type = optarg; break;
        case 'o': opts = optarg; break;
        default:  usage();
        }
    }

    unsigned long flags = parse_options(opts);
    if (readonly) flags |= MNT_RDONLY;

    const char *source = NULL, *target = NULL;
    int nargs = argc - optind;
    if (flags & MNT_UPDATE) {
        /* Remount in place: mount -o remount[,rw|ro] <mountpoint>.
         * device/type are irrelevant (the mount already exists) but the
         * mount(2) path wants a non-NULL fstype, so supply a placeholder. */
        if (nargs < 1) usage();
        target = argv[optind];
        if (nargs >= 2) source = argv[optind + 1];
        if (!type) type = "none";
    } else if (type && nargs == 2) {
        source = argv[optind];
        target = argv[optind + 1];
    } else if (!type && nargs == 3) {
        /* Legacy positional form: source target type */
        source = argv[optind];
        target = argv[optind + 1];
        type   = argv[optind + 2];
    } else {
        usage();
        return 1; /* unreachable */
    }

    if (mount(source, target, type, flags, (void *)opts) < 0) {
        fprintf(stderr, "%s: %s on %s (%s): %s\n",
                progname, source, target, type, strerror(errno));
        return 1;
    }
    return 0;
}
