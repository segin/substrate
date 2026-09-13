/*
 * ctermid(3) -- generate a pathname for the controlling terminal.
 *
 * POSIX only asks for a name that refers to the controlling terminal of the
 * calling process when opened, and "/dev/tty" is that name on substrate:
 * devfs resolves it to the opener's controlling terminal.  No lookup is
 * needed, so the result does not depend on whether the process has one --
 * opening the name then fails, which is how callers are meant to find out.
 */
#include <stdio.h>
#include <string.h>

static char ctermid_buf[L_ctermid];

char *ctermid(char *s)
{
    char *buf = (s != NULL) ? s : ctermid_buf;

    memcpy(buf, "/dev/tty", sizeof("/dev/tty"));
    return buf;
}
