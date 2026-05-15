/*
 * sys/audioio.h - Sun-compatible audio device interface.
 *
 * The Substrate audio API descends from Sun AudioIO (SunOS 4 / Solaris)
 * via NetBSD's audio(4).  Userspace programs targeting `/dev/audio` on
 * Solaris, NetBSD, or OpenBSD's compat layer should compile against this
 * header unmodified.
 *
 * Two character devices are exposed per audio backend:
 *   /dev/audioN     — full audio I/O (read/write blocks, ioctl)
 *   /dev/audioctlN  — control-only (ioctl, no transfers)
 *
 * The state of an audio device is encapsulated in an audio_info_t.  Each
 * direction (play and record) carries its own audio_prinfo_t with sample
 * rate, encoding, channel count, gain, and runtime statistics.  Use
 * AUDIO_INITINFO() to mark every field "no change", then assign only the
 * fields you want SETINFO to honor; the kernel ignores fields that still
 * carry the sentinel value.
 */

#ifndef _SYS_AUDIOIO_H
#define _SYS_AUDIOIO_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------- */
/* Encodings                                                           */
/* ------------------------------------------------------------------- */

#define AUDIO_ENCODING_NONE          0   /* Not set */
#define AUDIO_ENCODING_ULAW          1   /* µ-law (G.711) */
#define AUDIO_ENCODING_ALAW          2   /* A-law (G.711) */
#define AUDIO_ENCODING_PCM16         3   /* Signed linear, native byte order */
#define AUDIO_ENCODING_PCM8          4   /* Unsigned linear */
#define AUDIO_ENCODING_ADPCM         5   /* IMA ADPCM */
#define AUDIO_ENCODING_SLINEAR_LE    6   /* Signed linear, little-endian */
#define AUDIO_ENCODING_SLINEAR_BE    7   /* Signed linear, big-endian */
#define AUDIO_ENCODING_ULINEAR_LE    8   /* Unsigned linear, little-endian */
#define AUDIO_ENCODING_ULINEAR_BE    9   /* Unsigned linear, big-endian */
#define AUDIO_ENCODING_SLINEAR      10   /* Signed linear, native byte order */
#define AUDIO_ENCODING_ULINEAR      11   /* Unsigned linear, native byte order */
#define AUDIO_ENCODING_MPEG_L1_STREAM 12
#define AUDIO_ENCODING_MPEG_L1_PACKETS 13
#define AUDIO_ENCODING_MPEG_L1_SYSTEM 14
#define AUDIO_ENCODING_MPEG_L2_STREAM 15
#define AUDIO_ENCODING_MPEG_L2_PACKETS 16
#define AUDIO_ENCODING_MPEG_L2_SYSTEM 17
#define AUDIO_ENCODING_AC3          18

#define AUDIO_ENCODINGSTRLEN        16

typedef struct audio_encoding {
	int     index;                                   /* in */
	char    name[AUDIO_ENCODINGSTRLEN];              /* out */
	int     encoding;                                /* out */
	int     precision;                               /* out */
	int     flags;                                   /* out */
#define AUDIO_ENCODINGFLAG_EMULATED 0x0001    /* software-emulated */
} audio_encoding_t;

/* ------------------------------------------------------------------- */
/* Per-direction info                                                  */
/* ------------------------------------------------------------------- */

/*
 * Layout below is byte-for-byte identical to NetBSD 10's
 * struct audio_prinfo (sizeof == 56) so binaries built against
 * NetBSD's <sys/audioio.h> see the same offsets.  Do not reorder.
 */
typedef struct audio_prinfo {
	uint32_t sample_rate;        /* samples per second */
	uint32_t channels;           /* mono = 1, stereo = 2 */
	uint32_t precision;          /* bits per sample */
	uint32_t encoding;           /* AUDIO_ENCODING_* */
	uint32_t gain;               /* AUDIO_MIN_GAIN..AUDIO_MAX_GAIN */
	uint32_t port;               /* selected port bitmap */
	uint32_t seek;               /* BSD: byte offset within buffer */
	uint32_t avail_ports;        /* available port bitmap */
	uint32_t buffer_size;        /* device buffer size in bytes */
	uint32_t _ispare[1];
	uint32_t samples;            /* samples processed since open */
	uint32_t eof;                /* EOF count (write side) */
	uint8_t  pause;              /* non-zero = paused */
	uint8_t  error;              /* over/underrun since last query */
	uint8_t  waiting;            /* a thread is blocked on this side */
	uint8_t  balance;            /* AUDIO_LEFT_BAL..AUDIO_RIGHT_BAL */
	uint8_t  cspare[2];
	uint8_t  open;               /* device is open for this direction */
	uint8_t  active;             /* I/O is in progress */
} audio_prinfo_t;

/* ------------------------------------------------------------------- */
/* Aggregate device state                                              */
/* ------------------------------------------------------------------- */

/*
 * Layout matches NetBSD 10's struct audio_info exactly (sizeof == 136,
 * 2 × audio_prinfo + 6 × uint32_t).  `mode` is the LAST field — do not
 * move it; mpg123's libout123/output_sun.so writes a sizeof'd struct
 * here and the SETINFO ioctl number embeds the size in the high half
 * of the request word, so the layout MUST agree to the byte.
 */
typedef struct audio_info {
	audio_prinfo_t play;
	audio_prinfo_t record;
	uint32_t monitor_gain;       /* monitor (loopback) gain */
	uint32_t blocksize;          /* I/O block size in bytes */
	uint32_t hiwat;              /* output high-water (in blocks) */
	uint32_t lowat;              /* output low-water (in blocks) */
	uint32_t _ispare1;
	uint32_t mode;               /* AUMODE_* bitmap */
} audio_info_t;

/*
 * AUMODE bits.  PLAY and PLAY_ALL differ only in whether short writes are
 * extended with silence (PLAY_ALL) or treated as underruns (PLAY).
 */
#define AUMODE_PLAY     0x01
#define AUMODE_RECORD   0x02
#define AUMODE_PLAY_ALL 0x04

/*
 * Gain / balance.  Sun used 0..255 with a 128-step balance centered at
 * AUDIO_MID_BALANCE; we keep that scale.
 */
#define AUDIO_MIN_GAIN     0u
#define AUDIO_MAX_GAIN     255u
#define AUDIO_LEFT_BAL     0
#define AUDIO_MID_BALANCE  32
#define AUDIO_RIGHT_BAL    64

/*
 * Identifying string returned by AUDIO_GETDEV.
 */
typedef struct audio_device {
	char name[16];
	char version[16];
	char config[16];
} audio_device_t;

/*
 * Capability bits returned by AUDIO_GETPROPS.
 */
#define AUDIO_PROP_FULLDUPLEX  0x0001
#define AUDIO_PROP_MMAP        0x0002
#define AUDIO_PROP_INDEPENDENT 0x0004
#define AUDIO_PROP_PLAYBACK    0x0010
#define AUDIO_PROP_CAPTURE     0x0020

/*
 * Sentinel used by AUDIO_INITINFO to mark "field unchanged" in a SETINFO
 * payload.  Must equal the all-ones bit pattern of the corresponding
 * field type so the driver can detect untouched values uniformly across
 * uint32_t and uint8_t members.
 */
#define AUDIO_NOTSET_U32 0xFFFFFFFFu
#define AUDIO_NOTSET_U8  0xFFu

/*
 * Mirror NetBSD's idiom: blanket-fill with 0xff so every field
 * (including any layout padding) carries the sentinel uniformly,
 * regardless of whether the caller built against an older or newer
 * variant of the struct.
 */
#define AUDIO_INITINFO(_p) \
	(void)memset((void *)(_p), 0xff, sizeof(audio_info_t))

/* ------------------------------------------------------------------- */
/* ioctl numbers                                                       */
/* ------------------------------------------------------------------- */

/*
 * BSD-encoded ioctl numbers, matching NetBSD audio(4) byte-for-byte:
 *
 *   bits 31..30 : direction (00 none, 01 R, 10 W, 11 RW)
 *   bits 28..16 : payload size in bytes
 *   bits 15..8  : group ('A' for audio)
 *   bits  7..0  : sub-command number
 *
 * Defined inline (rather than via <sys/ioccom.h>) because Substrate's
 * generic _IOC encoding (memio.h) uses 14 size bits — incompatible
 * with NetBSD's 13-bit field — and we want exact source compatibility
 * with NetBSD binaries.
 */
#define _AUDIO_IOC(dir, nr, sz) \
	((uint32_t)((dir) | (((sz) & 0x1fff) << 16) | (0x41 << 8) | (nr)))
#define _AUDIO_IO(nr)         _AUDIO_IOC(0x20000000U, (nr), 0)
#define _AUDIO_IOR(nr, t)     _AUDIO_IOC(0x40000000U, (nr), sizeof(t))
#define _AUDIO_IOW(nr, t)     _AUDIO_IOC(0x80000000U, (nr), sizeof(t))
#define _AUDIO_IOWR(nr, t)    _AUDIO_IOC(0xc0000000U, (nr), sizeof(t))

#define AUDIO_GETINFO    _AUDIO_IOR(21, audio_info_t)        /* 0x40884115 */
#define AUDIO_SETINFO    _AUDIO_IOWR(22, audio_info_t)       /* 0xc0884116 */
#define AUDIO_DRAIN      _AUDIO_IO(23)                       /* 0x20004117 */
#define AUDIO_FLUSH      _AUDIO_IO(24)                       /* 0x20004118 */
#define AUDIO_WSEEK      _AUDIO_IOR(25, uint32_t)            /* 0x40044119 */
#define AUDIO_RERROR     _AUDIO_IOR(26, int)                 /* 0x4004411a */
#define AUDIO_GETDEV     _AUDIO_IOR(27, audio_device_t)      /* 0x4030411b */
#define AUDIO_GETENC     _AUDIO_IOWR(28, audio_encoding_t)   /* 0xc028411c */
#define AUDIO_GETFD      _AUDIO_IOR(29, int)                 /* 0x4004411d */
#define AUDIO_SETFD      _AUDIO_IOWR(30, int)                /* 0xc004411e */
#define AUDIO_PERROR     _AUDIO_IOR(31, int)                 /* 0x4004411f */
#define AUDIO_GETPROPS   _AUDIO_IOR(34, int)                 /* 0x40044122 */

#endif /* _SYS_AUDIOIO_H */
