/*
 * tc - traffic control.
 *
 * Substrate has no qdisc/netlink traffic-control subsystem, so this
 * command cannot configure anything.  The old stub printed a banner and
 * exited 0, which made scripts believe a shaping/queueing rule had been
 * installed.  Report unsupported on stderr and exit non-zero instead.
 */

#include <stdio.h>

int
main(void)
{
    fprintf(stderr, "tc: traffic control is not supported on this system\n");
    return 1;
}
