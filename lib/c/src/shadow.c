/*
 * shadow.c — reentrant shadow password database access.
 *
 * Parses /etc/shadow entries of the form
 *   name:passwd:lstchg:min:max:warn:inact:expire:flag
 * into a caller-supplied `struct spwd`, with the string fields pointing into a
 * caller-supplied scratch buffer (no shared state).  Empty numeric fields
 * become -1, as glibc reports them.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <shadow.h>

#define SHADOW_PATH "/etc/shadow"

static FILE *sp_stream;     /* enumeration stream for getspent_r() */

/* Parse one writable shadow line (the buffer is modified in place) into sp.
 * Returns 0 on success, -1 on a malformed line. */
static int
sp_parse(char *line, struct spwd *sp)
{
    char *f[9];
    char *p = line;
    int   i;

    for (i = 0; i < 8; i++) {
        char *colon = strchr(p, ':');
        if (colon == NULL)
            return -1;
        *colon = '\0';
        f[i] = p;
        p = colon + 1;
    }
    f[8] = p;

    sp->sp_namp = f[0];
    sp->sp_pwdp = f[1];
    /* An empty numeric field means "unset" -> -1. */
    sp->sp_lstchg = f[2][0] ? strtol(f[2], NULL, 10) : -1;
    sp->sp_min    = f[3][0] ? strtol(f[3], NULL, 10) : -1;
    sp->sp_max    = f[4][0] ? strtol(f[4], NULL, 10) : -1;
    sp->sp_warn   = f[5][0] ? strtol(f[5], NULL, 10) : -1;
    sp->sp_inact  = f[6][0] ? strtol(f[6], NULL, 10) : -1;
    sp->sp_expire = f[7][0] ? strtol(f[7], NULL, 10) : -1;
    sp->sp_flag   = f[8][0] ? strtoul(f[8], NULL, 10) : (unsigned long)-1;
    return 0;
}

static void
sp_strip_eol(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
        s[--n] = '\0';
}

void
setspent(void)
{
    if (sp_stream != NULL)
        rewind(sp_stream);
    else
        sp_stream = fopen(SHADOW_PATH, "r");
}

void
endspent(void)
{
    if (sp_stream != NULL) {
        fclose(sp_stream);
        sp_stream = NULL;
    }
}

int
sgetspent_r(const char *string, struct spwd *result_buf, char *buffer,
            size_t buflen, struct spwd **result)
{
    *result = NULL;
    if (string == NULL || result_buf == NULL || buffer == NULL)
        return EINVAL;
    if (strlen(string) + 1 > buflen)
        return ERANGE;
    strlcpy(buffer, string, buflen);
    sp_strip_eol(buffer);
    if (sp_parse(buffer, result_buf) != 0)
        return EINVAL;
    *result = result_buf;
    return 0;
}

int
fgetspent_r(FILE *stream, struct spwd *result_buf, char *buffer, size_t buflen,
            struct spwd **result)
{
    *result = NULL;
    if (stream == NULL || result_buf == NULL || buffer == NULL)
        return EINVAL;

    while (fgets(buffer, (int)buflen, stream) != NULL) {
        size_t len = strlen(buffer);
        if (len + 1 == buflen && buffer[len - 1] != '\n') {
            int c;
            while ((c = fgetc(stream)) != EOF && c != '\n')
                ;
            return ERANGE;
        }
        sp_strip_eol(buffer);
        if (buffer[0] == '\0' || buffer[0] == '#')
            continue;
        if (sp_parse(buffer, result_buf) != 0)
            continue;
        *result = result_buf;
        return 0;
    }
    return 0;       /* EOF: not found, *result already NULL */
}

int
getspent_r(struct spwd *result_buf, char *buffer, size_t buflen,
           struct spwd **result)
{
    *result = NULL;
    if (sp_stream == NULL) {
        setspent();
        if (sp_stream == NULL)
            return errno;
    }
    return fgetspent_r(sp_stream, result_buf, buffer, buflen, result);
}

int
getspnam_r(const char *name, struct spwd *result_buf, char *buffer,
           size_t buflen, struct spwd **result)
{
    FILE *f;
    int   rc;

    *result = NULL;
    if (name == NULL || result_buf == NULL || buffer == NULL)
        return EINVAL;
    f = fopen(SHADOW_PATH, "r");
    if (f == NULL)
        return errno;
    while ((rc = fgetspent_r(f, result_buf, buffer, buflen, result)) == 0 &&
           *result != NULL) {
        if (strcmp(result_buf->sp_namp, name) == 0)
            break;
        *result = NULL;
    }
    fclose(f);
    return (rc == ERANGE) ? ERANGE : 0;
}

/* Non-reentrant variants: parse into one shared static struct + line buffer,
 * the classic <shadow.h> interface.  Not thread-safe. */
static struct spwd sp_static;
static char        sp_static_buf[1024];

struct spwd *
getspnam(const char *name)
{
    struct spwd *result = NULL;
    if (getspnam_r(name, &sp_static, sp_static_buf, sizeof sp_static_buf,
                   &result) != 0)
        return NULL;
    return result;
}

struct spwd *
getspent(void)
{
    struct spwd *result = NULL;
    if (getspent_r(&sp_static, sp_static_buf, sizeof sp_static_buf,
                   &result) != 0)
        return NULL;
    return result;
}

struct spwd *
fgetspent(FILE *stream)
{
    struct spwd *result = NULL;
    if (fgetspent_r(stream, &sp_static, sp_static_buf, sizeof sp_static_buf,
                    &result) != 0)
        return NULL;
    return result;
}

struct spwd *
sgetspent(const char *string)
{
    struct spwd *result = NULL;
    if (sgetspent_r(string, &sp_static, sp_static_buf, sizeof sp_static_buf,
                    &result) != 0)
        return NULL;
    return result;
}
