/*
 * torture_sed_inplace_tmp.c -- "sed -i" does not follow a planted symlink.
 *
 * sed -i wrote its output to "<file>.sed<pid>", opened with fopen("w"):
 * anyone able to write the file's directory could guess the name, plant a
 * symlink there, and have sed truncate and overwrite the symlink's target.
 * This test forks, plants that symlink (pointing at a victim file) under
 * the child's pid, then has the child exec "sed -i" on a file in the same
 * directory, and checks that the victim is untouched, the file is edited,
 * and no temporary file is left behind.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define DIR_   "/tmp/sedtmp"
#define FILE_  DIR_ "/data"
#define VICTIM "/tmp/sedtmp-victim"

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

static void put(const char *path, const char *s) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        write(fd, s, strlen(s));
        close(fd);
    }
}

static int has(const char *path, const char *s) {
    char buf[256];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0)
        return 0;
    buf[n] = '\0';
    return strcmp(buf, s) == 0;
}

int main(void) {
    printf("torture_sed_inplace_tmp\n");
    mkdir("/tmp", 01777);
    mkdir(DIR_, 0755);
    put(FILE_, "hello\n");
    put(VICTIM, "precious\n");

    int go[2];
    pipe(go);
    pid_t pid = fork();
    if (pid == 0) {
        char c;
        close(go[1]);
        read(go[0], &c, 1);
        execl("/bin/sed", "sed", "-i", "s/hello/bye/", FILE_, (char *)NULL);
        _exit(99);
    }
    close(go[0]);

    char link[128];
    snprintf(link, sizeof(link), "%s.sed%d", FILE_, (int)pid);
    symlink(VICTIM, link);
    write(go[1], "x", 1);
    close(go[1]);

    int status;
    waitpid(pid, &status, 0);

    check("sed succeeded", WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "sed -i failed");
    check("file edited", has(FILE_, "bye\n"), "file does not hold the edit");
    check("symlink target untouched", has(VICTIM, "precious\n"),
          "sed wrote through the planted symlink");

    unlink(link);
    int left = 0;
    DIR *d = opendir(DIR_);
    struct dirent *de;
    while (d && (de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..") &&
            strcmp(de->d_name, "data")) {
            printf("  leftover: %s\n", de->d_name);
            left++;
        }
    }
    if (d)
        closedir(d);
    check("no temporary left behind", left == 0, "temporary file left");

    unlink(FILE_);
    unlink(VICTIM);
    rmdir(DIR_);
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
