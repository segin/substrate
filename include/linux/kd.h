/*
 * <linux/kd.h> — KD ioctl compat shim.
 *
 * Substrate exposes the KD ioctl surface (KDSETMODE / KDGETMODE /
 * KDSKBMODE / KDGKBMODE + KD_TEXT / KD_GRAPHICS / K_RAW / K_XLATE /
 * K_MEDIUMRAW / K_UNICODE / K_OFF) in <sys/vtio.h>; provide them
 * here under the Linux-canonical path too.
 *
 * KDKBDREP / KDSETLED / KDSKBLED / KDSETRAD etc are accepted as
 * ioctls but currently no-op at runtime — kdrive's linux backend
 * calls them on startup but tolerates the EINVAL/ENOTTY return.
 */
#ifndef _LINUX_KD_H
#define _LINUX_KD_H

#include <sys/vtio.h>

#define GIO_FONT        0x4B60
#define PIO_FONT        0x4B61
#define GIO_FONTX       0x4B6B
#define PIO_FONTX       0x4B6C
#define PIO_FONTRESET   0x4B6D
#define GIO_CMAP        0x4B70
#define PIO_CMAP        0x4B71

#define KIOCSOUND       0x4B2F  /* start sound, arg = freq */
#define KDMKTONE        0x4B30  /* generate tone */

#define KDGKBLED        0x4B64
#define KDSKBLED        0x4B65
#define KDGETLED        0x4B31
#define KDSETLED        0x4B32

#define K_SCROLLLOCK    0x01
#define K_NUMLOCK       0x02
#define K_CAPSLOCK      0x04

#define KDSIGACCEPT     0x4B4E
#define KDSETRAD        0x4B47  /* set keyboard rate / delay */
#define KDKBDREP        0x4B52  /* set/get rep delay */

#define KDGKBTYPE       0x4B33
#define KB_84           0x01
#define KB_101          0x02
#define KB_OTHER        0x03

#define KDADDIO         0x4B34  /* add i/o port */
#define KDDELIO         0x4B35  /* del i/o port */
#define KDENABIO        0x4B36
#define KDDISABIO       0x4B37

/* Keymap manipulation — kdrive uses KDGKBENT to read the in-kernel
 * console keymap and translate to X keysyms.  substrate's kernel
 * keymap isn't exposed through this surface; ioctl returns ENOTTY
 * and the caller falls back to its built-in table. */
#define KDGKBENT        0x4B46
#define KDSKBENT        0x4B47
#define KDGKBSENT       0x4B48
#define KDSKBSENT       0x4B49
#define KDGKBDIACR      0x4B4A
#define KDSKBDIACR      0x4B4B

struct kbd_repeat {
    int delay;
    int period;
};

#endif /* _LINUX_KD_H */
