/*
 * sbin/getty — open a tty line, prompt for a username, exec login.
 *
 * Usage:
 *   getty [-h] [-i] [-l <login-prog>] tty [speed [type [linedisc]]]
 *
 *   -h           Skip the hostname/banner header.
 *   -i           "init" mode: open the tty but skip the prompt and
 *                exec login immediately (login will do its own
 *                prompt).  Equivalent to BSD's `getty -i`.
 *   -l <prog>    Override the login program (default /bin/login).
 *   tty          tty device, with or without leading "/dev/".  If a
 *                relative name is given, "/dev/" is prepended.
 *   speed type linedisc
 *                Accepted for BSD compatibility; ignored — Substrate
 *                drivers manage line state via ioctl(TIOCSTI) etc.,
 *                not through gettytab speed entries.
 *
 * Per-tty session setup:
 *   1. close stdin/stdout/stderr
 *   2. open(tty, O_RDWR|O_NOCTTY)
 *   3. setsid() — drop any old controlling tty
 *   4. ioctl(fd, TIOCSCTTY, 0) — make this our controlling tty
 *   5. dup2 onto fds 0/1/2
 *   6. termios: ICANON, ISIG, ECHO, ECHOE, ECHOK, ICRNL, OPOST, ONLCR,
 *      CS8 + CREAD + HUPCL + CLOCAL, IXON enabled, BRKINT enabled.
 *   7. write /etc/issue (with limited %-expansion), then a "login: "
 *      prompt; read the username; exec login passing it as argv[1].
 *
 * If anything fails fatally we exit non-zero so /sbin/init can
 * notice and respawn us.
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef TIOCSCTTY
#define TIOCSCTTY 0x540E   /* Linux value; Substrate honours the same. */
#endif

#define USER_MAX 64

static const char *prog = "getty";

static void
die(const char *what)
{
    fprintf(stderr, "%s: %s: %s\n", prog, what, strerror(errno));
    exit(1);
}

/*
 * Open the line + make it the controlling tty.  Returns the fd
 * (0/1/2 will all dup'd onto it before this returns).
 */
static int
open_line(const char *tty_path)
{
    int fd;

    /* Close anything inherited from init. */
    (void)close(0);
    (void)close(1);
    (void)close(2);

    fd = open(tty_path, O_RDWR | O_NOCTTY);
    if (fd < 0) {
        /* stderr is closed; bail to /dev/console as a last resort. */
        int c = open("/dev/console", O_WRONLY);
        if (c >= 0) {
            const char *msg = "getty: cannot open line\n";
            (void)write(c, msg, strlen(msg));
            (void)close(c);
        }
        exit(1);
    }

    /* New session and process group; required so TIOCSCTTY actually
     * binds the tty.  Failing setsid means we already are session
     * leader (init or solo) — keep going. */
    (void)setsid();
    (void)ioctl(fd, TIOCSCTTY, 0);

    if (fd != 0) {
        if (dup2(fd, 0) < 0) die("dup2(0)");
    }
    if (fd != 1) {
        if (dup2(fd, 1) < 0) die("dup2(1)");
    }
    if (fd != 2) {
        if (dup2(fd, 2) < 0) die("dup2(2)");
    }
    if (fd > 2) {
        close(fd);
    }
    return 0;
}

static void
apply_termios_defaults(void)
{
    struct termios t;
    if (tcgetattr(0, &t) != 0) {
        return;  /* not a real tty (file?) — leave alone */
    }
    t.c_iflag = BRKINT | ICRNL | IXON;
    t.c_oflag = OPOST | ONLCR;
    t.c_cflag = (t.c_cflag & ~CSIZE) | CS8 | CREAD | HUPCL | CLOCAL;
    /* ECHONL — "echo NL even when ECHO is off" — must NOT be set:
     * line editors (zsh ZLE, fish, readline, ...) clear ECHO and
     * handle their own echo.  With ECHONL on, the line discipline
     * still echoes the newline on Enter, and the line editor's own
     * post-edit newline lands right behind it: two line feeds reach
     * the terminal, displayed as a blank line between every command
     * line and its output.  Console + xterm both showed this; ssh /
     * telnet did not because their pty slaves were never touched by
     * getty. */
    t.c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE
              | IEXTEN;
    t.c_cc[VEOF]   = 4;   /* ^D */
    t.c_cc[VEOL]   = 0;
    /* The PS/2 keyboard driver maps the Backspace key to 127 (DEL),
     * matching modern Linux/BSD defaults — not 8 (^H).  VERASE must
     * agree or canonical-mode erase silently drops control chars
     * into the read buffer where the shell renders them as glyphs. */
    t.c_cc[VERASE] = 127; /* DEL */
    t.c_cc[VINTR]  = 3;   /* ^C */
    t.c_cc[VKILL]  = 21;  /* ^U */
    t.c_cc[VQUIT]  = 28;  /* ^\ */
    t.c_cc[VSUSP]  = 26;  /* ^Z */
    t.c_cc[VSTART] = 17;  /* ^Q */
    t.c_cc[VSTOP]  = 19;  /* ^S */
    t.c_cc[VMIN]   = 1;
    t.c_cc[VTIME]  = 0;
    (void)tcsetattr(0, TCSANOW, &t);
}

/* Read the hostname into a caller buffer.  Falls back to
 * /etc/hostname's first line if gethostname doesn't return anything
 * useful (Substrate's sethostname may not have been called). */
static void
get_hostname(char *out, size_t out_sz)
{
    if (gethostname(out, out_sz) == 0 && out[0] != '\0') {
        return;
    }
    {
        FILE *f = fopen("/etc/hostname", "r");
        if (f != NULL) {
            if (fgets(out, (int)out_sz, f) != NULL) {
                size_t n = strlen(out);
                while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r')) {
                    out[--n] = '\0';
                }
                fclose(f);
                if (out[0] != '\0') {
                    return;
                }
            }
            fclose(f);
        }
    }
    strlcpy(out, "substrate", out_sz);
}

/* Stream /etc/issue to stdout with BSD-style escape expansion:
 *
 *   \\n  newline
 *   \\s  system name ("Substrate")
 *   \\h  hostname
 *   \\r  Substrate release (uname -r)
 *   \\l  tty name passed on the command line
 *   \\d  current date
 *   \\t  current time
 *   \\u  number of currently-logged-in users (always 0 today)
 *
 * Returns 0 if /etc/issue was opened & streamed (regardless of
 * length) and 1 if no /etc/issue is present.
 */
static int
print_issue(const char *tty_name)
{
    FILE *f = fopen("/etc/issue", "r");
    char  host[64];
    int   c;

    if (f == NULL) {
        return 1;
    }

    get_hostname(host, sizeof(host));

    while ((c = fgetc(f)) != EOF) {
        if (c != '\\') {
            putchar(c);
            continue;
        }
        c = fgetc(f);
        if (c == EOF) {
            putchar('\\');
            break;
        }
        switch (c) {
            case 'n':
                putchar('\n');
                break;
            case 's':
                fputs("Substrate", stdout);
                break;
            case 'h':
                fputs(host, stdout);
                break;
            case 'r':
                fputs("0.1", stdout);
                break;
            case 'l':
                fputs(tty_name ? tty_name : "tty", stdout);
                break;
            case 'd': {
                time_t now = time(NULL);
                if (now != (time_t)-1) {
                    struct tm *tm = localtime(&now);
                    if (tm != NULL) {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                                 tm->tm_year + 1900, tm->tm_mon + 1,
                                 tm->tm_mday);
                        fputs(buf, stdout);
                    }
                }
                break;
            }
            case 't': {
                time_t now = time(NULL);
                if (now != (time_t)-1) {
                    struct tm *tm = localtime(&now);
                    if (tm != NULL) {
                        char buf[16];
                        snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                                 tm->tm_hour, tm->tm_min, tm->tm_sec);
                        fputs(buf, stdout);
                    }
                }
                break;
            }
            case 'u':
                putchar('0');
                break;
            case '\\':
                putchar('\\');
                break;
            default:
                putchar('\\');
                putchar(c);
                break;
        }
    }
    fclose(f);
    return 0;
}

/* Read a username from stdin.  Strips backspaces explicitly even
 * though canonical mode usually handles VERASE — some clients
 * (raw paste, no termios) bypass it. */
static int
read_username(char *out, size_t out_sz)
{
    size_t i = 0;
    char   c;
    while (i + 1 < out_sz) {
        ssize_t n = read(0, &c, 1);
        if (n <= 0) {
            return -1;
        }
        if (c == '\n' || c == '\r') {
            break;
        }
        if (c == '\b' || c == 127) {
            if (i > 0) i--;
            continue;
        }
        /* Accept printable ASCII and the canonical login-name chars
         * (POSIX 3.437): letters, digits, `_`, `-`, and `$` as last
         * char.  Quietly drop anything else so a stray binary byte
         * doesn't lock the prompt. */
        if (c == '.' || c == '_' || c == '-' || c == '$' ||
            (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9')) {
            out[i++] = c;
        }
    }
    out[i] = '\0';
    return (int)i;
}

static void
usage(void)
{
    fprintf(stderr,
        "usage: %s [-h] [-i] [-l login-prog] tty [speed [type [linedisc]]]\n",
        prog);
    exit(2);
}

int
main(int argc, char **argv)
{
    const char *tty_arg = NULL;
    const char *login_prog = "/bin/login";
    int         skip_banner = 0;
    int         init_mode   = 0;
    int         i;
    char        tty_path[128];
    char        user[USER_MAX];

    prog = argv[0];

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-h") == 0) {
            skip_banner = 1;
        } else if (strcmp(a, "-i") == 0) {
            init_mode = 1;
        } else if (strcmp(a, "-l") == 0) {
            if (i + 1 >= argc) usage();
            login_prog = argv[++i];
        } else if (strcmp(a, "--help") == 0) {
            usage();
        } else if (a[0] == '-' && a[1] != '\0') {
            usage();
        } else if (tty_arg == NULL) {
            tty_arg = a;
        } else {
            /* speed / type / linedisc — accepted, ignored. */
        }
    }

    if (tty_arg == NULL) {
        usage();
    }

    /* Normalise to /dev/<name> unless already absolute. */
    if (tty_arg[0] == '/') {
        snprintf(tty_path, sizeof(tty_path), "%s", tty_arg);
    } else {
        snprintf(tty_path, sizeof(tty_path), "/dev/%s", tty_arg);
    }

    open_line(tty_path);
    apply_termios_defaults();

    if (!init_mode) {
        if (!skip_banner) {
            if (print_issue(tty_arg) != 0) {
                /* No /etc/issue — emit a minimal banner so the user
                 * knows they're logging in. */
                char host[64];
                get_hostname(host, sizeof(host));
                printf("\nSubstrate %s (%s)\n\n", host, tty_arg);
            }
        }
        printf("login: ");
        fflush(stdout);
        if (read_username(user, sizeof(user)) <= 0) {
            /* EOF / hangup — bail so init respawns us. */
            exit(0);
        }
        execl(login_prog, login_prog, user, (char *)NULL);
    } else {
        execl(login_prog, login_prog, (char *)NULL);
    }

    fprintf(stderr, "getty: exec %s failed: %s\n", login_prog, strerror(errno));
    return 1;
}
