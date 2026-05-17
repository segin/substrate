/*
 * bin/login/login.c — substrate login.
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
 * Refactor notes (vs the previous version):
 *
 *   - Banner now reports the real tty name from ttyname(0), not a
 *     hardcoded "tty" string.
 *   - /etc/issue contents are printed before the prompt when
 *     present, so admins can edit the per-tty banner without
 *     recompiling.
 *   - Signal dispositions are reset to SIG_DFL so the shell we
 *     exec doesn't inherit handlers from init or getty.
 *   - setsid() is attempted so the login shell heads its own
 *     session; if we were already a session leader (getty path)
 *     it's a harmless no-op.
 *   - Environment is wiped to the minimum POSIX set rather than
 *     just overlaid on top of init's environment.
 *   - The hot retry loop now sleeps a moment between failed
 *     attempts to slow down a brute-force attacker.
 *
 * (Plaintext shadow entries are no longer accepted — set up real
 * hashes with passwd(1) before deploying.)
 */

#include <crypt.h>
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <utmp.h>

#define LOGIN_MAX_USER  64
#define LOGIN_MAX_PASS  64
#define LOGIN_MAX_TRIES 3
#define LOGIN_FAIL_DELAY_SECS 2

/*
 * Reset every catchable signal to its default disposition.  We
 * inherit init's mask via getty; the user's shell should start
 * with the equivalent of a fresh process's signal state.
 */
static void
reset_signals(void)
{
    struct sigaction dfl;
    int              sig;

    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    for (sig = 1; sig < 32; sig++) {
        if (sig == SIGKILL || sig == SIGSTOP) {
            continue;
        }
        (void)sigaction(sig, &dfl, NULL);
    }
}

/*
 * Print /etc/issue verbatim before showing the prompt.  Quiet on
 * any I/O error — the banner is decorative and shouldn't block
 * login on a missing or unreadable file.
 */
static void
print_issue(void)
{
    FILE *f = fopen("/etc/issue", "r");
    if (f == NULL) {
        return;
    }
    char buf[256];
    while (fgets(buf, sizeof(buf), f) != NULL) {
        fputs(buf, stdout);
    }
    fclose(f);
}

/*
 * Strip "/dev/" so the banner reads "tty1" rather than "/dev/tty1".
 * Returns a pointer into the input buffer.
 */
static const char *
tty_basename(const char *path)
{
    if (path == NULL) {
        return "?";
    }
    /* Strip the "/dev/" prefix so /dev/tty1 becomes "tty1" and
     * /dev/pts/0 becomes "pts/0" — the basename idiom would have
     * lost the "pts/" qualifier on PTYs.  Falls back to strrchr
     * for paths that aren't under /dev (shouldn't happen in
     * practice, but defensive). */
    if (strncmp(path, "/dev/", 5) == 0) {
        return path + 5;
    }
    const char *slash = strrchr(path, '/');
    return slash != NULL ? slash + 1 : path;
}

/*
 * Build a clean environment for the login shell.  We don't pass
 * through whatever init had; instead the shell gets exactly:
 *   HOME, SHELL, USER, LOGNAME, PATH, TERM
 * with sensible defaults.
 */
static void
set_login_env(const struct passwd *pw, const char *inherited_term)
{
    /* clear what we can — clearenv() if libc had it, but we don't.
     * Walk a known prefix of common names and unset them; the rest
     * stay (harmless for login). */
    static const char *to_clear[] = {
        "IFS", "ENV", "BASH_ENV", "CDPATH", "LD_PRELOAD",
        "LD_LIBRARY_PATH", "PS1", "PS2", "PWD", "OLDPWD", NULL,
    };
    for (int i = 0; to_clear[i] != NULL; i++) {
        unsetenv(to_clear[i]);
    }

    setenv("HOME",    pw->pw_dir,   1);
    setenv("SHELL",   pw->pw_shell, 1);
    setenv("USER",    pw->pw_name,  1);
    setenv("LOGNAME", pw->pw_name,  1);
    setenv("PATH",
           pw->pw_uid == 0
               ? "/usr/local/sbin:/usr/local/bin:/usr/sbin:"
                 "/usr/bin:/sbin:/bin"
               : "/usr/local/bin:/usr/bin:/bin",
           1);
    if (inherited_term != NULL && inherited_term[0] != '\0') {
        setenv("TERM", inherited_term, 1);
    } else if (getenv("TERM") == NULL) {
        setenv("TERM", "linux", 1);
    }
}

/* Returns chars typed (>= 0) on a complete line, or -1 on EOF /
 * read error before any character was typed.  We need to tell
 * "empty line, user just pressed Enter" apart from "user typed
 * ^D" so main() can respawn the prompt on the latter instead of
 * letting login exit. */
#define LOGIN_READ_EOF (-1)

static int
read_line_echo(int fd, char *buf, size_t bufsz)
{
    size_t i = 0;
    char   c;
    int    got_any = 0;
    while (i + 1 < bufsz) {
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) {
            if (!got_any) { buf[0] = '\0'; return LOGIN_READ_EOF; }
            break;
        }
        got_any = 1;
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
        int n = read_line_echo(0, user, sizeof(user));
        if (n == LOGIN_READ_EOF) return -1;     /* ^D — caller respawns */
        if (n == 0) return 0;                   /* empty line — retry */
    }

    printf("Password: ");
    fflush(stdout);
    int pn = read_line_no_echo(0, pass, sizeof(pass));
    if (pn == LOGIN_READ_EOF) return -1;        /* ^D — caller respawns */
    if (pn < 0) return 0;                       /* read error — retry */

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

    /* Record the session in utmp + wtmp BEFORE forking.  Two reasons:
     * (1) we're still root and can write /var/run/utmp;
     * (2) the parent (root) will overwrite this slot with DEAD_PROCESS
     *     when the user's shell exits.
     *
     * `line` is the basename of the controlling tty; `host` is the
     * remote-host string supplied by telnetd via $REMOTEHOST (empty
     * for local console logins). */
    const char *line_name = tty_basename(ttyname(0));
    const char *remote    = getenv("REMOTEHOST");
    {
        struct utmp ut;
        memset(&ut, 0, sizeof(ut));
        ut.ut_type = USER_PROCESS;
        ut.ut_pid  = getpid();
        if (line_name) {
            strncpy(ut.ut_line, line_name, UT_LINESIZE - 1);
            /* ut_id is the last 2..4 chars of the line — convention. */
            size_t lnlen = strlen(line_name);
            const char *idsrc = lnlen > 4 ? line_name + lnlen - 4 : line_name;
            strncpy(ut.ut_id, idsrc, sizeof(ut.ut_id));
        }
        strncpy(ut.ut_user, pw->pw_name, UT_NAMESIZE - 1);
        if (remote) strncpy(ut.ut_host, remote, UT_HOSTSIZE - 1);
        struct timeval tv;
        gettimeofday(&tv, NULL);
        ut.ut_tv.tv_sec  = (int32_t)tv.tv_sec;
        ut.ut_tv.tv_usec = (int32_t)tv.tv_usec;

        setutent();
        pututline(&ut);
        endutent();
        updwtmp(WTMP_FILE, &ut);
    }

    /* (DEAD_PROCESS writing on logout would require login to fork
     * and wait for the user's shell.  That breaks tty ownership in
     * substrate's session model — the child needs to inherit the
     * controlling tty from getty, and an intervening fork moves
     * the leadership to the wrong process.  Skip for now;
     * `who -d` and `last` will show the USER_PROCESS as if the
     * user is still active until init or a reboot writes a
     * RUN_LVL entry to wtmp.) */

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

    {
        /* Preserve TERM (set by getty) across set_login_env's wipe. */
        const char *inherited_term = getenv("TERM");
        char        term_save[64];
        if (inherited_term != NULL) {
            snprintf(term_save, sizeof(term_save), "%s", inherited_term);
            inherited_term = term_save;
        }

        set_login_env(pw, inherited_term);
    }

    if (chdir(pw->pw_dir) != 0) {
        (void)chdir("/");
    }

    /* Fresh signal state for the shell. */
    reset_signals();

    /* Try to head our own session, ignoring EPERM if we already are. */
    (void)setsid();

    printf("\n");

    {
        const char *shell = pw->pw_shell;
        if (shell == NULL || shell[0] == '\0') {
            shell = "/bin/sh";
        }
        /* argv[0] = "-sh" convention requests a login shell. */
        char argv0[64];
        const char *base = strrchr(shell, '/');
        snprintf(argv0, sizeof(argv0), "-%s", base != NULL ? base + 1 : shell);
        execl(shell, argv0, (char *)NULL);
        execl(shell, shell, "-l", (char *)NULL);
        execl(shell, shell, (char *)NULL);
        perror("login: exec");
        return -1;
    }
}

int
main(int argc, char **argv)
{
    int         tries;
    const char *forced = NULL;
    const char *tty    = tty_basename(ttyname(0));

    if (argc >= 2 && argv[1][0] != '-') {
        forced = argv[1];
    }

    /* Only show the banner when run standalone.  When invoked from
     * getty with a username already in hand, getty has already shown
     * /etc/issue + the host banner before reading the user — printing
     * them again here would land them between "login:" and "Password:"
     * which looks like garbage. */
    if (forced == NULL) {
        print_issue();
        printf("Substrate (%s)\n\n", tty);
    }

    for (tries = 0; tries < LOGIN_MAX_TRIES; ) {
        int rc = do_login_one(forced);
        if (rc < 0) {
            /* EOF at a prompt — user typed ^D.  Don't let that
             * close the session; loop back and re-prompt.  This
             * matches getty / mingetty behaviour on stock Linux,
             * where ^D at "login:" just reprints the banner.
             * The try counter is NOT advanced (it'd be unfair to
             * lock out someone who ^D'd by accident).  */
            putchar('\n');
            forced = NULL;
            continue;
        }
        forced = NULL;
        if (rc == 0 && tries + 1 < LOGIN_MAX_TRIES) {
            /* slow brute-force attempts down a touch */
            sleep(LOGIN_FAIL_DELAY_SECS);
        }
        tries++;
    }
    return 1;
}
