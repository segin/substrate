/*
 * torture_passwd_user.c -- an ordinary user can change their own password.
 *
 * /bin/passwd was installed without the setuid bit, so for anyone but root
 * it could not rewrite /etc/shadow.  This test adds an account with a known
 * password and, as that user, runs /bin/passwd on pipes:
 *
 *   own       with the right current password the user's password changes;
 *   wrongcur  with a wrong current password nothing changes;
 *   other     "passwd root" is refused and root's entry is unchanged;
 *   owner     /etc/shadow keeps its owner, group and mode afterwards, and
 *             no temporary file is left behind.
 *
 * Runs as init (root); /etc/passwd and /etc/shadow are restored afterwards.
 * Prints a "Result:" line.
 */
#include <crypt.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define TEST_USER "pwtest"
#define TEST_UID  4243
#define TEST_GID  4243

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    char *buf = malloc(65536);
    *len = buf ? fread(buf, 1, 65535, f) : 0;
    if (buf)
        buf[*len] = '\0';
    fclose(f);
    return buf;
}

static int spit(const char *path, const char *data, size_t len, const char *extra) {
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fwrite(data, 1, len, f);
    if (extra)
        fputs(extra, f);
    return fclose(f);
}

/* The password field of `user` in /etc/shadow, in a static buffer. */
static const char *stored(const char *user) {
    static char field[256];
    size_t len, ulen = strlen(user);
    char *buf = slurp("/etc/shadow", &len);
    field[0] = '\0';
    for (char *l = buf; l && *l; ) {
        char *nl = strchr(l, '\n');
        if (strncmp(l, user, ulen) == 0 && l[ulen] == ':') {
            char *f = l + ulen + 1, *c = strchr(f, ':');
            size_t n = c ? (size_t)(c - f) : strlen(f);
            if (n >= sizeof(field))
                n = sizeof(field) - 1;
            memcpy(field, f, n);
            field[n] = '\0';
            break;
        }
        l = nl ? nl + 1 : NULL;
    }
    free(buf);
    return field;
}

static int verifies(const char *user, const char *pw) {
    const char *s = stored(user);
    char *h = *s ? crypt(pw, s) : NULL;
    return h && strcmp(h, s) == 0;
}

/* Run /bin/passwd [arg] as the test user with `input` on stdin. */
static int run_passwd(const char *arg, const char *input) {
    int in[2];
    pipe(in);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in[0], 0);
        close(in[0]);
        close(in[1]);
        if (setgid(TEST_GID) != 0 || setuid(TEST_UID) != 0)
            _exit(98);
        if (arg)
            execl("/bin/passwd", "passwd", arg, (char *)NULL);
        else
            execl("/bin/passwd", "passwd", (char *)NULL);
        _exit(99);
    }
    close(in[0]);
    write(in[1], input, strlen(input));
    close(in[1]);
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int main(void) {
    printf("torture_passwd_user\n");

    size_t plen = 0, slen = 0;
    char *passwd = slurp("/etc/passwd", &plen);
    char *shadow = slurp("/etc/shadow", &slen);
    struct stat before;
    char line[256];
    if (!passwd || !shadow || stat("/etc/shadow", &before) != 0) {
        printf("setup failed: %s\nResult: FAILED\n", strerror(errno));
        return 1;
    }
    snprintf(line, sizeof(line), TEST_USER ":%s:0:0:99999:7:::\n",
             crypt("oldpass", "$5$saltsalt"));
    if (spit("/etc/passwd", passwd, plen,
             TEST_USER ":x:4243:4243:passwd test:/:/bin/sh\n") != 0 ||
        spit("/etc/shadow", shadow, slen, line) != 0) {
        printf("setup failed: %s\nResult: FAILED\n", strerror(errno));
        return 1;
    }
    chmod("/etc/shadow", before.st_mode & 07777);
    chown("/etc/shadow", before.st_uid, before.st_gid);

    char root_before[256];
    snprintf(root_before, sizeof(root_before), "%s", stored("root"));

    int rc = run_passwd(NULL, "wrongpass\nnewpass1\nnewpass1\n");
    check("wrongcur: refused", rc != 0, "passwd accepted a wrong current password");
    check("wrongcur: password unchanged", verifies(TEST_USER, "oldpass"),
          "password changed");

    rc = run_passwd(NULL, "oldpass\nnewpass1\nnewpass1\n");
    check("own: passwd succeeded", rc == 0, "passwd failed");
    check("own: new password set", verifies(TEST_USER, "newpass1"),
          "new password does not verify");

    rc = run_passwd("root", "newpass1\nhacked\nhacked\n");
    check("other: refused", rc != 0, "passwd root succeeded");
    check("other: root unchanged", strcmp(stored("root"), root_before) == 0,
          "root's entry changed");

    struct stat after;
    int ok = stat("/etc/shadow", &after) == 0;
    check("owner: shadow owner, group and mode kept",
          ok && after.st_uid == before.st_uid && after.st_gid == before.st_gid &&
          (after.st_mode & 07777) == (before.st_mode & 07777),
          "ownership or mode changed");
    check("owner: no temporary left", access("/etc/shadow.new", F_OK) != 0,
          "/etc/shadow.new left behind");

    spit("/etc/passwd", passwd, plen, NULL);
    spit("/etc/shadow", shadow, slen, NULL);
    chmod("/etc/shadow", before.st_mode & 07777);
    chown("/etc/shadow", before.st_uid, before.st_gid);

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
