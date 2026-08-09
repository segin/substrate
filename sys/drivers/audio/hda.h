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

/*
 * SDnCTL is three bytes at +0x00 with SDnSTS immediately after at +0x03,
 * so a 32-bit access to SDnCTL straddles the status byte.  Address the
 * two control bytes we care about separately: SRST/RUN/IOCE/FEIE/DEIE
 * live in byte 0, and the stream tag in byte 2.  Spec 4.5.6 asks for byte
 * access from interrupt context specifically for this reason.
 */
#define HDA_SD_CTL               0x00   /* byte: SRST/RUN/IOCE/FEIE/DEIE */
#define HDA_SD_CTL2              0x02   /* byte: stripe/TP/DIR/stream tag */
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
/*
 * Bits 0..29 are Stream Interrupt Enable, one per stream descriptor index.
 * SDCTL.IOCE only decides whether a completed buffer sets SDSTS.BCIS; it is
 * this bit that lets that status reach the PCI interrupt line.  With it
 * clear the driver still sees BCIS if it polls, but no interrupt ever
 * fires -- so a cyclic stream replays its ring forever and any writer
 * blocks once the FIFO fills.
 */
#define HDA_INTCTL_SIE(n)        (1U << ((n) & 0x1F))

/*
 * INTSTS bits.  Same positions as INTCTL's enables, but a different
 * register with different semantics -- all of these are READ ONLY (spec
 * rev 1.0a table 15).  They cannot be acknowledged here; they clear only
 * when the underlying source does (SDnSTS, RIRBSTS, STATESTS).
 */
#define HDA_INTSTS_GIS           0x80000000U
#define HDA_INTSTS_CIS           0x40000000U
#define HDA_INTSTS_SIS(n)        (1U << ((n) & 0x1F))

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
#define HDA_SDCTL2_STRM_SHIFT    4          /* ... i.e. bits 7-4 of byte 2 */
#define HDA_SDCTL2_STRM_MASK     0xF0

/* Stream status bits (in SDSTS, byte at SD_BASE + 0x03) */
#define HDA_SDSTS_BCIS           0x04
#define HDA_SDSTS_FIFOE          0x08
#define HDA_SDSTS_DESE           0x10

/* ------------------------------------------------------------------- */
/* Verb format                                                         */
/* ------------------------------------------------------------------- */

/*
 * Two payload widths exist, and picking the wrong one puts the payload on
 * top of the command bits.  Only commands 2h, 3h, Ah and Bh take a 16-bit
 * payload (spelled 0xN00 below); every other command is 12 bits wide with
 * an 8-bit payload.  hda_verb_is_short() is the single place that decides.
 */

/* 4-bit command + 16-bit payload */
#define HDA_VERB_SET_CONV_FORMAT 0x200
#define HDA_VERB_SET_AMP_GAIN_MUTE       0x300
#define HDA_VERB_GET_CONV_FORMAT 0xA00
#define HDA_VERB_GET_AMP_GAIN_MUTE       0xB00

/* 12-bit command + 8-bit payload */
#define HDA_VERB_SET_CONN_SELECT 0x701
#define HDA_VERB_SET_POWER_STATE 0x705
#define HDA_VERB_SET_CONV_STREAM 0x706
#define HDA_VERB_SET_PIN_WIDGET_CONTROL 0x707
#define HDA_VERB_SET_EAPD_BTL    0x70C
#define HDA_VERB_GET_PARAMETER   0xF00
#define HDA_VERB_GET_CONN_LIST   0xF02
#define HDA_VERB_GET_PIN_WIDGET_CONTROL 0xF07
#define HDA_VERB_GET_EAPD_BTL    0xF0C
#define HDA_VERB_GET_CONFIG_DEFAULT 0xF1C

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
#define HDA_PARAM_INPUT_AMP_CAPS    0x0D
#define HDA_PARAM_CONN_LIST_LEN     0x0E
#define HDA_PARAM_OUTPUT_AMP_CAPS   0x12

/* Function group types (GET_PARAMETER 0x05) */
#define HDA_FGT_AUDIO            0x01
#define HDA_FGT_MODEM            0x02

/* SUBNODE_COUNT response: start node in 23:16, count in 7:0 */
#define HDA_SUBNODE_START(r)     (((r) >> 16) & 0xFF)
#define HDA_SUBNODE_COUNT(r)     ((r) & 0xFF)

/* AUDIO_WIDGET_CAPS (GET_PARAMETER 0x09) */
#define HDA_AW_TYPE(caps)        (((caps) >> 20) & 0x0F)
#define HDA_AW_TYPE_DAC          0x0   /* audio output converter */
#define HDA_AW_TYPE_ADC          0x1
#define HDA_AW_TYPE_MIXER        0x2
#define HDA_AW_TYPE_SELECTOR     0x3
#define HDA_AW_TYPE_PIN          0x4
#define HDA_AW_IN_AMP            0x00000002U  /* input amp present */
#define HDA_AW_OUT_AMP           0x00000004U  /* output amp present */
/*
 * Amp Param Override: the widget carries its own AMP_CAPS.  When clear,
 * the widget's own 0x0D/0x12 response is meaningless and the audio
 * function group's defaults apply instead (spec rev 1.0a 7.3.4.6).
 */
#define HDA_AW_AMP_OVERRIDE      0x00000008U
#define HDA_AW_CONN_LIST         0x00000100U  /* connection list present */

/* SET_AMP_GAIN_MUTE payload */
#define HDA_AMP_SET_OUTPUT       0x8000
#define HDA_AMP_SET_INPUT        0x4000
#define HDA_AMP_SET_LEFT         0x2000
#define HDA_AMP_SET_RIGHT        0x1000
#define HDA_AMP_SET_INDEX_SHIFT  8
#define HDA_AMP_MUTE             0x0080
#define HDA_AMP_GAIN_MASK        0x007F

/*
 * AMP_CAPS (GET_PARAMETER 0x0D input amp / 0x12 output amp).  Field
 * layout, spec rev 1.0a figure 91:
 *
 *    31       30:23   22:16      15     14:8       7      6:0
 *    MuteCap  Rsvd    StepSize   Rsvd   NumSteps   Rsvd   Offset
 *
 * NUMSTEPS used to be spelled (c) >> 16, which is StepSize -- the gain
 * granularity in 0.25 dB units, not the number of steps.  Offset is the
 * step that corresponds to 0 dB.
 */
#define HDA_AMPCAP_OFFSET(c)     ((c) & 0x7F)          /* the 0 dB setting */
#define HDA_AMPCAP_NUMSTEPS(c)   (((c) >> 8) & 0x7F)
#define HDA_AMPCAP_STEPSIZE(c)   (((c) >> 16) & 0x7F)
#define HDA_AMPCAP_MUTE_CAP(c)   (((c) >> 31) & 0x1)

/* SET_PIN_WIDGET_CONTROL payload */
#define HDA_PIN_CTRL_HP_ENABLE   0x80
#define HDA_PIN_CTRL_OUT_ENABLE  0x40
#define HDA_PIN_CTRL_IN_ENABLE   0x20

/* PIN_CAPS (GET_PARAMETER 0x0C) */
#define HDA_PINCAP_OUTPUT        0x00000010U
#define HDA_PINCAP_EAPD          0x00010000U

/* CONFIG_DEFAULT (verb 0xF1C) */
#define HDA_CONFIG_PORTCONN(cd)  (((cd) >> 30) & 0x3)
#define HDA_PORTCONN_NONE        0x1   /* no physical connection: skip */
#define HDA_CONFIG_DEVICE(cd)    (((cd) >> 20) & 0xF)
#define HDA_DEVICE_LINE_OUT      0x0
#define HDA_DEVICE_SPEAKER       0x1
#define HDA_DEVICE_HP_OUT        0x2

/* CONN_LIST_LEN (GET_PARAMETER 0x0E) */
#define HDA_CONNLIST_LEN(r)      ((r) & 0x7F)
#define HDA_CONNLIST_LONG        0x80

/* SET_POWER_STATE payload */
#define HDA_PS_D0                0x00
#define HDA_PS_D3                0x03

/*
 * SET_EAPD_BTL_ENABLE payload.  Three independent bits share this one
 * byte, so it has to be read-modify-written rather than assigned.
 */
#define HDA_EAPD_BTL             0x01
#define HDA_EAPD_ENABLE          0x02
#define HDA_EAPD_LR_SWAP         0x04
#define HDA_EAPD_MASK            0x07

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
 * Encode the 16-bit stream format register value (SDnFMT) from PCM
 * parameters: BASE bit 14, MULT bits 13..11, DIV bits 10..8, BITS bits
 * 6..4, CHAN-1 bits 3..0 (spec rev 1.0a section 3.3.41, table 40).
 *
 * Stores the encoding through *out and returns 0, or returns -EINVAL and
 * leaves *out untouched when the rate / sample width / channel count has
 * no legal encoding.
 *
 * The status cannot be folded into the return value the way it used to
 * be: 48 kHz 8-bit mono is BASE=0 MULT=0 DIV=0 BITS=0 CHAN=0, i.e. the
 * all-zero encoding, so a 0 return is a legal format and was
 * indistinguishable from "unsupported" -- which made set_params reject
 * that one combination outright.
 */
int hda_encode_format(uint32_t sample_rate, uint32_t bits_per_sample,
                      uint32_t channels, uint16_t *out);

/* SDnFMT field shifts (spec table 40). */
#define HDA_FMT_BASE_SHIFT       14
#define HDA_FMT_MULT_SHIFT       11
#define HDA_FMT_DIV_SHIFT        8
#define HDA_FMT_BITS_SHIFT       4

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
