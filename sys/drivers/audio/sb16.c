/*
 * sb16.c - Creative SoundBlaster 16 driver.
 *
 * One-shot 16-bit signed playback.  The DSP at the configured base is
 * probed via the canonical reset sequence; on success an ISA DMA buffer
 * is allocated below 16 MB and registered with the audio framework.
 * Each write() copies user PCM into the DMA buffer, programs the 16-bit
 * DMA channel, and issues 0xB6 with the right mode/length pair.
 *
 * Streaming via auto-init mode is intentionally left for a follow-up
 * once the framework gains ring-buffer book-keeping; this driver
 * exists primarily to validate the audio framework against real
 * hardware and to give Sound Blaster–era programs something to bind
 * to on legacy systems.
 */

#include "sb16.h"
#include "audio.h"

#include <kern/console.h>
#include <kern/isa.h>
#include <sys/audioio.h>
#include <sys/errno.h>
#include <sys/irq.h>
#include <vm/phys_mem.h>
#include <vm/vm_page.h>
#include <arch/x86-common/io.h>
#include <stdio.h>
#include <string.h>

#define SB16_DMA_BUFFER_SIZE   (16U * 1024U)  /* 16 KB */
#define SB16_DMA_LIMIT         0x01000000U    /* 16 MB ISA window */
#define SB16_DSP_RESET_TIMEOUT 0x10000

/* 16-bit DMA controller register set (channels 4..7). */
#define SB16_DMA16_MASK_PORT   0xD4
#define SB16_DMA16_MODE_PORT   0xD6
#define SB16_DMA16_FF_PORT     0xD8
#define SB16_DMA16_ADDR_PORT   0xC4   /* channel 5 */
#define SB16_DMA16_COUNT_PORT  0xC6
#define SB16_DMA16_PAGE_PORT   0x8B

/* ------------------------------------------------------------------- */
/* Pure helpers (also reachable from host tests)                       */
/* ------------------------------------------------------------------- */

uint8_t sb16_encode_irq(int irq)
{
	switch (irq) {
	case 2:  return 0x01;
	case 5:  return 0x02;
	case 7:  return 0x04;
	case 10: return 0x08;
	default: return 0;
	}
}

uint8_t sb16_encode_dma(int dma8, int dma16)
{
	uint8_t v = 0;

	if (dma8 >= 0 && dma8 <= 3) {
		v |= (uint8_t)(1u << dma8);
	}
	if (dma16 >= 5 && dma16 <= 7) {
		v |= (uint8_t)(1u << dma16);
	}
	return v;
}

uint8_t sb16_time_constant(uint32_t hz)
{
	uint32_t tc;

	if (hz == 0) {
		return 0;
	}
	tc = 1000000U / hz;
	if (tc >= 256U) {
		return 0;
	}
	return (uint8_t)(256U - tc);
}

uint8_t sb16_play_mode(uint32_t channels, int is_signed)
{
	uint8_t mode = 0;
	if (channels >= 2) {
		mode |= SB16_DSP_MODE_STEREO;
	}
	if (is_signed) {
		mode |= SB16_DSP_MODE_SIGNED;
	}
	return mode;
}

/* ------------------------------------------------------------------- */
/* Driver state                                                        */
/* ------------------------------------------------------------------- */

typedef struct sb16_dev {
	uint16_t io_base;
	int      irq;
	int      dma8;
	int      dma16;
	uint8_t  dsp_major;
	uint8_t  dsp_minor;

	void    *dma_buf;
	uintptr_t dma_phys;
	size_t   dma_size;

	/* Back-pressure: SB16 single-buffer DMA, so at most one chunk
	 * is in flight.  Block in write() until IRQ confirms drain. */
	volatile uint32_t writes_queued;
	volatile uint32_t plays_done;

	audio_dev_t audio;
} sb16_dev_t;

static sb16_dev_t sb16_singleton;
static int sb16_present;

/* ------------------------------------------------------------------- */
/* DSP I/O                                                             */
/* ------------------------------------------------------------------- */

static void sb16_io_wait(void)
{
	io_wait();
}

static int sb16_dsp_reset(sb16_dev_t *d)
{
	int budget;

	outb((uint16_t)(d->io_base + SB16_REG_RESET), 0x01);
	for (budget = 0; budget < 8; budget++) {
		sb16_io_wait();
	}
	outb((uint16_t)(d->io_base + SB16_REG_RESET), 0x00);
	for (budget = 0; budget < SB16_DSP_RESET_TIMEOUT; budget++) {
		uint8_t status = inb((uint16_t)(d->io_base +
		                                SB16_REG_READ_BUF_STATUS));
		if (status & 0x80) {
			uint8_t v = inb((uint16_t)(d->io_base +
			                           SB16_REG_READ_DATA));
			if (v == 0xAA) {
				return 0;
			}
			return -ENODEV;
		}
	}
	return -ENODEV;
}

static void sb16_dsp_write(sb16_dev_t *d, uint8_t cmd)
{
	int budget;

	for (budget = 0; budget < 0x10000; budget++) {
		uint8_t s = inb((uint16_t)(d->io_base + SB16_REG_WRITE_DATA));
		if ((s & 0x80) == 0) {
			break;
		}
	}
	outb((uint16_t)(d->io_base + SB16_REG_WRITE_DATA), cmd);
}

static uint8_t sb16_dsp_read(sb16_dev_t *d)
{
	int budget;

	for (budget = 0; budget < 0x10000; budget++) {
		uint8_t s = inb((uint16_t)(d->io_base + SB16_REG_READ_BUF_STATUS));
		if (s & 0x80) {
			break;
		}
	}
	return inb((uint16_t)(d->io_base + SB16_REG_READ_DATA));
}

static void sb16_mixer_write(sb16_dev_t *d, uint8_t reg, uint8_t val)
{
	outb((uint16_t)(d->io_base + SB16_REG_MIXER_ADDR), reg);
	outb((uint16_t)(d->io_base + SB16_REG_MIXER_DATA), val);
}

/* ------------------------------------------------------------------- */
/* IRQ                                                                 */
/* ------------------------------------------------------------------- */

static int sb16_irq_handler(unsigned int irq, void *dev_id, void *frame)
{
	sb16_dev_t *d = dev_id;

	(void)irq;
	(void)frame;
	if (d == NULL) {
		return 0;
	}
	/* Reading the 16-bit INTR ack port clears the latched interrupt
	 * line in the DSP — required even for single-shot DMA. */
	(void)inb((uint16_t)(d->io_base + SB16_REG_INTR_ACK_16));
	__atomic_add_fetch(&d->plays_done, 1, __ATOMIC_ACQ_REL);
	return 1;
}

/* ------------------------------------------------------------------- */
/* DMA programming                                                     */
/* ------------------------------------------------------------------- */

static void sb16_program_dma16(sb16_dev_t *d, size_t bytes)
{
	uint32_t phys = (uint32_t)d->dma_phys;
	/*
	 * 16-bit ISA DMA addresses 16-bit words, so the page register holds
	 * bits 16..23 of the byte address but the address-port pair holds
	 * the byte address shifted right by 1.  The hardware reconstructs
	 * the 24-bit byte address as (page << 16) | (addr << 1).
	 */
	uint16_t addr16 = (uint16_t)((phys >> 1) & 0xFFFFU);
	uint16_t count16 = (uint16_t)((bytes / 2) - 1);
	uint8_t  page    = (uint8_t)((phys >> 16) & 0xFFU);
	uint8_t  channel = (uint8_t)(d->dma16 - 4);

	/* Mask channel during programming. */
	outb(SB16_DMA16_MASK_PORT, (uint8_t)(0x04 | channel));
	outb(SB16_DMA16_FF_PORT, 0x00);
	outb(SB16_DMA16_MODE_PORT, (uint8_t)(0x48 | channel));  /* single,read,no-AI */
	outb(SB16_DMA16_ADDR_PORT, (uint8_t)(addr16 & 0xFF));
	outb(SB16_DMA16_ADDR_PORT, (uint8_t)((addr16 >> 8) & 0xFF));
	outb(SB16_DMA16_COUNT_PORT, (uint8_t)(count16 & 0xFF));
	outb(SB16_DMA16_COUNT_PORT, (uint8_t)((count16 >> 8) & 0xFF));
	outb(SB16_DMA16_PAGE_PORT, page);
	outb(SB16_DMA16_MASK_PORT, channel);  /* unmask */
}

/* ------------------------------------------------------------------- */
/* Backend ops                                                         */
/* ------------------------------------------------------------------- */

static int sb16_open(audio_dev_t *adev, int mode)
{
	sb16_dev_t *d = adev->driver_data;
	(void)mode;
	sb16_dsp_write(d, SB16_DSP_SPEAKER_ON);
	return 0;
}

static int sb16_close(audio_dev_t *adev)
{
	sb16_dev_t *d = adev->driver_data;
	sb16_dsp_write(d, SB16_DSP_HALT_DMA16);
	sb16_dsp_write(d, SB16_DSP_SPEAKER_OFF);
	/* Reset back-pressure state — without this the second cat
	 * inherits stale writes_queued and the spin thinks a chunk is
	 * forever in flight. */
	d->writes_queued = 0;
	d->plays_done    = 0;
	return 0;
}

static int sb16_set_params(audio_dev_t *adev, audio_info_t *info)
{
	sb16_dev_t *d = adev->driver_data;
	uint32_t rate = info->play.sample_rate;

	if (rate < 5000)  rate = 5000;
	if (rate > 44100) rate = 44100;

	sb16_dsp_write(d, SB16_DSP_SET_SAMPLE_RATE);
	sb16_dsp_write(d, (uint8_t)((rate >> 8) & 0xFF));
	sb16_dsp_write(d, (uint8_t)(rate & 0xFF));
	info->play.sample_rate = rate;

	{
		uint8_t att = (uint8_t)((info->play.gain * 0xFFu) >> 8);
		sb16_mixer_write(d, SB16_MIXER_MASTER_VOL,    att);
		sb16_mixer_write(d, SB16_MIXER_MASTER_VOL_R,  att);
		sb16_mixer_write(d, SB16_MIXER_VOICE_VOL,     att);
		sb16_mixer_write(d, SB16_MIXER_VOICE_VOL_R,   att);
	}
	return 0;
}

static int sb16_write(audio_dev_t *adev, const void *buf, size_t len)
{
	sb16_dev_t *d = adev->driver_data;
	size_t copy_len;
	uint16_t samples;
	uint8_t mode;

	if (len == 0 || d->dma_buf == NULL) {
		return (int)len;
	}
	copy_len = (len > d->dma_size) ? d->dma_size : len;

	/*
	 * Back-pressure: SB16 single-shot DMA — at most one chunk in
	 * flight.  Block until IRQ has confirmed the previous chunk
	 * drained.  Without this, every write() reprograms the DMA
	 * pointer mid-playback and the DSP races through whatever's in
	 * the (partially overwritten) buffer at full speed.
	 */
	for (;;) {
		uint32_t done = __atomic_load_n(&d->plays_done,
		                                __ATOMIC_ACQUIRE);
		uint32_t in_flight = d->writes_queued - done;
		if (in_flight == 0) {
			break;
		}
		__asm__ volatile("pause");
	}

	memcpy(d->dma_buf, buf, copy_len);
	/* Make the buffer write visible to the DMA controller before we
	 * program the DMA address registers. */
	__sync_synchronize();

	sb16_program_dma16(d, copy_len);

	samples = (uint16_t)((copy_len / 2) - 1);
	mode = sb16_play_mode(d->audio.current.play.channels, 1);

	sb16_dsp_write(d, SB16_DSP_PLAY_16BIT);
	sb16_dsp_write(d, mode);
	sb16_dsp_write(d, (uint8_t)(samples & 0xFF));
	sb16_dsp_write(d, (uint8_t)((samples >> 8) & 0xFF));

	d->writes_queued++;
	return (int)copy_len;
}

static int sb16_drain(audio_dev_t *adev)
{
	(void)adev;
	return 0;
}

static int sb16_flush(audio_dev_t *adev)
{
	sb16_dev_t *d = adev->driver_data;
	sb16_dsp_write(d, SB16_DSP_HALT_DMA16);
	return 0;
}

static void sb16_get_devinfo(audio_dev_t *adev, audio_device_t *out)
{
	sb16_dev_t *d = adev->driver_data;

	memset(out, 0, sizeof(*out));
	snprintf(out->name, sizeof(out->name), "sb16");
	snprintf(out->version, sizeof(out->version), "%u.%u",
	         d->dsp_major, d->dsp_minor);
	snprintf(out->config, sizeof(out->config), "io=0x%x irq=%d dma=%d/%d",
	         d->io_base, d->irq, d->dma8, d->dma16);
}

static int sb16_get_props(audio_dev_t *adev)
{
	(void)adev;
	return AUDIO_PROP_PLAYBACK;
}

static audio_dev_ops_t sb16_ops = {
	.open        = sb16_open,
	.close       = sb16_close,
	.write       = sb16_write,
	.read        = NULL,
	.set_params  = sb16_set_params,
	.drain       = sb16_drain,
	.flush       = sb16_flush,
	.get_devinfo = sb16_get_devinfo,
	.get_props   = sb16_get_props,
};

/* ------------------------------------------------------------------- */
/* Init                                                                */
/* ------------------------------------------------------------------- */

void sb16_init(void)
{
	sb16_dev_t *d = &sb16_singleton;
	vm_page_t *pages;
	size_t npages = (SB16_DMA_BUFFER_SIZE + 4095u) / 4096u;

	if (sb16_present) {
		return;
	}

	memset(d, 0, sizeof(*d));
	d->io_base = SB16_DEFAULT_BASE;
	d->irq     = SB16_DEFAULT_IRQ;
	d->dma8    = SB16_DEFAULT_DMA8;
	d->dma16   = SB16_DEFAULT_DMA16;

	if (sb16_dsp_reset(d) != 0) {
		return;
	}

	sb16_dsp_write(d, SB16_DSP_GET_VERSION);
	d->dsp_major = sb16_dsp_read(d);
	d->dsp_minor = sb16_dsp_read(d);
	if (d->dsp_major < 4) {
		/* SB16 is DSP 4.x; older cards aren't supported here. */
		return;
	}

	sb16_mixer_write(d, SB16_MIXER_RESET, 0x00);
	sb16_mixer_write(d, SB16_MIXER_IRQ_SELECT,
	                 sb16_encode_irq(d->irq));
	sb16_mixer_write(d, SB16_MIXER_DMA_SELECT,
	                 sb16_encode_dma(d->dma8, d->dma16));

	pages = vm_phys_alloc_contiguous_below(npages, SB16_DMA_LIMIT);
	if (pages == NULL) {
		return;
	}
	d->dma_phys = vm_page_to_phys(pages);
	d->dma_buf  = (void *)(d->dma_phys + 0xC0000000U);
	d->dma_size = npages * 4096u;
	memset(d->dma_buf, 0, d->dma_size);

	(void)request_irq((unsigned int)d->irq, sb16_irq_handler,
	                  0, "sb16", d);

	d->audio.ops = &sb16_ops;
	d->audio.driver_data = d;
	snprintf(d->audio.name, sizeof(d->audio.name), "sb16");
	if (audio_register_device(&d->audio) != 0) {
		return;
	}
	sb16_present = 1;
	kprintf("sb16: DSP %u.%u io=0x%x irq=%d dma=%d/%d dma_phys=0x%x\n",
	        d->dsp_major, d->dsp_minor, d->io_base, d->irq,
	        d->dma8, d->dma16, (unsigned int)d->dma_phys);
}
