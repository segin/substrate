/*
 * od - octal (byte) dump.
 *
 *   od [file...]
 *
 * Dumps each input file (or stdin, or "-") as a per-byte octal listing
 * with a running octal offset.  This is a deliberately small subset of
 * od(1): only the default byte-octal format is produced.
 *
 * The previous version silently treated any first argument as a filename
 * — including option-looking arguments such as "-t x1", which it then
 * failed to fopen and exited 1 on with no message.  It also ignored every
 * file past the first.  Now unsupported options are reported honestly on
 * stderr (exit 1), "-" means stdin, all operands are dumped, and a failed
 * open is diagnosed rather than silently swallowed.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *prog = "od";

static int
dump(FILE *fp, unsigned long *offset)
{
    unsigned char buf[16];
    size_t n;

    while ((n = fread(buf, 1, sizeof buf, fp)) > 0) {
        printf("%07lo", *offset);
        for (size_t i = 0; i < n; i++)
            printf(" %03o", buf[i]);
        putchar('\n');
        *offset += n;
    }
    if (ferror(fp)) {
        fprintf(stderr, "%s: read error: %s\n", prog, strerror(errno));
        return -1;
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    int   i;
    int   any_file = 0;
    int   rc = 0;
    unsigned long offset = 0;

    /* Reject options we don't implement rather than misinterpreting them
     * as filenames. */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        if (argv[i][0] == '-' && argv[i][1] != '\0') {
            fprintf(stderr,
                "%s: option '%s' not supported (only the default octal-byte "
                "dump is implemented)\n", prog, argv[i]);
            return 1;
        }
        break;   /* first non-option: fall through to operand handling */
    }

    /* Dump every operand (from the first non-option onward). */
    for (; i < argc; i++) {
        any_file = 1;
        FILE *fp;
        if (strcmp(argv[i], "-") == 0) {
            fp = stdin;
        } else {
            fp = fopen(argv[i], "rb");
            if (!fp) {
                fprintf(stderr, "%s: %s: %s\n", prog, argv[i], strerror(errno));
                rc = 1;
                continue;
            }
        }
        if (dump(fp, &offset) != 0)
            rc = 1;
        if (fp != stdin)
            fclose(fp);
    }

    if (!any_file) {
        if (dump(stdin, &offset) != 0)
            rc = 1;
    }

    printf("%07lo\n", offset);   /* trailing offset line, like od(1) */
    return rc;
}
