/*
 * torture_sdm_stop.c -- sdm stops at once on SIGTERM and takes X with it.
 *
 * sdm ran the greeter (and so the session) in the foreground; a shell runs
 * a trap only after the foreground command ends, so "rc.d/60-sdm stop"
 * did nothing until the user logged out.  This test replaces Xfbdev and
 * sgreet with stubs that just sleep (it runs on a throwaway image), starts
 * /usr/sbin/sdm, sends it SIGTERM and expects sdm to exit within a second
 * and neither stub to be left running.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

static void stub(const char *path, const char *tag) {
    char body[128];
    int n = snprintf(body, sizeof(body), "#!/bin/sh\n# %s stub\nexec sleep 1000\n", tag);
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
    write(fd, body, (size_t)n);
    close(fd);
    chmod(path, 0755);
}

/* How many "sleep 1000" processes are running? */
static int sleepers(void) {
    int n = 0;
    DIR *d = opendir("/proc");
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        char path[300], buf[128];
        if (e->d_name[0] < '0' || e->d_name[0] > '9')
            continue;
        snprintf(path, sizeof(path), "/proc/%s/cmdline", e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        ssize_t r = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (r <= 0)
            continue;
        buf[r] = '\0';
        const char *arg1 = buf + strlen(buf) + 1;    /* argv[1] */
        if (strstr(buf, "sleep") && arg1 < buf + r && strcmp(arg1, "1000") == 0)
            n++;
    }
    if (d)
        closedir(d);
    return n;
}

int main(void) {
    printf("torture_sdm_stop\n");
    mkdir("/tmp", 01777);
    stub("/usr/bin/Xfbdev", "Xfbdev");
    stub("/usr/sbin/sgreet", "sgreet");

    pid_t sdm = fork();
    if (sdm == 0) {
        execl("/usr/sbin/sdm", "sdm", (char *)NULL);
        _exit(127);
    }
    sleep(2);
    int before = sleepers();

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    kill(sdm, SIGTERM);
    int status, gone = 0;
    for (int i = 0; i < 30 && !gone; i++) {
        if (waitpid(sdm, &status, WNOHANG) == sdm)
            gone = 1;
        else
            usleep(100000);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    long ms = (t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000;
    usleep(300000);
    int after = sleepers();

    char why[64];
    printf("  stubs running: %d before, %d after; sdm %s after %ld ms\n",
           before, after, gone ? "exited" : "still running", ms);
    check("both stubs were running", before == 2, "sdm did not start them");
    snprintf(why, sizeof(why), "%s after %ld ms", gone ? "exited" : "running", ms);
    check("sdm exits within a second of SIGTERM", gone && ms <= 1000, why);
    check("the X server and greeter are stopped", after == 0, "stubs left running");

    if (!gone) {
        kill(sdm, SIGKILL);
        waitpid(sdm, NULL, 0);
    }
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
