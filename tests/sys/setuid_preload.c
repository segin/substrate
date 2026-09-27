/*
 * setuid_preload.c -- an LD_PRELOAD library that announces itself.
 *
 * torture_setuid preloads it into setuid_helper: the constructor's
 * "PRELOADED" must appear for a plain program and never for a setuid one.
 */
#include <unistd.h>

__attribute__((constructor)) static void announce(void) {
    write(2, "PRELOADED\n", 10);
}
