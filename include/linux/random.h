/*
 * <linux/random.h> — kernel-side random-device UAPI.  Substrate uses
 * arc4random / getrandom internally; this header exists so probes
 * detect a workable shape.  GRND_* flags match the Linux convention.
 */
#ifndef _LINUX_RANDOM_H
#define _LINUX_RANDOM_H

#include <linux/types.h>

#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002
#define GRND_INSECURE 0x0004

#endif
