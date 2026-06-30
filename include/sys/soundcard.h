/*
 * sys/soundcard.h - OSS (Open Sound System) device interface.
 *
 * Substrate exposes /dev/dsp (and /dev/dsp0) as an OSS-compatible
 * frontend onto the same audio backend that drives the Sun/SADA
 * /dev/audio nodes.  Programs targeting OSS — native, Linux, or
 * FreeBSD — should compile against this header unmodified.
 *
 * The ioctl numbers use the classic OSS / 4Front "_SIOC" encoding
 * (BSD-style: 2 direction bits, 13 size bits, 8 group bits, 8 number
 * bits).  This is identical on Linux's <sys/soundcard.h>, FreeBSD's
 * <sys/soundcard.h>, and here, so the canonical values below match
 * every OSS implementation byte-for-byte:
 *
 *   SNDCTL_DSP_SPEED   == 0xC0045002
 *   SNDCTL_DSP_SETFMT  == 0xC0045005
 *   SNDCTL_DSP_GETOSPACE == 0x4010500C
 *
 * The kernel-side dispatch is encoding-tolerant anyway — it matches on
 * the group ('P') and command number only, ignoring the direction/size
 * high bits — so a foreign personality's slightly different _IOC layout
 * still routes to the right handler.
 */

#ifndef _SYS_SOUNDCARD_H
#define _SYS_SOUNDCARD_H

#include <stdint.h>

/* ------------------------------------------------------------------- */
/* OSS ioctl encoding (BSD-style _SIOC, 13-bit size field)             */
/* ------------------------------------------------------------------- */

#define OSS_IOC_VOID    0x00000000U
#define OSS_IOC_OUT     0x40000000U   /* read:  driver -> userspace   */
#define OSS_IOC_IN      0x80000000U   /* write: userspace -> driver   */
#define OSS_IOC_INOUT   (OSS_IOC_IN | OSS_IOC_OUT)
#define OSS_IOCPARM_MASK 0x1fffU

#define _OSS_IOC(inout, group, num, len) \
	((uint32_t)((inout) | (((len) & OSS_IOCPARM_MASK) << 16) | \
		    (((group) & 0xFF) << 8) | ((num) & 0xFF)))

#define _OSS_IO(g, n)        _OSS_IOC(OSS_IOC_VOID,  (g), (n), 0)
#define _OSS_IOR(g, n, t)    _OSS_IOC(OSS_IOC_OUT,   (g), (n), sizeof(t))
#define _OSS_IOW(g, n, t)    _OSS_IOC(OSS_IOC_IN,    (g), (n), sizeof(t))
#define _OSS_IOWR(g, n, t)   _OSS_IOC(OSS_IOC_INOUT, (g), (n), sizeof(t))

/* ------------------------------------------------------------------- */
/* Sample formats (SNDCTL_DSP_SETFMT / SNDCTL_DSP_GETFMTS bitmask)     */
/* ------------------------------------------------------------------- */

#define AFMT_QUERY      0x00000000   /* return current format         */
#define AFMT_MU_LAW     0x00000001
#define AFMT_A_LAW      0x00000002
#define AFMT_IMA_ADPCM  0x00000004
#define AFMT_U8         0x00000008
#define AFMT_S16_LE     0x00000010   /* signed 16-bit little-endian   */
#define AFMT_S16_BE     0x00000020   /* signed 16-bit big-endian      */
#define AFMT_S8         0x00000040
#define AFMT_U16_LE     0x00000080
#define AFMT_U16_BE     0x00000100
#define AFMT_MPEG       0x00000200
#define AFMT_AC3        0x00000400

/* ------------------------------------------------------------------- */
/* SNDCTL_DSP_GETOSPACE / GETISPACE payload                            */
/* ------------------------------------------------------------------- */

typedef struct audio_buf_info {
	int fragments;     /* # of fragments that can be read/written now */
	int fragstotal;    /* total # of fragments in the buffer          */
	int fragsize;      /* size of a fragment in bytes                 */
	int bytes;         /* bytes that can be read/written now          */
} audio_buf_info;

/* SNDCTL_DSP_GETIPTR / GETOPTR payload (provided for completeness). */
typedef struct count_info {
	int bytes;         /* total bytes processed                       */
	int blocks;        /* fragment transitions since last query       */
	int ptr;           /* current DMA pointer within the buffer       */
} count_info;

/* ------------------------------------------------------------------- */
/* DSP capability bits (SNDCTL_DSP_GETCAPS)                            */
/* ------------------------------------------------------------------- */

#define DSP_CAP_REVISION  0x000000ff
#define DSP_CAP_DUPLEX    0x00000100
#define DSP_CAP_REALTIME  0x00000200
#define DSP_CAP_BATCH     0x00000400
#define DSP_CAP_COPROC    0x00000800
#define DSP_CAP_TRIGGER   0x00001000
#define DSP_CAP_MMAP      0x00002000
#define DSP_CAP_MULTI     0x00004000
#define DSP_CAP_BIND      0x00008000

/* Trigger bits (SNDCTL_DSP_SETTRIGGER / GETTRIGGER). */
#define PCM_ENABLE_INPUT  0x00000001
#define PCM_ENABLE_OUTPUT 0x00000002

/* ------------------------------------------------------------------- */
/* DSP ioctl commands (group 'P')                                      */
/* ------------------------------------------------------------------- */

#define OSS_GROUP_DSP   'P'

#define SNDCTL_DSP_RESET        _OSS_IO  ('P', 0)
#define SNDCTL_DSP_SYNC         _OSS_IO  ('P', 1)
#define SNDCTL_DSP_SPEED        _OSS_IOWR('P', 2, int)
#define SNDCTL_DSP_STEREO       _OSS_IOWR('P', 3, int)
#define SNDCTL_DSP_GETBLKSIZE   _OSS_IOWR('P', 4, int)
#define SNDCTL_DSP_SETFMT       _OSS_IOWR('P', 5, int)
#define SNDCTL_DSP_SAMPLESIZE   SNDCTL_DSP_SETFMT
#define SNDCTL_DSP_CHANNELS     _OSS_IOWR('P', 6, int)
#define SOUND_PCM_WRITE_FILTER  _OSS_IOWR('P', 7, int)
#define SNDCTL_DSP_POST         _OSS_IO  ('P', 8)
#define SNDCTL_DSP_SUBDIVIDE    _OSS_IOWR('P', 9, int)
#define SNDCTL_DSP_SETFRAGMENT  _OSS_IOWR('P', 10, int)
#define SNDCTL_DSP_GETFMTS      _OSS_IOR ('P', 11, int)
#define SNDCTL_DSP_GETOSPACE    _OSS_IOR ('P', 12, audio_buf_info)
#define SNDCTL_DSP_GETISPACE    _OSS_IOR ('P', 13, audio_buf_info)
#define SNDCTL_DSP_NONBLOCK     _OSS_IO  ('P', 14)
#define SNDCTL_DSP_GETCAPS      _OSS_IOR ('P', 15, int)
#define SNDCTL_DSP_GETTRIGGER   _OSS_IOR ('P', 16, int)
#define SNDCTL_DSP_SETTRIGGER   _OSS_IOW ('P', 16, int)
#define SNDCTL_DSP_GETIPTR      _OSS_IOR ('P', 17, count_info)
#define SNDCTL_DSP_GETOPTR      _OSS_IOR ('P', 18, count_info)
#define SNDCTL_DSP_SETDUPLEX    _OSS_IO  ('P', 22)
#define SNDCTL_DSP_GETODELAY    _OSS_IOR ('P', 23, int)

/* Common aliases used by legacy OSS code. */
#define SOUND_PCM_WRITE_RATE     SNDCTL_DSP_SPEED
#define SOUND_PCM_WRITE_CHANNELS SNDCTL_DSP_CHANNELS
#define SOUND_PCM_WRITE_BITS     SNDCTL_DSP_SETFMT
#define SOUND_PCM_POST           SNDCTL_DSP_POST
#define SOUND_PCM_RESET          SNDCTL_DSP_RESET
#define SOUND_PCM_SYNC           SNDCTL_DSP_SYNC
#define SOUND_PCM_GETOSPACE      SNDCTL_DSP_GETOSPACE

#endif /* _SYS_SOUNDCARD_H */
