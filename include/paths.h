/* <paths.h> — BSD-historical filesystem paths.  Used by mail tools,
 * sshd, getty, login, … */
#ifndef _PATHS_H
#define _PATHS_H

#define _PATH_BSHELL    "/bin/sh"
#define _PATH_CONSOLE   "/dev/console"
#define _PATH_DEFPATH   "/usr/bin:/bin:/usr/sbin:/sbin"
#define _PATH_STDPATH   "/usr/bin:/bin:/usr/sbin:/sbin"
#define _PATH_DEV       "/dev/"
#define _PATH_DEVNULL   "/dev/null"
#define _PATH_TTY       "/dev/tty"
#define _PATH_KMEM      "/dev/kmem"
#define _PATH_MEM       "/dev/mem"
#define _PATH_TMP       "/tmp/"
#define _PATH_VARTMP    "/var/tmp/"
#define _PATH_VARRUN    "/var/run/"
#define _PATH_MAILDIR   "/var/mail"
#define _PATH_NOLOGIN   "/etc/nologin"
#define _PATH_UTMP      "/var/run/utmp"
#define _PATH_UTMPX     _PATH_UTMP
#define _PATH_WTMP      "/var/log/wtmp"
#define _PATH_WTMPX     _PATH_WTMP
#define _PATH_LASTLOG   "/var/log/lastlog"
#define _PATH_RWHODIR   "/var/rwho"
#define _PATH_SHELLS    "/etc/shells"

#endif
