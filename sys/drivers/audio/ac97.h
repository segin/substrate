/*
 * ac97.h - AC'97 audio controller driver (kernel-internal).
 *
 * Targets the Intel ICH-style AC'97 controller (PCI class 0x04,
 * subclass 0x01) — the canonical implementation present in QEMU's
 * `-device ac97`, on real Intel ICH chipsets, and in compatible
 * VIA / SiS / nForce devices.  Two BARs are exposed:
 *
 *   BAR0 (NAMBAR)   - 256 bytes of AC'97 mixer / codec registers
 *   BAR1 (NABMBAR)  - 64 bytes of bus-master DMA registers (per stream)
 *
 * The bus master moves PCM data from a Buffer Descriptor List (BDL) of
 * up to 32 entries into the codec's serial link.  We allocate a small
 * ring of buffers and refill it from audio_dev_ops_t.write().
 */

#ifndef _DRIVERS_AUDIO_AC97_H
#define _DRIVERS_AUDIO_AC97_H

#include <stddef.h>
#include <stdint.h>
#include <sys/dma.h>

/* ------------------------------------------------------------------- */
/* Mixer (NAMBAR) registers                                            */
/* ------------------------------------------------------------------- */

#define AC97_RESET            0x00
#define AC97_MASTER_VOLUME    0x02
#define AC97_AUX_OUT_VOLUME   0x04
#define AC97_MONO_VOLUME      0x06
#define AC97_PCM_OUT_VOLUME   0x18
#define AC97_EXT_AUDIO_ID     0x28
#define AC97_EXT_AUDIO_CTRL   0x2A
#define AC97_PCM_FRONT_DAC_RATE 0x2C
#define AC97_PCM_LR_ADC_RATE  0x32

/* AC97_EXT_AUDIO_CTRL bits */
#define AC97_EXT_VRA          0x0001  /* Variable Rate Audio enable */

/* ------------------------------------------------------------------- */
/* Bus-master (NABMBAR) registers                                      */
/* ------------------------------------------------------------------- */

#define AC97_BM_PI_BASE       0x00   /* PCM In  */
#define AC97_BM_PO_BASE       0x10   /* PCM Out */
#define AC97_BM_MC_BASE       0x20   /* Mic In  */

/* Per-stream offsets, added to one of the bases above. */
#define AC97_BM_BDBAR         0x00   /* Buffer Descriptor Base Address (32-bit phys) */
#define AC97_BM_CIV           0x04   /* Current Index Value */
#define AC97_BM_LVI           0x05   /* Last Valid Index */
#define AC97_BM_SR            0x06   /* Status (16-bit) */
#define AC97_BM_PICB          0x08   /* Position in Current Buffer (samples) */
#define AC97_BM_PIV           0x0A   /* Prefetched Index Value */
#define AC97_BM_CR            0x0B   /* Control */

/* Status (SR) bits */
#define AC97_SR_DCH           0x0001  /* DMA Controller Halted */
#define AC97_SR_CELV          0x0002  /* Current Equals Last Valid */
#define AC97_SR_LVBCI         0x0004  /* Last Valid Buffer Completion */
#define AC97_SR_BCIS          0x0008  /* Buffer Completion Interrupt Status */
#define AC97_SR_FIFOE         0x0010  /* FIFO Error */

/* Control (CR) bits */
#define AC97_CR_RPBM          0x01   /* Run / Pause Bus Master */
#define AC97_CR_RR            0x02   /* Reset Registers */
#define AC97_CR_LVBIE         0x04   /* Last Valid Buffer Interrupt Enable */
#define AC97_CR_FEIE          0x08   /* FIFO Error Interrupt Enable */
#define AC97_CR_IOCE          0x10   /* Interrupt On Completion Enable */

/* Global Control (GLOB_CNT, NABMBAR + 0x2C) */
#define AC97_GLOB_CNT         0x2C
#define AC97_GLOB_STA         0x30
#define AC97_GLOB_CNT_COLD_RESET 0x00000002U
#define AC97_GLOB_CNT_WARM_RESET 0x00000004U
#define AC97_GLOB_STA_PCR        0x00000100U /* Primary Codec Ready */

/* ------------------------------------------------------------------- */
/* Buffer Descriptor List                                              */
/* ------------------------------------------------------------------- */

#define AC97_BDL_ENTRIES      32

/*
 * One descriptor: 32-bit physical address + 16-bit sample count + flags.
 * Sample count is in 16-bit samples (so 8 KB of stereo 16-bit audio is
 * 4096 samples per channel pair).
 */
typedef struct ac97_bdl_entry {
	uint32_t buf_phys;
	uint16_t samples;
	uint16_t flags;
} __attribute__((packed)) ac97_bdl_entry_t;

#define AC97_BDL_F_BUP        0x4000   /* Buffer Underrun Policy */
#define AC97_BDL_F_IOC        0x8000   /* Interrupt on Completion */

/*
 * Build one BDL entry.  Exposed so host tests can verify packing
 * without instantiating the full driver.  samples is in 16-bit sample
 * units (NOT bytes); ioc=1 sets the IOC flag.
 */
void ac97_build_bdl_entry(ac97_bdl_entry_t *entry, uint32_t buf_phys,
                          uint16_t samples, int ioc);

/*
 * Encode an AC'97 mixer volume register value.  The mixer uses a
 * 6-bit attenuation per channel (0 = loudest, 63 = softest, 0..0x1F
 * for some registers); bit 15 is the mute bit.
 */
uint16_t ac97_mixer_volume(uint8_t left, uint8_t right, int mute);

/*
 * Translate a libc-style sample-rate into the value the AC'97 codec's
 * VRA register expects.  Variable Rate Audio just stores the rate
 * directly in Hz; non-VRA codecs are clamped to 48 kHz.
 */
uint16_t ac97_encode_rate(uint32_t hz, int has_vra);

void ac97_init(void);

#endif /* _DRIVERS_AUDIO_AC97_H */
