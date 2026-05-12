#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <termios.h>

#ifndef _STRUCT_WINSIZE_DEFINED
#define _STRUCT_WINSIZE_DEFINED
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#endif

/* TTY ioctls */
#define TIOCGWINSZ   0x5413
#define TIOCSWINSZ   0x5414
#define TIOCGPGRP    0x540F
#define TIOCSPGRP    0x5410
#define TCGETS       0x5401

/* Unix98 PTY multiplexer ioctls. */
#define TIOCGPTN     0x80045430U
#define TIOCSPTLCK   0x40045431U
#define TIOCGPKT     0x80045438U
#define TIOCSIG      0x40045436U
#define TIOCPKT      0x5420

#define TIOCPKT_DATA          0x00
#define TIOCPKT_FLUSHREAD     0x01
#define TIOCPKT_FLUSHWRITE    0x02
#define TIOCPKT_STOP          0x04
#define TIOCPKT_START         0x08
#define TIOCPKT_NOSTOP        0x10
#define TIOCPKT_DOSTOP        0x20
#define TIOCPKT_IOCTL         0x40

int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif
