/*
 * halt — bring the system to a stop without powering off.
 *
 * By default we hand off to init: send SIGTERM to PID 1 so init's
 * shutdown_sequence runs (terminate every process, sync, then
 * reboot(RB_HALT_SYSTEM) at the tail) which is the clean path.
 *
 * With -f we skip init entirely and call reboot() ourselves — the
 * emergency path for when init isn't responsive or for single-user
 * mode shutdowns.
 *
 *   halt        — graceful: signal init
 *   halt -f     — force: reboot(RB_HALT_SYSTEM) directly
 *   halt -p     — poweroff variant (same as `poweroff`)
 *   halt -r     — reboot variant (same as `reboot`)
 *
 * Either way requires euid==0; the kernel returns -EPERM otherwise
 * (and kill(1, ...) fails the same way).
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/reboot.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int force = 0;
    int cmd = RB_HALT_SYSTEM;
    int via_init_signal = SIGTERM;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-f") == 0) force = 1;
        else if (strcmp(a, "-p") == 0) { cmd = RB_POWER_OFF;  via_init_signal = SIGUSR1; }
        else if (strcmp(a, "-r") == 0) { cmd = RB_AUTOBOOT;   via_init_signal = SIGUSR2; }
        else {
            fprintf(stderr, "usage: %s [-f] [-p|-r]\n", argv[0]);
            return 2;
        }
    }

    if (!force) {
        if (kill(1, via_init_signal) == 0) return 0;
        fprintf(stderr, "%s: signalling init failed (%s); falling back to direct reboot\n",
                argv[0], strerror(errno));
    }

    if (reboot(cmd) < 0) {
        fprintf(stderr, "%s: reboot: %s\n", argv[0], strerror(errno));
        return 1;
    }
    return 0;
}
