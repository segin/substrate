/*
 * <linux/keyboard.h> — keysym table compat shim.
 *
 * Linux's <linux/keyboard.h> defines the K_* keysym namespace
 * (K_F1 / K_F12 / K_HOLE / K_PASTE / ...) the in-kernel console
 * keymap uses.  kdrive's linux/keyboard.c references the K_*
 * codes when building its scan->XKB-keysym translation.
 *
 * substrate's kernel-side keymap uses different identifiers, so
 * we just provide a minimal set sufficient for kdrive's
 * scancode-walking code to compile.  The runtime path uses raw
 * scan codes / EV_KEY events, not the K_* layer.
 */
#ifndef _LINUX_KEYBOARD_H
#define _LINUX_KEYBOARD_H

#define KG_SHIFT     0
#define KG_CTRL      2
#define KG_ALT       3
#define KG_ALTGR     1
#define KG_SHIFTL    4
#define KG_KANASHIFT 4
#define KG_SHIFTR    5
#define KG_CTRLL     6
#define KG_CTRLR     7
#define KG_CAPSSHIFT 8

#define NR_SHIFT     9
#define NR_KEYS      256
#define MAX_NR_KEYMAPS 256
#define MAX_NR_OF_USER_KEYMAPS 256
#define MAX_NR_FUNC  256
#define MAX_NR_CONSOLES 63

#define KT_LATIN     0
#define KT_FN        1
#define KT_SPEC      2
#define KT_PAD       3
#define KT_DEAD      4
#define KT_CONS      5
#define KT_CUR       6
#define KT_SHIFT     7
#define KT_META      8
#define KT_ASCII     9
#define KT_LOCK     10
#define KT_LETTER   11
#define KT_SLOCK    12

#define K(t, v)        (((t) << 8) | (v))
#define KTYP(x)        ((x) >> 8)
#define KVAL(x)        ((x) & 0xff)

#define K_F1           K(KT_FN,  0)
#define K_F12          K(KT_FN, 11)
#define K_HOLE         K(KT_SPEC, 0)

struct kbd_struct;
struct kbentry {
    unsigned char kb_table;
    unsigned char kb_index;
    unsigned short kb_value;
};

struct kbsentry {
    unsigned char  kb_func;
    unsigned char  kb_string[512];
};

#endif /* _LINUX_KEYBOARD_H */
