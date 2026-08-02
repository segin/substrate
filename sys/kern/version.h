#ifndef _SYS_VERSION_H
#define _SYS_VERSION_H

#define OS_NAME "substrate"

/* OS_RELEASE / OS_VERSION / OS_OSRELEASE / OS_VERSION_LONG.  Kept in their own
 * header so a file needing only the version -- kern/sysctl.c -- can have it
 * without the externs below. */
#include <kern/osversion.h>

/* Build target architecture, for the boot banner / version strings.  Detected
 * from the compiler so it tracks whatever target the kernel is built for. */
#if defined(__x86_64__)
#define OS_ARCH "x86_64"
#elif defined(__i386__)
#define OS_ARCH "i386"
#else
#define OS_ARCH "unknown"
#endif

extern int serial_debug_enabled;
#define MAXHOSTNAMELEN 256
extern char kernel_hostname[MAXHOSTNAMELEN];

#endif
