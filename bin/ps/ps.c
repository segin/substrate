#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/sysinfo.h>
#include <unistd.h>

static void print_time(uint32_t ticks) {
    uint32_t seconds = ticks / 100;
    uint32_t h = seconds / 3600;
    uint32_t m = (seconds % 3600) / 60;
    uint32_t s = seconds % 60;
    printf("%02d:%02d:%02d", h, m, s);
}

static const char *state_to_char(uint8_t state) {
    switch (state) {
    case 1: return "IDL";
    case 2: return "RUN";
    case 3: return "SLP";
    case 4: return "STP";
    case 5: return "ZOM";
    case 6: return "DIE";
    default: return "???";
    }
}

int main(int argc, char *argv[]) {
    int show_bitness = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 || strcmp(argv[i], "--bitness") == 0)
            show_bitness = 1;
    }

    /* Query process count first */
    int count = sys_proc_list(NULL, 0);
    if (count <= 0) {
        if (count < 0)
            perror("sys_proc_list");
        return count < 0 ? 1 : 0;
    }

    pid_t *pids = malloc(count * sizeof(pid_t));
    if (!pids) {
        perror("malloc");
        return 1;
    }

    count = sys_proc_list(pids, count);
    if (count < 0) {
        perror("sys_proc_list");
        free(pids);
        return 1;
    }

    if (show_bitness)
        printf("%5s %5s %5s %5s %4s %-4s %9s %s\n",
               "PID", "PPID", "UID", "GID", "BITS", "STAT", "TIME", "CMD");
    else
        printf("%5s %5s %5s %5s %-4s %9s %s\n",
               "PID", "PPID", "UID", "GID", "STAT", "TIME", "CMD");

    for (int i = 0; i < count; i++) {
        sys_procinfo_t info;
        if (sys_proc_info(pids[i], &info) != 0)
            continue;

        uint32_t total_time = info.user_time + info.sys_time;

        if (show_bitness)
            printf("%5d %5d %5d %5d %4d %-4s ",
                   info.pid, info.ppid, info.uid, info.gid,
                   info.bitness, state_to_char(info.state));
        else
            printf("%5d %5d %5d %5d %-4s ",
                   info.pid, info.ppid, info.uid, info.gid,
                   state_to_char(info.state));

        print_time(total_time);
        printf(" %s\n", info.name);
    }

    free(pids);
    return 0;
}