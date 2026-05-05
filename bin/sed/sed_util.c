/*
 * sed_util.c - dynamic buffer and error helpers.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sed.h"

/* ------------------------------------------------------------------ */
/* Error helpers                                                        */
/* ------------------------------------------------------------------ */

void
die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "sed: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}

void
warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "sed: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}

/* ------------------------------------------------------------------ */
/* Dynamic buffer                                                        */
/* ------------------------------------------------------------------ */

void
db_init(dynbuf_t *db)
{
    db->buf = NULL;
    db->len = 0;
    db->cap = 0;
}

void
db_free(dynbuf_t *db)
{
    free(db->buf);
    db->buf = NULL;
    db->len = 0;
    db->cap = 0;
}

int
db_reserve(dynbuf_t *db, size_t extra)
{
    size_t need = db->len + extra + 1; /* +1 for NUL */
    if (need <= db->cap)
        return 0;
    size_t newcap = db->cap ? db->cap * 2 : 256;
    while (newcap < need)
        newcap *= 2;
    char *nb = realloc(db->buf, newcap);
    if (!nb)
        return -1;
    db->buf = nb;
    db->cap = newcap;
    return 0;
}

int
db_append(dynbuf_t *db, const char *s, size_t n)
{
    if (!n)
        return 0;
    if (db_reserve(db, n) < 0)
        return -1;
    memcpy(db->buf + db->len, s, n);
    db->len += n;
    db->buf[db->len] = '\0';
    return 0;
}

int
db_appendc(dynbuf_t *db, char c)
{
    return db_append(db, &c, 1);
}

void
db_clear(dynbuf_t *db)
{
    db->len = 0;
    if (db->buf)
        db->buf[0] = '\0';
}

void
db_set(dynbuf_t *db, const char *s, size_t n)
{
    db_clear(db);
    db_append(db, s, n);
}

int
db_ensure_nul(dynbuf_t *db)
{
    if (db_reserve(db, 0) < 0)
        return -1;
    if (db->buf)
        db->buf[db->len] = '\0';
    return 0;
}
