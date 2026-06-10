#ifndef _TERMIOS_H
#define _TERMIOS_H

#include <sys/types.h>
#include <stdint.h>

typedef uint32_t tcflag_t;
typedef uint8_t  cc_t;
typedef uint32_t speed_t;

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
// c_oflag output-delay masks (POSIX TABDLY etc.)
#define NLDLY   0000400
#define   NL0   0000000
#define   NL1   0000400
#define CRDLY   0003000
#define   CR0   0000000
#define   CR1   0001000
#define   CR2   0002000
#define   CR3   0003000
#define TABDLY  0014000
#define   TAB0  0000000
#define   TAB1  0004000
#define   TAB2  0010000
#define   TAB3  0014000
#define   XTABS 0014000
#define BSDLY   0020000
#define   BS0   0000000
#define   BS1   0020000
#define VTDLY   0040000
#define   VT0   0000000
#define   VT1   0040000
#define FFDLY   0100000
#define   FF0   0000000
#define   FF1   0100000

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
#define TIOCNOTTY   0x5422
#define TIOCOUTQ    0x5411
#define TIOCSTI     0x5412
#define TIOCGWINSZ  0x5413
#define TIOCSWINSZ  0x5414
#define TIOCMGET    0x5415
#define TIOCMBIS    0x5416
#define TIOCMBIC    0x5417
#define TIOCMSET    0x5418
#define TIOCCONS    0x541D  /* redirect kernel console output to this tty */
/* Modem line bits for TIOCMGET/TIOCMSET/TIOCMBIS/TIOCMBIC (mirror of the
 * kernel's <sys/termios.h>; the userspace header had omitted them). */
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
#define TIOCGSOFTCAR 0x5419
#define TIOCSSOFTCAR 0x541A
#define FIONREAD    0x541B
#define TIOCINQ     FIONREAD

#ifndef _STRUCT_WINSIZE_DEFINED
#define _STRUCT_WINSIZE_DEFINED
struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};
#endif

#define TCSANOW   0
#define TCSADRAIN  1
#define TCSAFLUSH  2

/* tcflush queue selectors */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

/* tcflow actions */
#define TCOOFF    0
#define TCOON     1
#define TCIOFF    2
#define TCION     3

/* Line speeds — values are the POSIX-compatible "encoded baud index"
 * stored in c_cflag's CBAUD field on Linux-style termios.  Substrate
 * carries them in c_ispeed / c_ospeed (the BSD style); the constants
 * are kept for source-compat with code that does the encode/decode
 * dance via cfgetispeed/cfsetispeed. */
#define B0       0000000
#define B50      0000001
#define B75      0000002
#define B110     0000003
#define B134     0000004
#define B150     0000005
#define B200     0000006
#define B300     0000007
#define B600     0000010
#define B1200    0000011
#define B1800    0000012
#define B2400    0000013
#define B4800    0000014
#define B9600    0000015
#define B19200   0000016
#define B38400   0000017
#define B57600   0010001
#define B115200  0010002
#define B230400  0010003

/* CBAUD/CBAUDEX: the c_cflag bit field that, on Linux-style termios, holds
 * the encoded baud index above.  CBAUDEX (0010000) distinguishes the
 * extended (>=57600) speeds; CBAUD masks the whole field.  Ported code
 * (CDE's DtTerm) clears it directly: tio.c_cflag &= ~CBAUD. */
#define CBAUDEX  0010000
#define CBAUD    0010017

#include <sys/types.h>

int tcgetattr(int fd, struct termios *termios_p);
int tcsetattr(int fd, int optional_actions, const struct termios *termios_p);

speed_t cfgetispeed(const struct termios *termios_p);
speed_t cfgetospeed(const struct termios *termios_p);
int     cfsetispeed(struct termios *termios_p, speed_t speed);
int     cfsetospeed(struct termios *termios_p, speed_t speed);
void    cfmakeraw(struct termios *termios_p);

int     tcdrain(int fd);
int     tcflow(int fd, int action);
int     tcflush(int fd, int queue);
pid_t   tcgetsid(int fd);
int     tcsendbreak(int fd, int duration);

#define TIOCGSID 0x5429   /* substrate ioctl number, mirrors Linux */

#endif
