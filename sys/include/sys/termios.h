#ifndef _SYS_TERMIOS_H
#define _SYS_TERMIOS_H

#include <stdint.h>

typedef uint32_t tcflag_t;
typedef uint8_t  cc_t;
typedef uint32_t speed_t;

#define B0 0
#define B50 50
#define B75 75
#define B110 110
#define B134 134
#define B150 150
#define B200 200
#define B300 300
#define B600 600
#define B1200 1200
#define B1800 1800
#define B2400 2400
#define B4800 4800
#define B9600 9600
#define B19200 19200
#define B38400 38400
#define B57600 57600
#define B115200 115200
#define B230400 230400
#define B460800 460800
#define B921600 921600

/* Native Substrate termios uses NCCS=32. Foreign personalities (Linux, FreeBSD)
 * translate to their own termios format in their personality ioctl handlers. */
#define NCCS 32

#ifndef _STRUCT_TERMIOS_DEFINED
#define _STRUCT_TERMIOS_DEFINED
struct termios {
    tcflag_t c_iflag; // Input flags
    tcflag_t c_oflag; // Output flags
    tcflag_t c_cflag; // Control flags
    tcflag_t c_lflag; // Local flags
    cc_t     c_line;  // Line discipline
    cc_t     c_cc[NCCS]; // Control characters
    speed_t  c_ispeed; // Input speed
    speed_t  c_ospeed; // Output speed
};
#endif

// c_cc indices
#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSWTC    7
#define VSTART   8
#define VSTOP    9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

// c_iflag
#define IGNBRK  0000001
#define BRKINT  0000002
#define IGNPAR  0000004
#define PARMRK  0000010
#define INPCK   0000020
#define ISTRIP  0000040
#define INLCR   0000100
#define IGNCR   0000200
#define ICRNL   0000400
#define IUCLC   0001000
#define IXON    0002000
#define IXANY   0004000
#define IXOFF   0010000
#define IMAXBEL 0020000
#define IUTF8   0040000

// c_oflag
#define OPOST   0000001
#define OLCUC   0000002
#define ONLCR   0000004
#define OCRNL   0000010
#define ONOCR   0000020
#define ONLRET  0000040
#define OFILL   0000100
#define OFDEL   0000200
#define OXTABS  0006000

// c_cflag
#define CSIZE   0000060
#define CS5     0000000
#define CS6     0000020
#define CS7     0000040
#define CS8     0000060
#define CSTOPB  0000100
#define CREAD   0000200
#define PARENB  0000400
#define PARODD  0001000
#define HUPCL   0002000
#define CLOCAL  0004000
#define CRTSCTS 020000000000		/* flow control */
// c_lflag
#define ISIG    0000001
#define ICANON  0000002
#define ECHO    0000010
#define ECHOE   0000020
#define ECHOK   0000040
#define ECHONL  0000100
#define NOFLSH  0000200
#define TOSTOP  0000400
#define ECHOCTL 0001000
#define ECHOPRT 0002000
#define ECHOKE  0004000
#define FLUSHO  0010000
#define PENDIN  0040000
#define IEXTEN  0100000

// ioctls
#define TCGETS      0x5401
#define TCSETS      0x5402
#define TCSETSW     0x5403
#define TCSETSF     0x5404
#define TCGETA      0x5405
#define TCSETA      0x5406
#define TCSETAW     0x5407
#define TCSETAF     0x5408
#define TCSBRK      0x5409
#define TCXONC      0x540A
#define TCFLSH      0x540B
#define TIOCEXCL    0x540C
#define TIOCNXCL    0x540D
#define TIOCSCTTY   0x540E
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410
#define TIOCOUTQ    0x5411
#define TIOCSTI     0x5412
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define TIOCMGET    0x5415
#define TIOCMBIS    0x5416
#define TIOCMBIC    0x5417
#define TIOCMSET    0x5418
#define TIOCGSOFTCAR 0x5419
#define TIOCSSOFTCAR 0x541A
#define FIONREAD    0x541B
#define TIOCINQ     FIONREAD
#define TIOCNOTTY   0x5422
#define TIOCCONS    0x541D  /* redirect kernel console output to this tty */
#define TIOCSBRK    0x5427  /* Set break */
#define TIOCCBRK    0x5428  /* Clear break */

/* Modem line bits for TIOCMGET/TIOCMSET/TIOCMBIS/TIOCMBIC */
#define TIOCM_LE    0x001   /* Line Enable (DSR) */
#define TIOCM_DTR   0x002
#define TIOCM_RTS   0x004
#define TIOCM_ST    0x008   /* Secondary Transmit */
#define TIOCM_SR    0x010   /* Secondary Receive */
#define TIOCM_CTS   0x020
#define TIOCM_CAR   0x040   /* DCD */
#define TIOCM_CD    TIOCM_CAR
#define TIOCM_RNG   0x080   /* Ring */
#define TIOCM_RI    TIOCM_RNG
#define TIOCM_DSR   0x100

#ifndef _STRUCT_WINSIZE_DEFINED
#define _STRUCT_WINSIZE_DEFINED
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#endif

/*
 * POSIX termios(3) attribute-control constants and prototypes.
 *
 * These are user-space API; the kernel implements them via the
 * underlying TCGETS / TCSETS{,W,F} ioctls.  They live in this
 * header rather than the userspace-only one because the kernel
 * Makefile search order puts sys/include before include/, so a
 * userspace component built with the kernel CFLAGS would
 * otherwise miss the prototypes.
 */
#define TCSANOW    0    /* change attributes immediately */
#define TCSADRAIN  1    /* change after pending output has drained */
#define TCSAFLUSH  2    /* drain output, flush input, then change */

/* tcflush(3) queue selectors. */
#ifndef TCIFLUSH
#define TCIFLUSH   0
#define TCOFLUSH   1
#define TCIOFLUSH  2
#endif

/* tcflow(3) actions. */
#ifndef TCOOFF
#define TCOOFF     0
#define TCOON      1
#define TCIOFF     2
#define TCION      3
#endif

#ifndef _KERNEL
#include <sys/types.h>      /* for pid_t */
int   tcgetattr(int fd, struct termios *termios_p);
int   tcsetattr(int fd, int optional_actions, const struct termios *termios_p);
int   tcsendbreak(int fd, int duration);
int   tcdrain(int fd);
int   tcflush(int fd, int queue_selector);
int   tcflow(int fd, int action);
pid_t tcgetsid(int fd);
void  cfmakeraw(struct termios *termios_p);
speed_t cfgetispeed(const struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);
int   cfsetispeed(struct termios *termios_p, speed_t speed);
int   cfsetospeed(struct termios *termios_p, speed_t speed);
int   cfsetspeed(struct termios *termios_p, speed_t speed);
#endif

#endif
