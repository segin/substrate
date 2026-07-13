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

extern char **environ;

/*
 * Remove environment variables that let an unprivileged caller subvert the
 * dynamic loader or the target shell's startup across the privilege
 * boundary. Without this, `LD_PRELOAD=/tmp/evil.so su -c cmd` (or IFS / ENV
 * / BASH_ENV) executes attacker code as the target uid. Called before exec.
 */
static void sanitize_environment(void)
{
	static const char *const unsafe[] = {
		"IFS", "ENV", "BASH_ENV", "CDPATH", "SHELLOPTS", "BASHOPTS",
		"GLOBIGNORE", "PS1", "PS2", "PS4", "PROMPT_COMMAND", NULL
	};

	/* Strip every LD_* / _RLD* entry; unsetenv() shifts environ, so on a
	 * removal we re-examine the same index. */
	for (int i = 0; environ[i] != NULL; ) {
		if (strncmp(environ[i], "LD_", 3) == 0 ||
		    strncmp(environ[i], "_RLD", 4) == 0) {
			char name[128];
			const char *eq = strchr(environ[i], '=');
			size_t n = eq ? (size_t)(eq - environ[i])
			              : strlen(environ[i]);
			if (n >= sizeof(name))
				n = sizeof(name) - 1;
			memcpy(name, environ[i], n);
			name[n] = '\0';
			unsetenv(name);
		} else {
			i++;
		}
	}
	for (int i = 0; unsafe[i] != NULL; i++)
		unsetenv(unsafe[i]);
}

#define SU_PASS_MAX 128

#include <signal.h>

/* Overwrite a buffer so the compiler cannot elide the clear (no
 * explicit_bzero on this target). */
static void
secure_zero(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--)
        *v++ = 0;
}

/* Constant-time byte compare of two NUL-terminated strings (SU-06). */
static int
ct_streq(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (unsigned char)(la ^ lb);
    for (size_t i = 0; i < la; i++)
        diff |= (unsigned char)(a[i] ^ b[i % (lb ? lb : 1)]);
    return diff == 0 && la == lb;
}

/* Terminal state to restore if a signal interrupts the echo-off prompt. */
static struct termios g_saved_tio;
static volatile sig_atomic_t g_tio_saved;

static void
restore_tio_and_die(int sig)
{
    if (g_tio_saved)
        tcsetattr(0, TCSANOW, &g_saved_tio);
    _exit(128 + sig);
}

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
        /* Arrange to restore the terminal if a signal fires while echo is
         * off, otherwise ^C leaves the tty un-echoing and leaks the next
         * typed line (SU-07). */
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
            /* EOF before any character is a read failure, not an empty
             * password (SU-08). */
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
    /* Fail closed: an empty or locked hash field (empty, '*' or '!') is
     * not a valid password to authenticate against (SU-04); this also
     * closes the "EOF -> empty attempt matches empty field" bypass. */
    if (stored[0] == '\0' || stored[0] == '*' || stored[0] == '!') {
        return 0;
    }
    hashed = crypt(attempt, stored);
    if (hashed == NULL) {
        return 0;
    }
    return ct_streq(hashed, stored);
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
        /* Only fall back to the passwd field if it holds a real hash
         * (not "x"/empty/locked) — a missing shadow entry must not become
         * a passwordless success (SU-04). */
        if (stored == NULL && pw->pw_passwd != NULL &&
            pw->pw_passwd[0] != 'x' && pw->pw_passwd[0] != '\0' &&
            pw->pw_passwd[0] != '*' && pw->pw_passwd[0] != '!') {
            stored = pw->pw_passwd;
        }
        int ok = shadow_matches(stored, pass);
        secure_zero(pass, sizeof(pass));	/* SU-05 */
        if (!ok) {
            fprintf(stderr, "su: Authentication failure\n");
            return 1;
        }
    }

    if (setgid(pw->pw_gid) != 0) {
        perror("su: setgid");
        return 1;
    }
    /* Must succeed before setuid: on failure the target shell would keep
     * root's supplementary groups (e.g. gid 0/wheel) after the uid drop. */
    if (initgroups(pw->pw_name, pw->pw_gid) != 0) {
        perror("su: initgroups");
        return 1;
    }
    if (setuid(pw->pw_uid) != 0) {
        perror("su: setuid");
        return 1;
    }

    sanitize_environment();

    /* Establish a safe PATH across the boundary; the inherited one may
     * point at attacker-writable dirs (SU-03). */
    const char *safe_path = (pw->pw_uid == 0)
        ? "/sbin:/bin:/usr/sbin:/usr/bin"
        : "/bin:/usr/bin:/usr/local/bin";

    if (login_shell) {
        setenv("HOME", pw->pw_dir, 1);
        setenv("SHELL", pw->pw_shell, 1);
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("PATH", safe_path, 1);
        if (chdir(pw->pw_dir) != 0) {
            (void)chdir("/");
        }
    } else {
        setenv("USER", pw->pw_name, 1);
        setenv("LOGNAME", pw->pw_name, 1);
        setenv("PATH", safe_path, 1);
    }

    shell = pw->pw_shell;
    if (shell == NULL || shell[0] == '\0') {
        shell = "/bin/sh";
    }

    /* For a login shell, pass argv[0] as "-<basename>" so the shell
     * detects login mode (SU-09). */
    char login_arg0[64];
    const char *base = strrchr(shell, '/');
    base = base ? base + 1 : shell;
    snprintf(login_arg0, sizeof(login_arg0), "-%s", base);

    if (exec_cmd != NULL) {
        execl(shell, shell, "-c", exec_cmd, (char *)NULL);
    } else if (login_shell) {
        execl(shell, login_arg0, (char *)NULL);
    } else {
        execl(shell, shell, (char *)NULL);
    }
    perror("su: exec");
    return 1;
}
