#ifndef _SYS_VTIO_H
#define _SYS_VTIO_H

/*
 * VGA text-console specific ioctls used by the VT tty backend.
 * Arguments are pointers to int.
 */
#define VTIOCGTABW     0x56E0
#define VTIOCSTABW     0x56E1
#define VTIOCGCURSOR   0x56E2
#define VTIOCSCURSOR   0x56E3
#define VTIOCGBLINK    0x56E4
#define VTIOCSBLINK    0x56E5
#define VTIOCGCURBLINK 0x56E6
#define VTIOCSCURBLINK 0x56E7

/*
 * Linux-compatible KD ioctls.  X servers (kdrive Xfbdev, Xorg DDX)
 * use KDSETMODE / KDGETMODE to claim the framebuffer: setting
 * KD_GRAPHICS tells the kernel "stop rendering text and the status
 * bar on this VT; the userland is going to draw pixels directly."
 * KD_TEXT puts the VT back into normal kernel-console mode and
 * triggers a redraw of the active VT's contents.
 *
 * KDSKBMODE controls keyboard processing: K_XLATE feeds cooked
 * characters into the TTY line discipline (default), K_RAW (or
 * any non-XLATE value here) suppresses that so keystrokes only
 * reach userland via /dev/input/event0.  Without this, typing
 * into an X session also fills the underlying VT's TTY buffer.
 *
 * Argument is a pointer-to-int (KDGETMODE / KDGKBMODE: out;
 * KDSETMODE / KDSKBMODE: in).
 */
#define KDGETMODE      0x4B3B
#define KDSETMODE      0x4B3A
#define KDGKBMODE      0x4B44
#define KDSKBMODE      0x4B45

#define KD_TEXT        0
#define KD_GRAPHICS    1

#define K_RAW          0
#define K_XLATE        1
#define K_MEDIUMRAW    2
#define K_UNICODE      3
#define K_OFF          4

#endif
