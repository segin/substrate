/*
 * lib/c/src/pwd.c — /etc/passwd parser.
 *
 * Replaces the previous mock implementation that only ever returned
 * "root".  Parses real /etc/passwd lines of the standard form:
 *
 *   name:passwd:uid:gid:gecos:dir:shell
 *
 * The non-reentrant getpwuid/getpwnam/getpwent share a single
 * struct passwd + line buffer; consecutive calls invalidate
 * previous return values, per POSIX.  Reentrant _r variants take a
 * caller-supplied buffer.
 */

#include <pwd.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define PW_LINE_MAX 1024

static FILE         *pw_stream;
static struct passwd pw_static;
static char          pw_static_line[PW_LINE_MAX];

/* Internal: chop a colon-delimited buf into 7 fields and fill `pwd`.
 * Buf must be writable (NUL bytes get written in place).  Returns 0
 * on success, -1 if the line is malformed. */
static int
pw_parse_line(char *buf, struct passwd *pwd)
{
    char *fields[7];
    char *p = buf;
    int   i;

    for (i = 0; i < 6; i++) {
        char *colon = strchr(p, ':');
        if (colon == NULL) {
            return -1;
        }
        *colon = '\0';
        fields[i] = p;
        p = colon + 1;
    }
    fields[6] = p;

    pwd->pw_name   = fields[0];
    pwd->pw_passwd = fields[1];
    pwd->pw_uid    = (uid_t)strtoul(fields[2], NULL, 10);
    pwd->pw_gid    = (gid_t)strtoul(fields[3], NULL, 10);
    pwd->pw_gecos  = fields[4];
    pwd->pw_dir    = fields[5];
    pwd->pw_shell  = fields[6];
    return 0;
}

/* Strip trailing newline / CR. */
static void
pw_strip_eol(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[--n] = '\0';
    }
}

void
setpwent(void)
{
    if (pw_stream != NULL) {
        rewind(pw_stream);
        return;
    }
    pw_stream = fopen("/etc/passwd", "r");
}

void
endpwent(void)
{
    if (pw_stream != NULL) {
        fclose(pw_stream);
        pw_stream = NULL;
    }
}

struct passwd *
getpwent(void)
{
    if (pw_stream == NULL) {
        setpwent();
        if (pw_stream == NULL) {
            return NULL;
        }
    }

    while (fgets(pw_static_line, sizeof(pw_static_line), pw_stream) != NULL) {
        pw_strip_eol(pw_static_line);
        /* Skip blank lines and comment lines. */
        if (pw_static_line[0] == '\0' || pw_static_line[0] == '#') {
            continue;
        }
        if (pw_parse_line(pw_static_line, &pw_static) == 0) {
            return &pw_static;
        }
        /* Malformed line — try the next one rather than aborting the
         * whole scan; the database may have a stray bad entry but the
         * one the caller wants could be further down. */
    }
    return NULL;
}

struct passwd *
getpwuid(uid_t uid)
{
    struct passwd *p;
    setpwent();
    while ((p = getpwent()) != NULL) {
        if (p->pw_uid == uid) {
            endpwent();
            return p;
        }
    }
    endpwent();
    return NULL;
}

struct passwd *
getpwnam(const char *name)
{
    struct passwd *p;
    if (name == NULL) {
        return NULL;
    }
    setpwent();
    while ((p = getpwent()) != NULL) {
        if (strcmp(p->pw_name, name) == 0) {
            endpwent();
            return p;
        }
    }
    endpwent();
    return NULL;
}

/* Reentrant variant scaffolding: parse the current line into the
 * caller-supplied buf + struct, no shared state. */
static int
pw_lookup_r(FILE *f, struct passwd *pwd, char *buf, size_t buflen,
            int (*match)(const struct passwd *, const void *),
            const void *key, struct passwd **result)
{
    char        *line = buf;
    struct passwd local;

    *result = NULL;

    while (fgets(line, (int)buflen, f) != NULL) {
        size_t len = strlen(line);
        if (len + 1 == buflen && line[len - 1] != '\n') {
            /* Line truncated.  Drain the remainder and report ERANGE. */
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') {
                /* discard */
            }
            return ERANGE;
        }
        pw_strip_eol(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }
        if (pw_parse_line(line, &local) != 0) {
            continue;
        }
        if (match(&local, key)) {
            *pwd = local;
            *result = pwd;
            return 0;
        }
    }
    return 0;  /* not found, *result already NULL */
}

static int
pw_match_uid(const struct passwd *p, const void *key)
{
    return p->pw_uid == *(const uid_t *)key;
}

static int
pw_match_name(const struct passwd *p, const void *key)
{
    return strcmp(p->pw_name, (const char *)key) == 0;
}

int
getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen,
           struct passwd **result)
{
    FILE *f;
    int   rc;
    if (pwd == NULL || buf == NULL || result == NULL || buflen < 64) {
        if (result != NULL) {
            *result = NULL;
        }
        return ERANGE;
    }
    f = fopen("/etc/passwd", "r");
    if (f == NULL) {
        *result = NULL;
        return errno;
    }
    rc = pw_lookup_r(f, pwd, buf, buflen, pw_match_uid, &uid, result);
    fclose(f);
    return rc;
}

int
getpwnam_r(const char *name, struct passwd *pwd, char *buf, size_t buflen,
           struct passwd **result)
{
    FILE *f;
    int   rc;
    if (name == NULL || pwd == NULL || buf == NULL || result == NULL ||
        buflen < 64) {
        if (result != NULL) {
            *result = NULL;
        }
        return ERANGE;
    }
    f = fopen("/etc/passwd", "r");
    if (f == NULL) {
        *result = NULL;
        return errno;
    }
    rc = pw_lookup_r(f, pwd, buf, buflen, pw_match_name, name, result);
    fclose(f);
    return rc;
}

/* Read the next entry from an arbitrary already-open passwd stream into the
 * caller's storage.  The next valid line is taken (no key filter). */
int
fgetpwent_r(FILE *f, struct passwd *pwd, char *buf, size_t buflen,
            struct passwd **result)
{
    char        *line = buf;
    struct passwd local;

    if (f == NULL || pwd == NULL || buf == NULL || result == NULL) {
        if (result != NULL)
            *result = NULL;
        return ERANGE;
    }
    *result = NULL;

    while (fgets(line, (int)buflen, f) != NULL) {
        size_t len = strlen(line);
        if (len + 1 == buflen && line[len - 1] != '\n') {
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n')
                ;
            return ERANGE;
        }
        pw_strip_eol(line);
        if (line[0] == '\0' || line[0] == '#')
            continue;
        if (pw_parse_line(line, &local) != 0)
            continue;
        *pwd = local;
        *result = pwd;
        return 0;
    }
    return ENOENT;      /* end of file */
}

/* Enumerate /etc/passwd via the shared internal stream used by getpwent(),
 * but parse into the caller's storage. */
int
getpwent_r(struct passwd *pwd, char *buf, size_t buflen, struct passwd **result)
{
    if (pwd == NULL || buf == NULL || result == NULL) {
        if (result != NULL)
            *result = NULL;
        return ERANGE;
    }
    if (pw_stream == NULL) {
        setpwent();
        if (pw_stream == NULL) {
            *result = NULL;
            return errno;
        }
    }
    return fgetpwent_r(pw_stream, pwd, buf, buflen, result);
}
