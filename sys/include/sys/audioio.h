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

typedef struct audio_prinfo {
	uint32_t sample_rate;        /* samples per second */
	uint32_t channels;           /* mono = 1, stereo = 2 */
	uint32_t precision;          /* bits per sample */
	uint32_t encoding;           /* AUDIO_ENCODING_* */
	uint32_t gain;               /* AUDIO_MIN_GAIN..AUDIO_MAX_GAIN */
	uint32_t port;               /* selected port bitmap */
	uint32_t avail_ports;        /* available port bitmap */
	uint32_t buffer_size;        /* device buffer size in bytes */
	uint32_t samples;            /* samples processed since open */
	uint32_t eof;                /* EOF count (write side) */
	uint8_t  pause;              /* non-zero = paused */
	uint8_t  error;              /* over/underrun since last query */
	uint8_t  waiting;            /* a thread is blocked on this side */
	uint8_t  balance;            /* AUDIO_LEFT_BAL..AUDIO_RIGHT_BAL */
	uint8_t  cflags;             /* channel flags (reserved) */
	uint8_t  pad[3];
	uint8_t  open;               /* device is open for this direction */
	uint8_t  active;              /* I/O is in progress */
} audio_prinfo_t;

/* ------------------------------------------------------------------- */
/* Aggregate device state                                              */
/* ------------------------------------------------------------------- */

typedef struct audio_info {
	audio_prinfo_t play;
	audio_prinfo_t record;
	uint32_t monitor_gain;       /* monitor (loopback) gain */
	uint32_t mode;               /* AUMODE_* bitmap */
	uint32_t blocksize;          /* I/O block size in bytes */
	uint32_t hiwat;              /* output high-water (in blocks) */
	uint32_t lowat;              /* output low-water (in blocks) */
	uint32_t _ispare1;
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

#define AUDIO_INITINFO(_p) do {                                        \
	audio_info_t *__a = (_p);                                          \
	audio_prinfo_t *__pri = &__a->play;                                \
	audio_prinfo_t *__rec = &__a->record;                              \
	int __i;                                                           \
	for (__i = 0; __i < 2; __i++) {                                    \
		audio_prinfo_t *__d = (__i == 0) ? __pri : __rec;              \
		__d->sample_rate = AUDIO_NOTSET_U32;                           \
		__d->channels    = AUDIO_NOTSET_U32;                           \
		__d->precision   = AUDIO_NOTSET_U32;                           \
		__d->encoding    = AUDIO_NOTSET_U32;                           \
		__d->gain        = AUDIO_NOTSET_U32;                           \
		__d->port        = AUDIO_NOTSET_U32;                           \
		__d->avail_ports = AUDIO_NOTSET_U32;                           \
		__d->buffer_size = AUDIO_NOTSET_U32;                           \
		__d->samples     = AUDIO_NOTSET_U32;                           \
		__d->eof         = AUDIO_NOTSET_U32;                           \
		__d->pause       = AUDIO_NOTSET_U8;                            \
		__d->error       = AUDIO_NOTSET_U8;                            \
		__d->waiting     = AUDIO_NOTSET_U8;                            \
		__d->balance     = AUDIO_NOTSET_U8;                            \
		__d->cflags      = AUDIO_NOTSET_U8;                            \
		__d->open        = AUDIO_NOTSET_U8;                            \
		__d->active      = AUDIO_NOTSET_U8;                            \
	}                                                                  \
	__a->monitor_gain = AUDIO_NOTSET_U32;                              \
	__a->mode         = AUDIO_NOTSET_U32;                              \
	__a->blocksize    = AUDIO_NOTSET_U32;                              \
	__a->hiwat        = AUDIO_NOTSET_U32;                              \
	__a->lowat        = AUDIO_NOTSET_U32;                              \
	__a->_ispare1     = AUDIO_NOTSET_U32;                              \
} while (0)

/* ------------------------------------------------------------------- */
/* ioctl numbers                                                       */
/* ------------------------------------------------------------------- */

/*
 * Numbering follows the convention used elsewhere in Substrate (high
 * byte 'A' = 0x41, second byte the subsystem id, low half the command).
 * The trailing byte mirrors NetBSD's offsets where the API overlaps so
 * porting an existing audio program is a recompile.
 */
#define AUDIO_GETINFO        0x41020001U  /* R audio_info_t */
#define AUDIO_SETINFO        0x41020002U  /* W audio_info_t */
#define AUDIO_DRAIN          0x41020003U  /* (no arg) */
#define AUDIO_FLUSH          0x41020004U  /* (no arg) */
#define AUDIO_WSEEK          0x41020005U  /* R uint32_t */
#define AUDIO_RERROR         0x41020006U  /* R int (record errors) */
#define AUDIO_GETDEV         0x41020007U  /* R audio_device_t */
#define AUDIO_GETENC         0x41020008U  /* RW audio_encoding_t */
#define AUDIO_GETFD          0x41020009U  /* R int */
#define AUDIO_SETFD          0x4102000AU  /* W int */
#define AUDIO_GETPROPS       0x4102000BU  /* R int */

#endif /* _SYS_AUDIOIO_H */
