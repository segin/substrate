/*
 * rc_ttysprobe.c -- init starts gettys on the lines /etc/ttys enables.
 *
 * init's terminal lines were compiled in (tty1-tty3), so the tty1 getty
 * could not be turned off for sdm, although rc.d/60-sdm said to.  Boot the
 * real init with this probe at /etc/rc.d/03-ttysprobe and an /etc/ttys
 * that has tty1 off and tty2 and tty3 on.  rc runs the probe before init
 * starts any getty, so it forks, waits, then lists the running gettys from
 * /proc/<pid>/cmdline and expects exactly tty2 and tty3.  Prints a
 * "Result:" line.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int seen[4], other;

/* Tally the running gettys by line; returns how many there are. */
static int scan(int verbose) {
    int total = 0;
    memset(seen, 0, sizeof(seen));
    other = 0;
    DIR *d = opendir("/proc");
    struct dirent *e;
    while (d && (e = readdir(d)) != NULL) {
        if (e->d_name[0] < '0' || e->d_name[0] > '9')
            continue;
        char path[300], buf[256];
        snprintf(path, sizeof(path), "/proc/%s/cmdline", e->d_name);
        int fd = open(path, O_RDONLY);
        if (fd < 0)
            continue;
        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0)
            continue;
        buf[n] = '\0';
        if (!strstr(buf, "getty"))
            continue;
        const char *arg = buf + strlen(buf) + 1;   /* argv[1]: the tty */
        if (arg >= buf + n)
            continue;
        if (verbose)
            printf("rc_ttysprobe: getty on %s\n", arg);
        if (strcmp(arg, "/dev/tty1") == 0) seen[1]++;
        else if (strcmp(arg, "/dev/tty2") == 0) seen[2]++;
        else if (strcmp(arg, "/dev/tty3") == 0) seen[3]++;
        else other++;
        total++;
    }
    if (d)
        closedir(d);
    return total;
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "start") != 0)
        return 0;
    if (fork() != 0)
        return 0;               /* let rc and init carry on */

    /* init starts the gettys once all of rc has run: wait for the first,
     * then give it time to start the rest. */
    for (int i = 0; i < 90 && scan(0) == 0; i++)
        sleep(1);
    sleep(3);
    scan(1);

    int ok = seen[1] == 0 && seen[2] == 1 && seen[3] == 1 && other == 0;
    printf("rc_ttysprobe: tty1 %s, tty2 %s, tty3 %s\n",
           seen[1] ? "getty" : "none", seen[2] ? "getty" : "none",
           seen[3] ? "getty" : "none");
    printf("Result: %s\n", ok ? "PASSED" : "FAILED");
    fflush(stdout);
    return 0;
}
