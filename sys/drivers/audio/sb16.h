/*
 * sb16.h - Creative SoundBlaster 16 driver (kernel-internal).
 *
 * The DSP at the configurable I/O base (default 0x220) is the public
 * face of every SB-family card; the SB16 adds 16-bit signed/stereo
 * support, programmable IRQ via mixer reg 0x80, programmable DMA
 * channels via mixer reg 0x81, and CT-1745 mixer commands.  We rely
 * on the auto-init DMA mode (0xB6) for streaming playback.
 */

#ifndef _DRIVERS_AUDIO_SB16_H
#define _DRIVERS_AUDIO_SB16_H

#include <stddef.h>
#include <stdint.h>

#define SB16_DEFAULT_BASE   0x220   /* I/O base */
#define SB16_DEFAULT_IRQ    5
#define SB16_DEFAULT_DMA8   1
#define SB16_DEFAULT_DMA16  5

/* Per-board offsets from SB16_DEFAULT_BASE. */
#define SB16_REG_RESET            0x06
#define SB16_REG_READ_DATA        0x0A
#define SB16_REG_WRITE_DATA       0x0C
#define SB16_REG_READ_BUF_STATUS  0x0E
#define SB16_REG_INTR_ACK_8       0x0E
#define SB16_REG_INTR_ACK_16      0x0F
#define SB16_REG_MIXER_ADDR       0x04
#define SB16_REG_MIXER_DATA       0x05

/* DSP commands */
#define SB16_DSP_GET_VERSION      0xE1
#define SB16_DSP_SET_TIME_CONST   0x40   /* legacy 8-bit */
#define SB16_DSP_SET_SAMPLE_RATE  0x41   /* output rate, 16-bit Hz */
#define SB16_DSP_SET_INPUT_RATE   0x42
#define SB16_DSP_SPEAKER_ON       0xD1
#define SB16_DSP_SPEAKER_OFF      0xD3
#define SB16_DSP_HALT_DMA8        0xD0
#define SB16_DSP_CONTINUE_DMA8    0xD4
#define SB16_DSP_HALT_DMA16       0xD5
#define SB16_DSP_CONTINUE_DMA16   0xD6

/*
 * 16-bit programmed I/O command:
 *   0xB6  - 16-bit DAC, auto-init DMA, FIFO on
 *   mode  - 0x10 stereo, 0x20 signed; combine for "stereo signed".
 */
#define SB16_DSP_PLAY_16BIT       0xB6
#define SB16_DSP_PLAY_8BIT        0xC6
#define SB16_DSP_MODE_STEREO      0x20
#define SB16_DSP_MODE_SIGNED      0x10

/* Mixer registers (CT-1745) */
#define SB16_MIXER_RESET          0x00
#define SB16_MIXER_MASTER_VOL     0x30
#define SB16_MIXER_MASTER_VOL_R   0x31
#define SB16_MIXER_VOICE_VOL      0x32
#define SB16_MIXER_VOICE_VOL_R    0x33
#define SB16_MIXER_IRQ_SELECT     0x80
#define SB16_MIXER_DMA_SELECT     0x81

/*
 * Encode an IRQ number for mixer reg 0x80.  Bits 0/1/2/3 select IRQ
 * 2/5/7/10 respectively; everything else is unsupported on the SB16.
 */
uint8_t sb16_encode_irq(int irq);

/*
 * Encode a (8-bit, 16-bit) DMA pair for mixer reg 0x81.  Bits 0..3
 * select channels 0..3 for 8-bit transfers; bits 5..7 select channels
 * 5..7 for 16-bit.
 */
uint8_t sb16_encode_dma(int dma8, int dma16);

/*
 * Translate a sample rate (Hz) to a DSP "time constant" used by the
 * pre-SB16 0x40 command.  Modern SB16 should use 0x41 with the rate
 * directly, but the legacy path is tested for compatibility.
 *
 * Formula (per Creative spec):  tc = 256 - (1000000 / rate)
 * Result is clamped to [0..255].
 */
uint8_t sb16_time_constant(uint32_t hz);

/*
 * Encode the second mode byte for SB16_DSP_PLAY_16BIT given the channel
 * count and signedness.  Stereo 16-bit signed PCM (channels=2,
 * signed=1) returns 0x30.
 */
uint8_t sb16_play_mode(uint32_t channels, int is_signed);

void sb16_init(void);

#endif /* _DRIVERS_AUDIO_SB16_H */
