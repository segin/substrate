#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

static const char *
get_chmod_bin(void)
{
    const char *bin = getenv("CHMOD_BIN");

    if (bin == NULL || bin[0] == '\0') {
        return "../../bin/chmod/chmod";
    }
    return bin;
}

static void
die(const char *msg)
{
    perror(msg);
    exit(1);
}

static void
check_mode(const char *path, mode_t expected)
{
    struct stat st;

    if (stat(path, &st) != 0) {
        die("stat");
    }

    if ((st.st_mode & 07777) != (expected & 07777)) {
        fprintf(stderr, "mode mismatch for %s: got=%#o expected=%#o\n", path,
            (unsigned)(st.st_mode & 07777), (unsigned)(expected & 07777));
        exit(1);
    }
}

static void
create_file(const char *path)
{
    int fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);

    if (fd < 0) {
        die("open");
    }
    if (write(fd, "x", 1) != 1) {
        close(fd);
        die("write");
    }
    if (close(fd) != 0) {
        die("close");
    }
}

static int
run_chmod(char *const args[])
{
    pid_t pid = fork();
    int status;

    if (pid < 0) {
        die("fork");
    }
    if (pid == 0) {
        execv(args[0], args);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        die("waitpid");
    }
    if (!WIFEXITED(status)) {
        fprintf(stderr, "child did not exit normally\n");
        exit(1);
    }
    return WEXITSTATUS(status);
}

static void
join_path(char *out, size_t out_len, const char *a, const char *b)
{
    if (snprintf(out, out_len, "%s/%s", a, b) >= (int)out_len) {
        fprintf(stderr, "path too long\n");
        exit(1);
    }
}

int
main(void)
{
    const char *chmod_bin = get_chmod_bin();
    char tmp_template[] = "/tmp/chmod-unit-XXXXXX";
    char *tmp;

    char ref[PATH_MAX];
    char tgt[PATH_MAX];
    char file[PATH_MAX];
    char dir[PATH_MAX];
    char target[PATH_MAX];
    char linkp[PATH_MAX];

    int rc;

    tmp = mkdtemp(tmp_template);
    if (tmp == NULL) {
        die("mkdtemp");
    }

    join_path(dir, sizeof(dir), tmp, "dir");
    join_path(file, sizeof(file), tmp, "file");
    join_path(ref, sizeof(ref), tmp, "ref");
    join_path(tgt, sizeof(tgt), tmp, "target");
    join_path(target, sizeof(target), tmp, "symlink_target");
    join_path(linkp, sizeof(linkp), tmp, "link");

    if (mkdir(dir, 0755) != 0) {
        die("mkdir");
    }

    create_file(file);
    if (chmod(dir, 0755) != 0 || chmod(file, 0644) != 0) {
        die("chmod setup");
    }

    {
        char *const argv_d[] = {
            (char *)chmod_bin,
            (char *)"-d",
            (char *)"700",
            dir,
            file,
            NULL
        };
        rc = run_chmod(argv_d);
        if (rc != 0) {
            fprintf(stderr, "-d invocation failed unexpectedly\n");
            return 1;
        }
        check_mode(dir, 0700);
        check_mode(file, 0644);
    }

    create_file(ref);
    create_file(tgt);
    if (chmod(ref, 0612) != 0 || chmod(tgt, 0644) != 0) {
        die("chmod setup reference");
    }

    {
        char ref_arg[PATH_MAX + 32];
        char *const argv_ref[] = {
            (char *)chmod_bin,
            ref_arg,
            tgt,
            NULL
        };

        if (snprintf(ref_arg, sizeof(ref_arg), "--reference=%s", ref) >=
            (int)sizeof(ref_arg)) {
            fprintf(stderr, "reference arg too long\n");
            return 1;
        }

        rc = run_chmod(argv_ref);
        if (rc != 0) {
            fprintf(stderr, "--reference invocation failed unexpectedly\n");
            return 1;
        }
        check_mode(tgt, 0612);
    }

    create_file(target);
    if (chmod(target, 0644) != 0) {
        die("chmod setup symlink target");
    }
    if (symlink("symlink_target", linkp) != 0) {
        die("symlink");
    }

    {
        char *const argv_h[] = {
            (char *)chmod_bin,
            (char *)"-h",
            (char *)"000",
            linkp,
            NULL
        };

        rc = run_chmod(argv_h);
        check_mode(target, 0644);
        if (rc != 0 && rc != 1) {
            fprintf(stderr, "unexpected -h exit code: %d\n", rc);
            return 1;
        }
    }

    printf("unit_cli: ok\n");
    return 0;
}
