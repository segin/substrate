/*
 * wall — write a message to every logged-in user's terminal.
 *
 * Reads the message from a file argument, or from stdin when none is
 * given, then writes it — behind the customary banner — to the tty
 * of every USER_PROCESS record in /var/run/utmp.
 *
 *   wall [file]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <pwd.h>
#include <utmp.h>
#include <sys/types.h>

/* Collect the message into `buf` (NUL-terminated); return its length. */
static size_t read_message(const char *path, char *buf, size_t cap)
{
    FILE *src = stdin;
    if (cap == 0) return 0;                 /* guard cap-1 underflow (WALL-02) */
    if (path) {
        src = fopen(path, "r");
        if (!src) { perror(path); exit(1); }
    }
    size_t n = 0;
    int c;
    while (n < cap - 1 && (c = fgetc(src)) != EOF)
        buf[n++] = (char)c;
    buf[n] = '\0';
    if (src != stdin) fclose(src);
    return n;
}

/*
 * Render an untrusted message safe to write to a terminal: pass printable
 * bytes, newline and tab; render other control bytes as ^X and high-bit
 * bytes as M-.  Without this, ESC/OSC/DECRQSS sequences in the message would
 * be interpreted by every recipient's terminal (retitle, clear, type-back,
 * OSC52 clipboard) — a broadcast escape-injection (WALL-01).  Returns a
 * malloc'd buffer; *outlen gets its length.
 */
static char *sanitize_message(const char *in, size_t n, size_t *outlen)
{
    char  *out = malloc(n * 4 + 1);
    size_t o = 0;
    if (out == NULL) return NULL;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '\n' || c == '\t') { out[o++] = (char)c; continue; }
        if (c == '\r') continue;                 /* drop bare CR */
        if (c >= 0x80) { out[o++] = 'M'; out[o++] = '-'; c = (unsigned char)(c & 0x7f); }
        if (c < 0x20 || c == 0x7f) { out[o++] = '^'; out[o++] = (char)(c ^ 0x40); }
        else                       { out[o++] = (char)c; }
    }
    out[o] = '\0';
    *outlen = o;
    return out;
}

/* Write the whole buffer, retrying short writes/EINTR (WALL-05). */
static int full_write(int fd, const char *buf, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)w;
    }
    return 0;
}

/* Best-effort name of the invoking user. */
static const char *who_am_i(void)
{
    const char *u = getlogin();
    if (u && u[0]) return u;
    struct passwd *pw = getpwuid(getuid());
    if (pw && pw->pw_name) return pw->pw_name;
    return "root";
}

int main(int argc, char *argv[])
{
    static char msg[8192];
    size_t rawlen = read_message(argc > 1 ? argv[1] : NULL, msg, sizeof msg);
    size_t mlen = 0;
    char  *smsg = sanitize_message(msg, rawlen, &mlen);
    if (smsg == NULL) { fprintf(stderr, "wall: out of memory\n"); return 1; }

    /* Compose the banner. */
    char host[64] = "localhost";
    gethostname(host, sizeof host);
    host[sizeof host - 1] = '\0';

    char when[16] = "";
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) strftime(when, sizeof when, "%H:%M", tm);

    char banner[256];
    int blen = snprintf(banner, sizeof banner,
        "\r\n\007Broadcast message from %s@%s at %s ...\r\n\r\n",
        who_am_i(), host, when);
    if (blen < 0) blen = 0;

    /* Fan the message out to every logged-in user's terminal. */
    FILE *ut = fopen(UTMP_FILE, "r");
    if (!ut) {
        fprintf(stderr, "wall: cannot open %s\n", UTMP_FILE);
        return 1;
    }

    struct utmp u;
    int delivered = 0;
    while (fread(&u, sizeof u, 1, ut) == 1) {
        if (u.ut_type != USER_PROCESS) continue;
        if (u.ut_line[0] == '\0') continue;

        char dev[8 + UT_LINESIZE];
        char line[UT_LINESIZE + 1];
        memcpy(line, u.ut_line, UT_LINESIZE);
        line[UT_LINESIZE] = '\0';

        /* Reject a ut_line with '/' or a leading '.' so a crafted utmp
         * (ut_line="../../etc/passwd") can't redirect the write outside
         * /dev (WALL-03). */
        if (strchr(line, '/') != NULL || line[0] == '.' || line[0] == '\0')
            continue;
        snprintf(dev, sizeof dev, "/dev/%s", line);

        /* O_NONBLOCK: never block wall on a wedged terminal.  O_NOFOLLOW +
         * an isatty() gate stop a symlink at /dev/<line> from redirecting
         * the broadcast into an arbitrary file (WALL-04). */
        int fd = open(dev, O_WRONLY | O_NONBLOCK | O_NOFOLLOW);
        if (fd < 0) continue;
        if (!isatty(fd)) { close(fd); continue; }
        (void)full_write(fd, banner, (size_t)blen);
        (void)full_write(fd, smsg, mlen);
        (void)full_write(fd, "\r\n", 2);
        close(fd);
        delivered++;
    }
    fclose(ut);

    free(smsg);
    if (delivered == 0)
        fprintf(stderr, "wall: no logged-in users\n");
    return 0;
}
