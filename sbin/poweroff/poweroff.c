/*
 * poweroff — bring the system to a stop and turn the machine off
 * (when supported by hardware; today's substrate kernel falls back
 * to halt-equivalent until ACPI/APM lands).
 *
 *   poweroff       — graceful: SIGUSR1 to init
 *   poweroff -f    — force: reboot(RB_POWER_OFF) directly
 *
 * Requires euid==0.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int force = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-f") == 0) force = 1;
        else {
            fprintf(stderr, "usage: %s [-f]\n", argv[0]);
            return 2;
        }
    }

    if (!force) {
        if (kill(1, SIGUSR1) == 0) return 0;
        fprintf(stderr, "%s: signalling init failed (%s); falling back to direct reboot\n",
                argv[0], strerror(errno));
    }

    if (reboot(RB_POWER_OFF) < 0) {
        fprintf(stderr, "%s: reboot: %s\n", argv[0], strerror(errno));
        return 1;
    }
    return 0;
}
