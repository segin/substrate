/*
 * bin/passwd/passwd.c — change a user's login password.
 *
 * Usage:  passwd [user]
 *
 * Without an argument, changes the invoking user's password.  Only
 * root may change other users' passwords.  Non-root users must
 * prove they know the current password first.
 *
 * New passwords are stored as `$5$<salt>$<hash>` (SHA-256-crypt,
 * 5000 default rounds) via crypt(3).  Old `$1$` / DES hashes in
 * /etc/shadow continue to verify but get upgraded to `$5$` on the
 * next successful password change.
 */

#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "crypt.h"
#include <sys/pwdb.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Overwrite a buffer so the compiler cannot elide the clear (no
 * explicit_bzero on this target). */
static void
secure_zero(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--)
        *v++ = 0;
}

#define PW_MAX 128

/* Terminal state to restore if a signal interrupts the echo-off prompt,
 * so ^C never leaves the tty un-echoing and leaking the next line
 * (PASSWD-05). */
static struct termios         g_saved_tio;
static volatile sig_atomic_t  g_tio_saved;

static void
restore_tio_and_die(int sig)
{
    if (g_tio_saved)
        tcsetattr(0, TCSANOW, &g_saved_tio);
    _exit(128 + sig);
}

/* Read a line with echo disabled.  Returns the length on success, or -1
 * on read error or EOF-before-any-input (PASSWD-07) so the caller can
 * tell an I/O failure from a genuinely empty entry. */
static int
read_password(const char *prompt, char *buf, size_t bufsz)
{
    struct termios old_tio;
    struct termios new_tio;
    int            tty_ok;
    size_t         i = 0;
    char           c;
    ssize_t        n;
    int            got_error = 0;

    fputs(prompt, stdout);
    fflush(stdout);

    tty_ok = (tcgetattr(0, &old_tio) == 0);
    if (tty_ok) {
        struct sigaction sa = { 0 };
        new_tio = old_tio;
        new_tio.c_lflag &= ~(ECHO | ECHONL);
        new_tio.c_lflag |= ICANON;
        g_saved_tio = old_tio;
        g_tio_saved = 1;
        sa.sa_handler = restore_tio_and_die;
        sigaction(SIGINT, &sa, NULL);
        sigaction(SIGQUIT, &sa, NULL);
        sigaction(SIGTERM, &sa, NULL);
        tcsetattr(0, TCSANOW, &new_tio);
    }
    while (i + 1 < bufsz) {
        n = read(0, &c, 1);
        if (n < 0) { got_error = 1; break; }
        if (n == 0 || c == '\n' || c == '\r') {
            if (n == 0 && i == 0)
                got_error = 1;
            break;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    if (tty_ok) {
        tcsetattr(0, TCSANOW, &old_tio);
        g_tio_saved = 0;
    }
    fputc('\n', stdout);
    fflush(stdout);
    return got_error ? -1 : (int)i;
}

/* Read /etc/shadow into a buffer; on return the caller owns the
 * buffer (free with free()) and *out_len holds the byte count. */
static char *
read_shadow_file(size_t *out_len)
{
    FILE *f = fopen("/etc/shadow", "r");
    char *buf = NULL;
    size_t cap = 0, len = 0;
    int    c;
    if (f == NULL) {
        return NULL;
    }
    while ((c = fgetc(f)) != EOF) {
        if (len + 1 >= cap) {
            size_t ncap = cap ? cap * 2 : 1024;
            char  *nbuf = realloc(buf, ncap);
            if (nbuf == NULL) {
                free(buf);
                fclose(f);
                return NULL;
            }
            buf = nbuf;
            cap = ncap;
        }
        buf[len++] = (char)c;
    }
    fclose(f);
    if (buf != NULL) {
        buf[len] = '\0';
    }
    *out_len = len;
    return buf;
}

struct rewrite_ctx {
    const char *in;
    const char *user;
    const char *newpw;
    int         found;
};

/* Emit `n` bytes, propagating a short/failed write as an error. */
static int
emit(FILE *out, const char *data, size_t n)
{
    return (n == 0 || fwrite(data, 1, n, out) == n) ? 0 : -1;
}

/* pwdb_atomic_rewrite callback: copy the shadow contents, replacing the
 * password field of `user`'s row. Every write is checked; returns -1 (which
 * makes pwdb_atomic_rewrite unlink the temp and NOT rename) if the user is
 * not found or any write fails. */
static int
rewrite_shadow_cb(FILE *out, void *arg)
{
    struct rewrite_ctx *ctx = arg;
    size_t      ulen = strlen(ctx->user);
    const char *p = ctx->in;

    while (*p != '\0') {
        const char *line_start = p;
        while (*p != '\0' && *p != '\n') {
            p++;
        }
        if (!ctx->found &&
            (size_t)(p - line_start) > ulen &&
            strncmp(line_start, ctx->user, ulen) == 0 &&
            line_start[ulen] == ':') {
            const char *colon2 = strchr(line_start + ulen + 1, ':');
            if (colon2 == NULL || colon2 > p) {
                if (emit(out, line_start, (size_t)(p - line_start)) != 0)
                    return -1;
            } else {
                if (emit(out, line_start, ulen + 1) != 0 ||
                    emit(out, ctx->newpw, strlen(ctx->newpw)) != 0 ||
                    emit(out, colon2, (size_t)(p - colon2)) != 0)
                    return -1;
            }
            ctx->found = 1;
        } else {
            if (emit(out, line_start, (size_t)(p - line_start)) != 0)
                return -1;
        }
        if (*p == '\n') {
            if (fputc('\n', out) == EOF)
                return -1;
            p++;
        }
    }
    /* Refuse the rewrite (unlink temp, no rename) if the user was absent,
     * so a not-found never truncates or replaces /etc/shadow. */
    return ctx->found ? 0 : -1;
}

/* Replace the password field of `user`'s row with `newpw`.  Returns 0 on
 * success, -1 on failure. Holds the passwd-DB lock across the read and the
 * atomic rewrite so concurrent passwd/useradd cannot lose an update
 * (PASSWD-01); pwdb_atomic_rewrite does the O_EXCL 0640 temp + fsync +
 * checked writes + rename, unlinking on any failure (PASSWD-02/03). */
static int
rewrite_shadow(const char *user, const char *newpw)
{
    int    lockfd = pwdb_lock();
    size_t in_len = 0;
    char  *in;
    struct rewrite_ctx ctx;
    int    rc;

    if (lockfd < 0) {
        return -1;
    }
    in = read_shadow_file(&in_len);
    if (in == NULL) {
        pwdb_unlock(lockfd);
        return -1;
    }

    ctx.in = in;
    ctx.user = user;
    ctx.newpw = newpw;
    ctx.found = 0;

    rc = pwdb_atomic_rewrite("/etc/shadow", 0640, rewrite_shadow_cb, &ctx);

    /* Scrub the buffer: it held every user's hash. */
    secure_zero(in, in_len);
    free(in);
    pwdb_unlock(lockfd);
    return rc;
}

static const char *
shadow_current(const char *user, char *line_out, size_t line_sz)
{
    FILE  *f = fopen("/etc/shadow", "r");
    size_t ulen;
    int    at_line_start = 1;
    if (f == NULL) {
        return NULL;
    }
    ulen = strlen(user);
    while (fgets(line_out, (int)line_sz, f) != NULL) {
        size_t l = strlen(line_out);
        int    full_line = (l > 0 && line_out[l - 1] == '\n');
        int    this_start = at_line_start;
        char  *colon;

        /* A chunk that did not end in '\n' was truncated by the buffer;
         * the next chunk is a mid-line continuation and must never be
         * matched as a record start (PASSWD-09). */
        at_line_start = full_line;
        if (!this_start) {
            continue;
        }
        if (strncmp(line_out, user, ulen) != 0 || line_out[ulen] != ':') {
            continue;
        }
        colon = strchr(line_out + ulen + 1, ':');
        if (colon != NULL) {
            *colon = '\0';
        }
        fclose(f);
        return line_out + ulen + 1;
    }
    fclose(f);
    return NULL;
}

int
main(int argc, char **argv)
{
    const char    *user;
    struct passwd *pw;
    uid_t          my_uid = getuid();
    char           cur[PW_MAX];
    char           new1[PW_MAX];
    char           new2[PW_MAX];
    char           line[512];

    if (argc >= 2) {
        user = argv[1];
    } else {
        pw = getpwuid(my_uid);
        if (pw == NULL) {
            fprintf(stderr, "passwd: cannot identify uid %u\n", (unsigned)my_uid);
            return 1;
        }
        user = pw->pw_name;
    }

    pw = getpwnam(user);
    if (pw == NULL) {
        fprintf(stderr, "passwd: user %s does not exist\n", user);
        return 1;
    }

    if (my_uid != 0 && pw->pw_uid != my_uid) {
        fprintf(stderr, "passwd: must be root to change another user's password\n");
        return 1;
    }

    printf("Changing password for %s\n", user);

    if (my_uid != 0) {
        const char *cur_stored;
        char       *cur_hashed;
        if (read_password("Current password: ", cur, sizeof(cur)) < 0) {
            fprintf(stderr, "passwd: no current password read\n");
            return 1;
        }
        cur_stored = shadow_current(user, line, sizeof(line));
        if (cur_stored == NULL) {
            secure_zero(cur, sizeof(cur));
            secure_zero(line, sizeof(line));
            fprintf(stderr, "passwd: authentication failure\n");
            return 1;
        }
        if (cur_stored[0] == '\0') {
            /* An empty stored password authenticates only against an
             * empty entry — never silently skip re-auth (PASSWD-10). */
            if (cur[0] != '\0') {
                secure_zero(cur, sizeof(cur));
                secure_zero(line, sizeof(line));
                fprintf(stderr, "passwd: authentication failure\n");
                return 1;
            }
        } else {
            cur_hashed = crypt(cur, cur_stored);
            if (cur_hashed == NULL ||
                strcmp(cur_hashed, cur_stored) != 0) {
                secure_zero(cur, sizeof(cur));
                secure_zero(line, sizeof(line));
                fprintf(stderr, "passwd: authentication failure\n");
                return 1;
            }
        }
        secure_zero(cur, sizeof(cur));
        secure_zero(line, sizeof(line));
    }

    if (read_password("New password: ", new1, sizeof(new1)) < 0) {
        fprintf(stderr, "passwd: no new password read\n");
        return 1;
    }
    if (read_password("Retype new password: ", new2, sizeof(new2)) < 0) {
        secure_zero(new1, sizeof(new1));
        fprintf(stderr, "passwd: no new password read\n");
        return 1;
    }
    if (strcmp(new1, new2) != 0) {
        secure_zero(new1, sizeof(new1));
        secure_zero(new2, sizeof(new2));
        fprintf(stderr, "passwd: passwords do not match\n");
        return 1;
    }
    secure_zero(new2, sizeof(new2));
    if (new1[0] == '\0') {
        secure_zero(new1, sizeof(new1));
        fprintf(stderr, "passwd: empty password not allowed\n");
        return 1;
    }

    {
        /* Build a $5$ setting with a fresh salt and crypt the new
         * password under it.  The salt is filled directly from
         * /dev/urandom (8 chars, 6 bits each); if we cannot obtain the
         * full 8 random bytes we fail closed rather than fall back to a
         * predictable pid/time-derived salt (PASSWD-04/08). */
        static const char b64[] =
            "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        char  setting[16];
        char *hashed;
        int           i, urand_fd, rc;
        unsigned char rb[8];
        size_t        got = 0;

        urand_fd = open("/dev/urandom", O_RDONLY);
        if (urand_fd < 0) {
            secure_zero(new1, sizeof(new1));
            fprintf(stderr, "passwd: cannot open /dev/urandom for salt\n");
            return 1;
        }
        while (got < sizeof(rb)) {
            ssize_t r = read(urand_fd, rb + got, sizeof(rb) - got);
            if (r <= 0) {
                break;
            }
            got += (size_t)r;
        }
        close(urand_fd);
        if (got != sizeof(rb)) {
            secure_zero(new1, sizeof(new1));
            fprintf(stderr, "passwd: short read from /dev/urandom\n");
            return 1;
        }

        memcpy(setting, "$5$", 3);
        for (i = 0; i < 8; i++) {
            setting[3 + i] = b64[rb[i] & 63];
        }
        setting[11] = '\0';

        hashed = crypt(new1, setting);
        secure_zero(new1, sizeof(new1));
        secure_zero(rb, sizeof(rb));
        if (hashed == NULL) {
            fprintf(stderr, "passwd: crypt() failed\n");
            return 1;
        }

        rc = rewrite_shadow(user, hashed);
        /* hashed points into crypt(3)'s static buffer; overwrite it so the
         * new hash does not linger there. */
        secure_zero(hashed, strlen(hashed));
        if (rc != 0) {
            fprintf(stderr, "passwd: failed to update /etc/shadow\n");
            return 1;
        }
    }

    printf("passwd: password updated for %s\n", user);
    return 0;
}
