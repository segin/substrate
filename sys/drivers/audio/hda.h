/*
 * hda.h - Intel High Definition Audio controller driver (kernel-internal).
 *
 * Targets the Intel HDA spec rev 1.0 controller: PCI class 0x04, subclass
 * 0x03 (the modern replacement for AC'97).  All registers are accessed
 * through a single MMIO BAR (HDABAR) typically 16 KB in size.
 *
 * Codec communication runs over two ring buffers in coherent system
 * memory: the CORB (Command Output Ring Buffer) carries verbs from host
 * to codec, and the RIRB (Response Input Ring Buffer) returns responses.
 * Each stream (input or output) gets a 32-byte stream descriptor in the
 * register block with its own BDL pointing at the data buffers.
 */

#ifndef _DRIVERS_AUDIO_HDA_H
#define _DRIVERS_AUDIO_HDA_H

#include <stddef.h>
#include <stdint.h>
#include <sys/dma.h>

/* ------------------------------------------------------------------- */
/* Global controller registers (offsets from HDABAR)                   */
/* ------------------------------------------------------------------- */

#define HDA_REG_GCAP             0x00
#define HDA_REG_VMIN             0x02
#define HDA_REG_VMAJ             0x03
#define HDA_REG_OUTPAY           0x04
#define HDA_REG_INPAY            0x06
#define HDA_REG_GCTL             0x08
#define HDA_REG_WAKEEN           0x0C
#define HDA_REG_STATESTS         0x0E
#define HDA_REG_GSTS             0x10
#define HDA_REG_INTCTL           0x20
#define HDA_REG_INTSTS           0x24
#define HDA_REG_WALCLK           0x30
#define HDA_REG_SSYNC            0x38

/* CORB */
#define HDA_REG_CORBLBASE        0x40
#define HDA_REG_CORBUBASE        0x44
#define HDA_REG_CORBWP           0x48
#define HDA_REG_CORBRP           0x4A
#define HDA_REG_CORBCTL          0x4C
#define HDA_REG_CORBSTS          0x4D
#define HDA_REG_CORBSIZE         0x4E

/* RIRB */
#define HDA_REG_RIRBLBASE        0x50
#define HDA_REG_RIRBUBASE        0x54
#define HDA_REG_RIRBWP           0x58
#define HDA_REG_RINTCNT          0x5A
#define HDA_REG_RIRBCTL          0x5C
#define HDA_REG_RIRBSTS          0x5D
#define HDA_REG_RIRBSIZE         0x5E

#define HDA_REG_DPLBASE          0x70
#define HDA_REG_DPUBASE          0x74

/* Stream descriptors live at offset 0x80 + n * 0x20 */
#define HDA_SD_BASE              0x80
#define HDA_SD_STRIDE            0x20

#define HDA_SD_CTL               0x00   /* 24-bit (treat as 32-bit) */
#define HDA_SD_STS               0x03   /* 8-bit */
#define HDA_SD_LPIB              0x04
#define HDA_SD_CBL               0x08
#define HDA_SD_LVI               0x0C
#define HDA_SD_FIFOW             0x0E
#define HDA_SD_FIFOSIZE          0x10
#define HDA_SD_FMT               0x12
#define HDA_SD_BDPL              0x18
#define HDA_SD_BDPU              0x1C

/* GCTL bits */
#define HDA_GCTL_CRST            0x00000001U
#define HDA_GCTL_FCNTRL          0x00000002U
#define HDA_GCTL_UNSOL           0x00000100U

/* INTCTL bits */
#define HDA_INTCTL_GIE           0x80000000U
#define HDA_INTCTL_CIE           0x40000000U

/* CORBCTL bits */
#define HDA_CORBCTL_MEIE         0x01
#define HDA_CORBCTL_RUN          0x02

/* RIRBCTL bits */
#define HDA_RIRBCTL_RINTCTL      0x01
#define HDA_RIRBCTL_RUN          0x02
#define HDA_RIRBCTL_OIC          0x04

/* CORBSIZE / RIRBSIZE encoding (bits 0-1) */
#define HDA_RBSIZE_2             0x00
#define HDA_RBSIZE_16            0x01
#define HDA_RBSIZE_256           0x02

/* Stream descriptor SDCTL bits */
#define HDA_SDCTL_SRST           0x00000001U
#define HDA_SDCTL_RUN            0x00000002U
#define HDA_SDCTL_IOCE           0x00000004U
#define HDA_SDCTL_FEIE           0x00000008U
#define HDA_SDCTL_DEIE           0x00000010U
#define HDA_SDCTL_STREAM_SHIFT   20         /* stream tag in bits 20-23 */

/* Stream status bits (in SDSTS, byte at SD_BASE + 0x03) */
#define HDA_SDSTS_BCIS           0x04
#define HDA_SDSTS_FIFOE          0x08
#define HDA_SDSTS_DESE           0x10

/* ------------------------------------------------------------------- */
/* Verb format                                                         */
/* ------------------------------------------------------------------- */

/* Long form (12-bit verb + 8-bit data): GET_PARAMETER, GET_CONFIG_DEFAULT */
#define HDA_VERB_GET_PARAMETER   0xF00
#define HDA_VERB_GET_CONFIG_DEFAULT 0xF1C
#define HDA_VERB_GET_PIN_WIDGET_CONTROL 0xF07
#define HDA_VERB_GET_CONNECTION_LIST_LEN  0xF0A

/* Short form (4-bit verb + 16-bit data): SET_CONVERTER_FORMAT, etc. */
#define HDA_VERB_SET_CONV_FORMAT 0x200
#define HDA_VERB_SET_CONV_STREAM 0x706
#define HDA_VERB_SET_PIN_WIDGET_CONTROL 0x707
#define HDA_VERB_SET_AMP_GAIN_MUTE       0x300

/* GET_PARAMETER parameter IDs */
#define HDA_PARAM_VENDOR_ID         0x00
#define HDA_PARAM_REVISION_ID       0x02
#define HDA_PARAM_SUBNODE_COUNT     0x04
#define HDA_PARAM_FUNCTION_GROUP_TYPE 0x05
#define HDA_PARAM_AFG_CAPS          0x08
#define HDA_PARAM_AUDIO_WIDGET_CAPS 0x09
#define HDA_PARAM_SUPPORTED_RATES   0x0A
#define HDA_PARAM_SUPPORTED_FORMATS 0x0B
#define HDA_PARAM_PIN_CAPS          0x0C
#define HDA_PARAM_OUTPUT_AMP_CAPS   0x12

/* Function group types (GET_PARAMETER 0x05) */
#define HDA_FGT_AUDIO            0x01
#define HDA_FGT_MODEM            0x02

/* ------------------------------------------------------------------- */
/* Helpers exported for host tests                                     */
/* ------------------------------------------------------------------- */

/*
 * Pack a verb for CORB.  cad is the codec address (0..15), nid is the
 * widget/node ID (0..255), verb is one of the constants above, payload
 * is 8 bits for long-form verbs / 16 bits for short-form (the encoder
 * doesn't differentiate; the caller is responsible for using the
 * correct constant).
 */
uint32_t hda_pack_verb(uint8_t cad, uint8_t nid, uint16_t verb,
                       uint16_t payload);

/*
 * Encode the 16-bit stream format register value from PCM parameters.
 * Returns the SDFMT value: BASE bit 14, MULT bits 11..13, DIV bits 8..10,
 * BITS bits 4..6, CHAN-1 bits 0..3.  Unsupported combinations return 0.
 */
uint16_t hda_encode_format(uint32_t sample_rate, uint32_t bits_per_sample,
                           uint32_t channels);

/*
 * Buffer Descriptor List entry (16 bytes per the spec: u64 buf_phys,
 * u32 length, u32 flags).  IOC bit is 0x01 in flags; bit 1 is reserved.
 */
typedef struct hda_bdl_entry {
	uint64_t buf_phys;
	uint32_t length;
	uint32_t flags;
} __attribute__((packed)) hda_bdl_entry_t;

#define HDA_BDL_F_IOC            0x01

void hda_build_bdl_entry(hda_bdl_entry_t *entry, uint64_t buf_phys,
                         uint32_t length, int ioc);

void hda_init(void);

#endif /* _DRIVERS_AUDIO_HDA_H */
