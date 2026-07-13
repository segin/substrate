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

#include <crypt.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/pwdb.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

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

static int
read_password(const char *prompt, char *buf, size_t bufsz)
{
    struct termios old_tio;
    struct termios new_tio;
    int            tty_ok;
    size_t         i = 0;
    char           c;
    ssize_t        n;

    fputs(prompt, stdout);
    fflush(stdout);

    tty_ok = (tcgetattr(0, &old_tio) == 0);
    if (tty_ok) {
        new_tio = old_tio;
        new_tio.c_lflag &= ~(ECHO | ECHONL);
        new_tio.c_lflag |= ICANON;
        tcsetattr(0, TCSANOW, &new_tio);
    }
    while (i + 1 < bufsz) {
        n = read(0, &c, 1);
        if (n <= 0 || c == '\n' || c == '\r') {
            break;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    if (tty_ok) {
        tcsetattr(0, TCSANOW, &old_tio);
    }
    fputc('\n', stdout);
    fflush(stdout);
    return (int)i;
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
    if (f == NULL) {
        return NULL;
    }
    ulen = strlen(user);
    while (fgets(line_out, (int)line_sz, f) != NULL) {
        char *colon;
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
            return 1;
        }
        cur_stored = shadow_current(user, line, sizeof(line));
        if (cur_stored == NULL) {
            fprintf(stderr, "passwd: authentication failure\n");
            return 1;
        }
        if (cur_stored[0] != '\0') {
            cur_hashed = crypt(cur, cur_stored);
            if (cur_hashed == NULL ||
                strcmp(cur_hashed, cur_stored) != 0) {
                fprintf(stderr, "passwd: authentication failure\n");
                return 1;
            }
        }
    }

    if (read_password("New password: ", new1, sizeof(new1)) < 0) {
        return 1;
    }
    if (read_password("Retype new password: ", new2, sizeof(new2)) < 0) {
        return 1;
    }
    if (strcmp(new1, new2) != 0) {
        fprintf(stderr, "passwd: passwords do not match\n");
        return 1;
    }
    if (new1[0] == '\0') {
        fprintf(stderr, "passwd: empty password not allowed\n");
        return 1;
    }

    {
        /* Build a $5$ setting with a fresh salt and crypt the new
         * password under it.  Salt: 8 chars from the b64 alphabet
         * seeded from time + uid + a /dev/urandom byte if available. */
        static const char b64[] =
            "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
        char  setting[64];
        char *hashed;
        unsigned long seed;
        int           i, urand_fd;
        unsigned char rb[8];

        seed = (unsigned long)getpid() * 0x9E3779B1u + (unsigned long)getuid();
        urand_fd = open("/dev/urandom", 0);
        if (urand_fd >= 0) {
            (void)read(urand_fd, rb, sizeof(rb));
            close(urand_fd);
            for (i = 0; i < 8; i++) {
                seed = seed * 1103515245u + rb[i];
            }
        }

        memcpy(setting, "$5$", 3);
        for (i = 0; i < 8; i++) {
            setting[3 + i] = b64[seed & 63];
            seed >>= 6;
            if (seed == 0) seed = 0xDEADBEEFu ^ (unsigned long)i;
        }
        setting[11] = '\0';

        hashed = crypt(new1, setting);
        if (hashed == NULL) {
            fprintf(stderr, "passwd: crypt() failed\n");
            return 1;
        }

        if (rewrite_shadow(user, hashed) != 0) {
            fprintf(stderr, "passwd: failed to update /etc/shadow\n");
            return 1;
        }
    }

    printf("passwd: password updated for %s\n", user);
    return 0;
}
