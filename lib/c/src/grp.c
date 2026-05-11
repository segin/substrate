/*
 * lib/c/src/grp.c — /etc/group parser.
 *
 * Format:   name:passwd:gid:user1,user2,user3
 *
 * Like pwd.c, the non-reentrant accessors share a single static
 * struct + line buffer + member-array buffer.  Consecutive calls
 * invalidate previous return values.
 */

#include <grp.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#define GR_LINE_MAX 1024
#define GR_MEM_MAX  64

static FILE        *gr_stream;
static struct group gr_static;
static char         gr_static_line[GR_LINE_MAX];
static char        *gr_static_mem[GR_MEM_MAX + 1];

/* Split the comma-separated member list at `mems` into the
 * caller-supplied gr_mem array, NUL-terminating each name in place.
 * Returns the count.  Empty list (or a single empty token) yields 0. */
static int
gr_split_members(char *mems, char **out, size_t out_max)
{
    size_t n = 0;
    char  *p;

    if (mems == NULL || *mems == '\0') {
        out[0] = NULL;
        return 0;
    }

    p = mems;
    while (n + 1 < out_max) {
        out[n++] = p;
        p = strchr(p, ',');
        if (p == NULL) {
            break;
        }
        *p++ = '\0';
    }
    out[n] = NULL;
    return (int)n;
}

/* Parse one /etc/group line into `grp`, splitting member names into
 * the caller-supplied `mems` array.  Returns 0 on success, -1 on
 * malformed input. */
static int
gr_parse_line(char *buf, struct group *grp, char **mems, size_t mems_max)
{
    char *fields[4];
    char *p = buf;
    int   i;

    for (i = 0; i < 3; i++) {
        char *colon = strchr(p, ':');
        if (colon == NULL) {
            return -1;
        }
        *colon = '\0';
        fields[i] = p;
        p = colon + 1;
    }
    fields[3] = p;

    grp->gr_name   = fields[0];
    grp->gr_passwd = fields[1];
    grp->gr_gid    = (gid_t)strtoul(fields[2], NULL, 10);
    (void)gr_split_members(fields[3], mems, mems_max);
    grp->gr_mem    = mems;
    return 0;
}

static void
gr_strip_eol(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

void
setgrent(void)
{
    if (gr_stream != NULL) {
        rewind(gr_stream);
        return;
    }
    gr_stream = fopen("/etc/group", "r");
}

void
endgrent(void)
{
    if (gr_stream != NULL) {
        fclose(gr_stream);
        gr_stream = NULL;
    }
}

struct group *
getgrent(void)
{
    if (gr_stream == NULL) {
        setgrent();
        if (gr_stream == NULL) {
            return NULL;
        }
    }

    while (fgets(gr_static_line, sizeof(gr_static_line), gr_stream) != NULL) {
        gr_strip_eol(gr_static_line);
        if (gr_static_line[0] == '\0' || gr_static_line[0] == '#') {
            continue;
        }
        if (gr_parse_line(gr_static_line, &gr_static, gr_static_mem,
                          GR_MEM_MAX + 1) == 0) {
            return &gr_static;
        }
    }
    return NULL;
}

struct group *
getgrgid(gid_t gid)
{
    struct group *g;
    setgrent();
    while ((g = getgrent()) != NULL) {
        if (g->gr_gid == gid) {
            endgrent();
            return g;
        }
    }
    endgrent();
    return NULL;
}

struct group *
getgrnam(const char *name)
{
    struct group *g;
    if (name == NULL) {
        return NULL;
    }
    setgrent();
    while ((g = getgrent()) != NULL) {
        if (strcmp(g->gr_name, name) == 0) {
            endgrent();
            return g;
        }
    }
    endgrent();
    return NULL;
}

/*
 * Reentrant variants.  The caller-supplied buffer holds the raw
 * line PLUS the gr_mem pointer array; reserve GR_MEM_MAX+1 pointers
 * at the tail of the buffer.
 */
static int
gr_lookup_r(FILE *f, struct group *grp, char *buf, size_t buflen,
            int (*match)(const struct group *, const void *),
            const void *key, struct group **result)
{
    size_t pointer_area = sizeof(char *) * (GR_MEM_MAX + 1);
    char  *line = buf;
    size_t line_max;
    char **mems;
    struct group local;

    *result = NULL;

    if (buflen < pointer_area + 64) {
        return ERANGE;
    }
    line_max = buflen - pointer_area;
    mems = (char **)(buf + line_max);

    while (fgets(line, (int)line_max, f) != NULL) {
        size_t len = strlen(line);
        if (len + 1 == line_max && line[len - 1] != '\n') {
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {
                /* drain */
            }
            return ERANGE;
        }
        gr_strip_eol(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        if (gr_parse_line(line, &local, mems, GR_MEM_MAX + 1) != 0) {
            continue;
        }
        if (match(&local, key)) {
            *grp = local;
            *result = grp;
            return 0;
        }
    }
    return 0;
}

static int
gr_match_gid(const struct group *g, const void *key)
{
    return g->gr_gid == *(const gid_t *)key;
}

static int
gr_match_name(const struct group *g, const void *key)
{
    return strcmp(g->gr_name, (const char *)key) == 0;
}

int
getgrgid_r(gid_t gid, struct group *grp, char *buf, size_t buflen,
           struct group **result)
{
    FILE *f;
    int   rc;
    if (grp == NULL || buf == NULL || result == NULL) {
        if (result != NULL) {
            *result = NULL;
        }
        return EINVAL;
    }
    f = fopen("/etc/group", "r");
    if (f == NULL) {
        *result = NULL;
        return errno;
    }
    rc = gr_lookup_r(f, grp, buf, buflen, gr_match_gid, &gid, result);
    fclose(f);
    return rc;
}

int
getgrnam_r(const char *name, struct group *grp, char *buf, size_t buflen,
           struct group **result)
{
    FILE *f;
    int   rc;
    if (name == NULL || grp == NULL || buf == NULL || result == NULL) {
        if (result != NULL) {
            *result = NULL;
        }
        return EINVAL;
    }
    f = fopen("/etc/group", "r");
    if (f == NULL) {
        *result = NULL;
        return errno;
    }
    rc = gr_lookup_r(f, grp, buf, buflen, gr_match_name, name, result);
    fclose(f);
    return rc;
}

int
getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups)
{
    struct group *g;
    int wanted = 0;
    int max = (ngroups != NULL) ? *ngroups : 0;
    int truncated = 0;

    if (user == NULL || ngroups == NULL) {
        return -1;
    }

    /* Primary group is always first. */
    if (wanted < max) {
        groups[wanted] = group;
    } else {
        truncated = 1;
    }
    wanted++;

    setgrent();
    while ((g = getgrent()) != NULL) {
        int i;

        if (g->gr_gid == group) {
            /* Already added as primary. */
            continue;
        }
        if (g->gr_mem == NULL) {
            continue;
        }
        for (i = 0; g->gr_mem[i] != NULL; i++) {
            if (strcmp(g->gr_mem[i], user) == 0) {
                if (wanted < max) {
                    groups[wanted] = g->gr_gid;
                } else {
                    truncated = 1;
                }
                wanted++;
                break;
            }
        }
    }
    endgrent();

    *ngroups = wanted;
    return truncated ? -1 : wanted;
}

int
initgroups(const char *user, gid_t group)
{
    gid_t groups[32];
    int ngroups = (int)(sizeof(groups) / sizeof(groups[0]));
    (void)getgrouplist(user, group, groups, &ngroups);
    if (ngroups > (int)(sizeof(groups) / sizeof(groups[0]))) {
        ngroups = (int)(sizeof(groups) / sizeof(groups[0]));
    }
    return setgroups(ngroups, groups);
}
