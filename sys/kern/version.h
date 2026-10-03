#ifndef _SYS_VERSION_H
#define _SYS_VERSION_H

#define OS_NAME "substrate"

/* OS_RELEASE / OS_VERSION / OS_OSRELEASE / kernel_version_long.  Kept in their own
 * header so a file needing only the version -- kern/sysctl.c -- can have it
 * without the externs below. */
#include <kern/osversion.h>


extern int serial_debug_enabled;
#define MAXHOSTNAMELEN 256
extern char kernel_hostname[MAXHOSTNAMELEN];

#endif
