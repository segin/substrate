/*
 * bin/su/su.c — switch user.
 *
 * Usage:  su [-l|--login] [-c cmd] [user]
 *
 * Default user is "root".  If the invoker is already uid 0 we skip
 * the password prompt; otherwise we read a password and compare
 * against /etc/shadow via crypt(3).
 */

#include <crypt.h>
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define SU_PASS_MAX 128

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
        }
        fclose(f);
        return line + ulen + 1;
    }
    fclose(f);
    return NULL;
}

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

int
main(int argc, char **argv)
{
    const char    *target = "root";
    int            login_shell = 0;
    const char    *exec_cmd = NULL;
    int            i;
    struct passwd *pw;
    char           pass[SU_PASS_MAX];
    const char    *stored;
    const char    *shell;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-l") == 0 || strcmp(a, "--login") == 0 ||
            strcmp(a, "-") == 0) {
            login_shell = 1;
        } else if (strcmp(a, "-c") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "su: -c requires an argument\n");
                return 1;
            }
            exec_cmd = argv[++i];
        } else if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "su: unknown option %s\n", a);
            return 1;
        } else {
            target = a;
        }
    }

    pw = getpwnam(target);
    if (pw == NULL) {
        fprintf(stderr, "su: user %s does not exist\n", target);
        return 1;
    }

    /* Root skips the password prompt. */
    if (getuid() != 0) {
        if (read_password("Password: ", pass, sizeof(pass)) < 0) {
            return 1;
        }
        stored = lookup_shadow(target);
        if (stored == NULL && pw->pw_passwd != NULL &&
            pw->pw_passwd[0] != 'x') {
            stored = pw->pw_passwd;
        }
        if (!shadow_matches(stored, pass)) {
            fprintf(stderr, "su: Authentication failure\n");
            return 1;
        }
    }

    if (setgid(pw->pw_gid) != 0) {
        perror("su: setgid");
        return 1;
    }
    (void)initgroups(pw->pw_name, pw->pw_gid);
    if (setuid(pw->pw_uid) != 0) {
        perror("su: setuid");
        return 1;
    }

    if (login_shell) {
        setenv("HOME", pw->pw_dir, 1);
        setenv("SHELL", pw->pw_shell, 1);
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        if (chdir(pw->pw_dir) != 0) {
            (void)chdir("/");
        }
    } else {
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
    }

    shell = pw->pw_shell;
    if (shell == NULL || shell[0] == '\0') {
        shell = "/bin/sh";
    }

    if (exec_cmd != NULL) {
        execl(shell, shell, "-c", exec_cmd, (char *)NULL);
    } else if (login_shell) {
        execl(shell, shell, "-l", (char *)NULL);
        execl(shell, shell, (char *)NULL);
    } else {
        execl(shell, shell, (char *)NULL);
    }
    perror("su: exec");
    return 1;
}
