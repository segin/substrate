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

#endif
