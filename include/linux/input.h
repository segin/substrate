/*
 * <linux/input.h> — compat shim that bridges to substrate's
 * <sys/input.h>.  Provides struct input_event under the Linux name
 * plus the EV_*, KEY_*, BTN_*, REL_* code-namespace ported software
 * (xorg-server's kdrive evdev, libinput, evtest) references.
 */
#ifndef _LINUX_INPUT_H
#define _LINUX_INPUT_H

#include <sys/input.h>

/* Linux exposes input_event as a struct tag, substrate as a typedef.
 * Add an alias so both spellings work. */
#ifndef _LINUX_INPUT_HAVE_STRUCT_TAG
#define _LINUX_INPUT_HAVE_STRUCT_TAG 1
#define input_event input_event /* see typedef in <sys/input.h> */
#endif

/* Bit-test helpers — kdrive evdev uses ISBITSET / EVIOCGBIT. */
#ifndef BITS_PER_LONG
#define BITS_PER_LONG (8 * sizeof(long))
#endif
#ifndef BITS_TO_LONGS
#define BITS_TO_LONGS(nr) (((nr) + BITS_PER_LONG - 1) / BITS_PER_LONG)
#endif
#ifndef OFF
#define OFF(x)  ((x) % BITS_PER_LONG)
#endif
#ifndef LONG
#define LONG(x) ((x) / BITS_PER_LONG)
#endif
#ifndef test_bit
#define test_bit(bit, array) ((array)[LONG(bit)] >> OFF(bit) & 1)
#endif

/* EVIOCG* ioctl numbers — kdrive evdev uses EVIOCGBIT to probe a
 * device's capability bitmasks.  Substrate's /dev/input/event0
 * doesn't implement these (returns -ENOTTY); the kdrive code's
 * error path falls back to assuming standard kbd + mouse. */
/* IMPORTANT — the base values below must NOT carry any SIZE bits.
 * SIZE lives in ioctl bits 16-29 and is ORed in by the macro at the
 * call site.  Earlier versions of this header had 0x81004... bases
 * with SIZE=0x100 already set, so EVIOCGBIT(0, 4) decoded to size=260
 * — substrate's evdev ioctl handler then copied 260 zero bytes into
 * the caller's 4-byte stack buffer, smashing saved registers and
 * caller arg slots and producing a delayed NULL-pointer SIGSEGV
 * (e.g. xorg-server kdrive EvdevPtrEnable crashing at `pi->driverPrivate = ke;`
 * because the saved `pi` on the stack had been clobbered to 0).
 *
 * Correct bases: DIR=READ (bits 30-31 = 10), TYPE='E' (0x45<<8),
 * SIZE=0, NR=... .  E.g. EVIOCGNAME's base = 0x80004506:
 *   DIR  = 0x80000000  (bits 30=1, 31=1 → DIR_READ=2)
 *   SIZE = 0 (caller-supplied via the macro OR)
 *   TYPE = 0x4500      ('E' << 8)
 *   NR   = 0x06        (GNAME) */
#define EVIOCGVERSION    0x80044501u
#define EVIOCGID         0x80084502u
#define EVIOCGNAME(len)  (0x80004506u | ((len) << 16))
#define EVIOCGPHYS(len)  (0x80004507u | ((len) << 16))
#define EVIOCGUNIQ(len)  (0x80004508u | ((len) << 16))
#define EVIOCGBIT(ev, len) (0x80004520u | ((ev) << 8) | ((len) << 16))
#define EVIOCGKEY(len)   (0x80004518u | ((len) << 16))
#define EVIOCGABS(abs)   (0x80184540u | ((abs) << 8))

/* Maximum event/key/relative/absolute codes Linux defines — sized
 * generously so ISBITSET arrays match Linux's expectations. */
#define EV_MAX           0x1f
#define KEY_MAX          0x2ff
#define REL_MAX          0x0f
#define ABS_MAX          0x3f
#define MSC_MAX          0x07
#define LED_MAX          0x0f
#define SND_MAX          0x07
#define SW_MAX           0x10

#define EV_CNT           (EV_MAX + 1)
#define KEY_CNT          (KEY_MAX + 1)
#define REL_CNT          (REL_MAX + 1)
#define ABS_CNT          (ABS_MAX + 1)

/* Sync subcodes. */
#define SYN_REPORT       0
#define SYN_CONFIG       1

/* Absolute axes — the kdrive linux backend references ABS_X / ABS_Y /
 * ABS_PRESSURE for tslib code paths.  Substrate evdev doesn't emit
 * EV_ABS yet; defining the codes lets the code compile and the
 * runtime probes (EVIOCGBIT) just report "no abs axes." */
#define ABS_X            0x00
#define ABS_Y            0x01
#define ABS_Z            0x02
#define ABS_RX           0x03
#define ABS_RY           0x04
#define ABS_RZ           0x05
#define ABS_THROTTLE     0x06
#define ABS_RUDDER       0x07
#define ABS_WHEEL        0x08
#define ABS_GAS          0x09
#define ABS_BRAKE        0x0a
#define ABS_HAT0X        0x10
#define ABS_HAT0Y        0x11
#define ABS_HAT1X        0x12
#define ABS_HAT1Y        0x13
#define ABS_HAT2X        0x14
#define ABS_HAT2Y        0x15
#define ABS_HAT3X        0x16
#define ABS_HAT3Y        0x17
#define ABS_PRESSURE     0x18
#define ABS_DISTANCE     0x19
#define ABS_TILT_X       0x1a
#define ABS_TILT_Y       0x1b
#define ABS_TOOL_WIDTH   0x1c
#define ABS_VOLUME       0x20
#define ABS_MISC         0x28

/* Misc subcodes.  Empty for our purposes — substrate emits none. */
#define MSC_SERIAL       0x00
#define MSC_PULSELED     0x01
#define MSC_GESTURE      0x02
#define MSC_RAW          0x03
#define MSC_SCAN         0x04

/* EV_REL extras — REL_HWHEEL is used by xorg's evdev driver too. */
#ifndef REL_HWHEEL
#define REL_HWHEEL       0x06
#define REL_DIAL         0x07
#define REL_WHEEL_HI_RES 0x0b
#define REL_HWHEEL_HI_RES 0x0c
#endif

/* Additional buttons substrate's <sys/input.h> doesn't already define. */
#ifndef BTN_TOUCH
#define BTN_TOUCH        0x14a
#define BTN_STYLUS       0x14b
#define BTN_STYLUS2      0x14c
#endif
#ifndef BTN_FORWARD
#define BTN_FORWARD      0x115
#define BTN_BACK         0x116
#define BTN_TASK         0x117
#endif

/* Key code subset — kdrive's linux/keyboard.c only references the
 * scan-code -> XKB-keysym table by ordinal index, not by KEY_*
 * symbolic name; we only need the codes that other Xorg sources
 * reference.  Most software ifdef's around `defined(KEY_FOO)`. */
#define KEY_RESERVED     0
#define KEY_ESC          1
#define KEY_1            2
#define KEY_Q            16
#define KEY_W            17
#define KEY_LEFTCTRL     29
#define KEY_LEFTSHIFT    42
#define KEY_RIGHTSHIFT   54
#define KEY_LEFTALT      56
#define KEY_SPACE        57
#define KEY_F1           59
#define KEY_F10          68
#define KEY_F11          87
#define KEY_F12          88
#define KEY_RIGHTCTRL    97
#define KEY_RIGHTALT     100
#define KEY_HOME         102
#define KEY_UP           103
#define KEY_PAGEUP       104
#define KEY_LEFT         105
#define KEY_RIGHT        106
#define KEY_END          107
#define KEY_DOWN         108
#define KEY_PAGEDOWN     109
#define KEY_INSERT       110
#define KEY_DELETE       111
#define KEY_MAX          0x2ff /* duplicate of above; some headers re-define */

/* More button codes the kdrive evdev backend probes. */
#ifndef BTN_MISC
#define BTN_MISC         0x100
#define BTN_0            0x100
#define BTN_1            0x101
#endif
#ifndef BTN_MOUSE
#define BTN_MOUSE        0x110
#endif
#ifndef BTN_JOYSTICK
#define BTN_JOYSTICK     0x120
#define BTN_TRIGGER      0x120
#define BTN_GAMEPAD      0x130
#define BTN_DIGI         0x140
#define BTN_WHEEL        0x150
#endif

/* EVIOCGRAB — exclusive grab of an evdev device.  Substrate doesn't
 * implement exclusive grab; the ioctl returns ENOTTY and kdrive
 * falls back to non-grabbing reads. */
#ifndef EVIOCGRAB
#define EVIOCGRAB        0x40044590u
#endif

/* struct input_absinfo — describes the range/precision of an
 * EV_ABS axis.  Substrate doesn't emit EV_ABS events so the only
 * place this is used is in the unreachable absinfo[ABS_MAX] array. */
struct input_absinfo {
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    int32_t fuzz;
    int32_t flat;
    int32_t resolution;
};

/* struct input_id — kdrive evdev probes EVIOCGID; not implemented. */
struct input_id {
    uint16_t bustype;
    uint16_t vendor;
    uint16_t product;
    uint16_t version;
};

#endif /* _LINUX_INPUT_H */
