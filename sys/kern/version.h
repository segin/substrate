#ifndef _SYS_VERSION_H
#define _SYS_VERSION_H

#define OS_NAME "substrate"
#define OS_VERSION "0.3.0"

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
