/*
 * prof - display profile data.
 *
 * Substrate has no gprof/mon.out profiling backend, so there is no
 * profile data to display.  Rather than the old stub — which printed a
 * reassuring line and exited 0, masking the absence from any script that
 * checked the exit status — report the situation on stderr and exit
 * non-zero.
 */

#include <stdio.h>

int
main(void)
{
    fprintf(stderr, "prof: profiling is not supported on this system\n");
    return 1;
}
