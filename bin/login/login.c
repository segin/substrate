/*
 * bin/login/login.c — real login.
 *
 * Prompts for a username + password, validates against /etc/passwd
 * + /etc/shadow via crypt(3), then becomes that user (setgid,
 * initgroups, setuid), sets a minimal environment (HOME / SHELL /
 * USER / PATH / LOGNAME), chdirs to the home directory, and execs
 * the user's login shell.
 *
 * Shadow entries are matched as follows:
 *
 *   ""              → no password required (account, no auth)
 *   "*" or "!..."   → account locked
 *   "$5$...", "$1$..."  → hashed with crypt(3); we crypt the
 *                          attempt under the stored salt and
 *                          string-compare
 *   anything else (13-char or shorter) → treated as a traditional
 *                          DES crypt setting and verified the same
 *                          way
 *
 * (Plaintext shadow entries are no longer accepted — set up real
 * hashes with passwd(1) before deploying.)
 */

#include <crypt.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define LOGIN_MAX_USER  64
#define LOGIN_MAX_PASS  64
#define LOGIN_MAX_TRIES 3

static int
read_line_echo(int fd, char *buf, size_t bufsz)
{
    size_t i = 0;
    char   c;
    while (i + 1 < bufsz) {
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            break;
        }
        if (c == '\n' || c == '\r') {
            break;
        }
        buf[i++] = c;
    }
    buf[i] = '\0';
    return (int)i;
}

static int
read_line_no_echo(int fd, char *buf, size_t bufsz)
{
    struct termios old_tio;
    struct termios new_tio;
    int            tty_ok = (tcgetattr(fd, &old_tio) == 0);
    int            len;

    if (tty_ok) {
        new_tio = old_tio;
        new_tio.c_lflag &= ~(ECHO | ECHONL);
        new_tio.c_lflag |= ICANON;
        tcsetattr(fd, TCSANOW, &new_tio);
    }
    len = read_line_echo(fd, buf, bufsz);
    if (tty_ok) {
        tcsetattr(fd, TCSANOW, &old_tio);
    }
    fputc('\n', stdout);
    fflush(stdout);
    return len;
}

/*
 * Lookup the shadow password for `user`.  Returns a pointer into a
 * static buffer (overwritten on each call), or NULL if the user has
 * no shadow entry.  Caller treats `""` (empty) as "no password
 * required" and `"*"` / `"!"` as "account locked".
 */
static const char *
lookup_shadow(const char *user)
{
    static char line[512];
    FILE       *f = fopen("/etc/shadow", "r");
    size_t      ulen;

    if (f == NULL) {
        return NULL;
    }
    ulen = strlen(user);
    while (fgets(line, sizeof(line), f) != NULL) {
        char *colon;
        if (strncmp(line, user, ulen) != 0 || line[ulen] != ':') {
            continue;
        }
        colon = strchr(line + ulen + 1, ':');
        if (colon != NULL) {
            *colon = '\0';
        } else {
            size_t n = strlen(line);
            while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
                line[--n] = '\0';
            }
        }
        fclose(f);
        return line + ulen + 1;
    }
    fclose(f);
    return NULL;
}

/*
 * Compare `attempt` against the stored shadow string using
 * crypt(3).  See file header for the full match policy.
 */
static int
shadow_matches(const char *stored, const char *attempt)
{
    char *hashed;
    if (stored == NULL) {
        return 0;
    }
    if (stored[0] == '\0') {
        return 1;
    }
    if (stored[0] == '*' || stored[0] == '!') {
        return 0;
    }
    hashed = crypt(attempt, stored);
    if (hashed == NULL) {
        return 0;
    }
    return strcmp(hashed, stored) == 0;
}

static int
do_login_one(const char *forced_user)
{
    char           user[LOGIN_MAX_USER];
    char           pass[LOGIN_MAX_PASS];
    struct passwd *pw;
    const char    *stored;

    if (forced_user != NULL) {
        snprintf(user, sizeof(user), "%s", forced_user);
    } else {
        printf("login: ");
        fflush(stdout);
        if (read_line_echo(0, user, sizeof(user)) <= 0) {
            return -1;
        }
    }

    printf("Password: ");
    fflush(stdout);
    if (read_line_no_echo(0, pass, sizeof(pass)) < 0) {
        return -1;
    }

    pw = getpwnam(user);
    if (pw == NULL) {
        printf("Login incorrect\n");
        return 0;
    }

    stored = lookup_shadow(user);
    if (stored == NULL) {
        if (pw->pw_passwd != NULL && pw->pw_passwd[0] != 'x') {
            stored = pw->pw_passwd;
        }
    }

    if (!shadow_matches(stored, pass)) {
        printf("Login incorrect\n");
        return 0;
    }

    /* Become the user.  Order matters: setgid+initgroups BEFORE
     * setuid, otherwise we lose the privilege needed to install the
     * supplementary group list. */
    if (setgid(pw->pw_gid) != 0) {
        perror("login: setgid");
        return 0;
    }
    if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
        /* Non-fatal: no group list, but keep going. */
    }
    if (setuid(pw->pw_uid) != 0) {
        perror("login: setuid");
        return 0;
    }

    setenv("HOME", pw->pw_dir, 1);
    setenv("SHELL", pw->pw_shell, 1);
    setenv("USER", pw->pw_name, 1);
    setenv("LOGNAME", pw->pw_name, 1);
    if (getenv("PATH") == NULL) {
        setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/sbin:/usr/sbin", 1);
    }
    if (chdir(pw->pw_dir) != 0) {
        (void)chdir("/");
    }

    printf("\n");

    {
        const char *shell = pw->pw_shell;
        if (shell == NULL || shell[0] == '\0') {
            shell = "/bin/sh";
        }
        execl(shell, shell, "-l", (char *)NULL);
        execl(shell, shell, (char *)NULL);
        perror("login: exec");
        return -1;
    }
}

int
main(int argc, char **argv)
{
    int tries;
    const char *forced = NULL;

    if (argc >= 2 && argv[1][0] != '-') {
        forced = argv[1];
    }

    printf("Substrate (%s)\n\n", "tty");

    for (tries = 0; tries < LOGIN_MAX_TRIES; tries++) {
        int rc = do_login_one(forced);
        if (rc < 0) {
            return 1;
        }
        forced = NULL;
    }
    return 1;
}
