#ifndef _SYS_OSVERSION_H
#define _SYS_OSVERSION_H

/*
 * The kernel version, in one place.  Edit OS_RELEASE; everything else derives.
 *
 * It used to be four independent literals -- OS_VERSION in kern/version.h,
 * kernel_osrelease and kernel_version in kern/sysctl.c, and uname()'s release
 * in kern/syscall.c -- none of which referenced the others, so a bump meant
 * finding all four and getting them consistent by hand.  getty had a fifth
 * copy and missed two bumps: /etc/issue advertised 0.1 while uname(2) reported
 * 0.2.  getty now asks uname(2), and these four agree by construction.
 *
 * Three shapes exist because three consumers want different things: uname(2)
 * wants the bare release, sysctl kern.osrelease carries the stability tag, and
 * the boot banner prints x.y.z.
 *
 * This lives apart from kern/version.h -- which also declares kernel_hostname
 * and serial_debug_enabled -- so that a file wanting only the version does not
 * have to pull in unrelated externs.  sysctl.c is exactly that file: it keeps
 * its own static hostname buffer, which collides with version.h's extern.
 */
#define OS_RELEASE      "0.3"                     /* uname -r */
#define OS_RELEASE_TAG  "-ALPHA"                  /* stability suffix */
#define OS_OSRELEASE    OS_RELEASE OS_RELEASE_TAG /* sysctl kern.osrelease */
#define OS_VERSION      OS_RELEASE ".0"           /* boot banner, x.y.z */

/* Build identity for sysctl kern.version.  The stamp is still hand-written --
 * nothing date-stamps the kernel at build time yet. */
#define OS_BUILD_STAMP  "#0: Fri Jun 26 00:00:00 UTC 2026"
#define OS_VERSION_LONG "Substrate " OS_OSRELEASE " (GENERIC) " OS_BUILD_STAMP

#endif
