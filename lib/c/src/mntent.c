/*
 * mntent.c — filesystem-table access, reentrant getmntent_r(3) at the core.
 *
 * Parses whitespace-separated fstab/mtab lines
 *   fsname  dir  type  opts  [freq]  [passno]
 * into a caller-supplied struct mntent whose string fields point into the
 * caller's scratch buffer.  Blank lines and '#' comments are skipped.
 */

#include <stdlib.h>
#include <string.h>

#include <mntent.h>

FILE *
setmntent(const char *filename, const char *type)
{
    return fopen(filename, type);
}

int
endmntent(FILE *stream)
{
    if (stream != NULL)
        fclose(stream);
    return 1;       /* glibc always returns 1 */
}

struct mntent *
getmntent_r(FILE *stream, struct mntent *mntbuf, char *buf, int buflen)
{
    char *line, *save, *tok;

    if (stream == NULL || mntbuf == NULL || buf == NULL || buflen <= 0)
        return NULL;

    while (fgets(buf, buflen, stream) != NULL) {
        /* Skip blank and comment lines. */
        line = buf;
        while (*line == ' ' || *line == '\t')
            line++;
        if (*line == '\0' || *line == '\n' || *line == '#')
            continue;

        mntbuf->mnt_fsname = strtok_r(buf, " \t\n", &save);
        if (mntbuf->mnt_fsname == NULL)
            continue;
        mntbuf->mnt_dir  = strtok_r(NULL, " \t\n", &save);
        mntbuf->mnt_type = strtok_r(NULL, " \t\n", &save);
        mntbuf->mnt_opts = strtok_r(NULL, " \t\n", &save);
        if (mntbuf->mnt_dir == NULL || mntbuf->mnt_type == NULL)
            continue;
        if (mntbuf->mnt_opts == NULL)
            mntbuf->mnt_opts = (char *)"";

        tok = strtok_r(NULL, " \t\n", &save);
        mntbuf->mnt_freq = tok ? atoi(tok) : 0;
        tok = strtok_r(NULL, " \t\n", &save);
        mntbuf->mnt_passno = tok ? atoi(tok) : 0;
        return mntbuf;
    }
    return NULL;
}

struct mntent *
getmntent(FILE *stream)
{
    static struct mntent mnt_static;
    static char          mnt_buf[1024];
    return getmntent_r(stream, &mnt_static, mnt_buf, (int)sizeof mnt_buf);
}

char *
hasmntopt(const struct mntent *mnt, const char *opt)
{
    const char *p, *q;
    size_t      optlen;

    if (mnt == NULL || mnt->mnt_opts == NULL || opt == NULL)
        return NULL;
    optlen = strlen(opt);
    p = mnt->mnt_opts;
    while (*p) {
        /* Match opt at the start of a comma-delimited element. */
        for (q = opt; *q && *p == *q; q++, p++)
            ;
        if (q == opt + optlen && (*p == '\0' || *p == ',' || *p == '='))
            return (char *)(p - optlen);
        /* Advance to the next option after the comma. */
        while (*p && *p != ',')
            p++;
        while (*p == ',')
            p++;
    }
    return NULL;
}
