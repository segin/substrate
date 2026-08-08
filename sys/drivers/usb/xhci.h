/*
 * xhci.h — xHCI (USB 3.x) host controller register, TRB, and context defs.
 *
 * xHCI is memory-mapped (BAR0) and splits into four register regions: the
 * capability region (fixed offsets), the operational region (at CAPLENGTH),
 * the runtime region (at RTSOFF, holds the interrupter/event-ring registers),
 * and the doorbell array (at DBOFF).
 *
 * Transfers are driven through rings of 16-byte TRBs: a command ring (host ->
 * controller), an event ring (controller -> host, completions), and one
 * transfer ring per active endpoint.  Devices are described by 32/64-byte
 * contexts (slot + endpoint) reached through the Device Context Base Address
 * Array (DCBAA), indexed by the controller-assigned slot id.
 */
#ifndef _XHCI_H
#define _XHCI_H

#include <stdint.h>

void xhci_init(void);

/* ---- Capability registers (offsets from MMIO base) ---- */
#define XHCI_CAP_CAPLENGTH   0x00   /* 8-bit: bytes to operational regs */
#define XHCI_CAP_HCIVERSION  0x02
#define XHCI_CAP_HCSPARAMS1  0x04   /* MaxSlots[7:0], MaxIntrs, MaxPorts[31:24] */
#define XHCI_CAP_HCSPARAMS2  0x08   /* IST, ERST Max, Max Scratchpad Buffers */
#define XHCI_CAP_HCCPARAMS1  0x10   /* bit2 = CSZ (64-byte contexts) */
#define XHCI_CAP_DBOFF       0x14   /* doorbell array offset */
#define XHCI_CAP_RTSOFF      0x18   /* runtime register-space offset */

#define XHCI_HCS1_MAXSLOTS(x)  ((x) & 0xFF)
#define XHCI_HCS1_MAXPORTS(x)  (((x) >> 24) & 0xFF)
/*
 * Max Scratchpad Buffers is a split field: the low five bits live at 31:27 and
 * the high five at 25:21, so it cannot be extracted with a single shift+mask.
 * Same expression FreeBSD and NetBSD use.  Value is a count of PAGESIZE pages
 * the controller wants handed to it; see xhci_alloc_scratchpad().
 */
#define XHCI_HCS2_SPB_MAX(x)   ((((x) >> 16) & 0x3E0) | (((x) >> 27) & 0x1F))
/* PAGESIZE (operational 0x08): bit n set => the xHC's page size is 2^(n+12). */
#define XHCI_PAGESIZE_MASK     0x0000FFFFu
#define XHCI_HCC1_CSZ          0x04
#define XHCI_HCC1_XECP(x)      (((x) >> 16) & 0xFFFF)  /* ext-cap ptr, in dwords */

/*
 * ---- Extended capabilities ----
 *
 * A singly-linked list in the capability region starting at HCCPARAMS1's XECP
 * field.  Each entry's low byte is its ID and the next byte the dword distance
 * to the following entry (0 = end of list).
 */
#define XHCI_XECP_ID(x)        ((x) & 0xFF)
#define XHCI_XECP_NEXT(x)      (((x) >> 8) & 0xFF)
#define XHCI_ECAP_ID_LEGACY    0x01   /* USB Legacy Support */
#define XHCI_ECAP_ID_PROTOCOL  0x02   /* Supported Protocol */

/*
 * Supported Protocol capability (xHCI 1.2 s7.2).  Dword 0 carries the protocol
 * revision in its top half, dword 1 the ASCII name ("USB "), and dword 2 the
 * range of root-hub ports this block describes.  Without it the Protocol Speed
 * IDs in PORTSC can only be interpreted against the default table, which is
 * merely the *default* -- a controller may redefine them per protocol.
 */
#define XHCI_XECP_SP_NAME_USB  0x20425355u  /* "USB " little-endian */
#define XHCI_XECP_SP_MAJOR(w0) (((w0) >> 24) & 0xFF)
#define XHCI_XECP_SP_PORT_OFF(w2) ((w2) & 0xFF)         /* 1-based */
#define XHCI_XECP_SP_PORT_CNT(w2) (((w2) >> 8) & 0xFF)

/*
 * USB Legacy Support capability: USBLEGSUP at +0x00 (bit 16 = HC BIOS Owned,
 * bit 24 = HC OS Owned — addressed as bytes so the semaphores can be written
 * without disturbing the ID/next fields), USBLEGCTLSTS at +0x04.
 */
#define XHCI_LEGSUP_BIOS_SEM   0x02
#define XHCI_LEGSUP_OS_SEM     0x03
#define XHCI_LEGCTLSTS         0x04
/* RsvdP fields of USBLEGCTLSTS: preserved, everything else (the SMI enables)
 * is cleared.  Bits 31:29 are the RW1C SMI status bits. */
#define XHCI_LEGCTL_RSVD       ((0x7u << 1) | (0xFFu << 5) | (0x7u << 17))
#define XHCI_LEGCTL_SMI_EVENTS (0x7u << 29)

/*
 * ---- Intel chipset port routing (PCI config space) ----
 *
 * Intel PCHs (Panther/Lynx/Wildcat Point, BayTrail, Broadwell) come out of
 * reset with the shared USB2 ports wired to the companion EHCI controller.
 * These registers hand them to the xHCI; the *PRM masks report which ports the
 * chipset actually lets software switch.
 */
#define XHCI_INTEL_XUSB2PR     0xD0   /* USB2 port routing */
#define XHCI_INTEL_USB2PRM     0xD4   /* USB2 port routing mask */
#define XHCI_INTEL_USB3_PSSEN  0xD8   /* USB3 port SuperSpeed enable */
#define XHCI_INTEL_USB3PRM     0xDC   /* USB3 port routing mask */

/* ---- Operational registers (from opbase = mmio + CAPLENGTH) ---- */
#define XHCI_OP_USBCMD       0x00
#define XHCI_OP_USBSTS       0x04
#define XHCI_OP_PAGESIZE     0x08
#define XHCI_OP_DNCTRL       0x14
#define XHCI_OP_CRCR         0x18   /* command ring control (64-bit) */
#define XHCI_OP_DCBAAP       0x30   /* device context base array ptr (64-bit) */
#define XHCI_OP_CONFIG       0x38   /* MaxSlotsEn[7:0] */
#define XHCI_OP_PORTSC(p)    (0x400 + (uint32_t)(p) * 0x10)  /* p is 0-based */

#define XHCI_CMD_RUN         0x00000001
#define XHCI_CMD_HCRST       0x00000002
#define XHCI_CMD_INTE        0x00000004
#define XHCI_STS_HCH         0x00000001   /* HC halted */
#define XHCI_STS_HSE         0x00000004   /* host system error */
#define XHCI_STS_HCE         0x00001000   /* host controller error */
#define XHCI_STS_CNR         0x00000800   /* controller not ready */
#define XHCI_CRCR_RCS        0x00000001   /* ring cycle state */
#define XHCI_CRCR_CA         0x00000004   /* command abort (RW1S) */
#define XHCI_CRCR_CRR        0x00000008   /* command ring running (RO) */

/* PORTSC */
#define XHCI_PORT_CCS        0x00000001   /* current connect status */
#define XHCI_PORT_PED        0x00000002   /* port enabled/disabled */
#define XHCI_PORT_PR         0x00000010   /* port reset */
#define XHCI_PORT_PP         0x00000200   /* port power */
#define XHCI_PORT_PLS_SHIFT  5
#define XHCI_PORT_PLS_MASK   0x000001E0
#define XHCI_PORT_SPEED_SHIFT 10
#define XHCI_PORT_SPEED_MASK 0x00003C00
#define XHCI_PORT_CSC        0x00020000   /* connect status change (W1C) */
#define XHCI_PORT_WRC        0x00080000   /* warm port reset change (W1C) */
#define XHCI_PORT_PRC        0x00200000   /* port reset change (W1C) */
#define XHCI_PORT_WPR        0x80000000U  /* warm port reset (USB3 only) */
/*
 * Bits that must be cleared out of a PORTSC value before writing it back.
 *
 * PORTSC cannot be read-modify-written naively: several of its bits act on a
 * write of 1 rather than storing one.  The critical one is PED (bit 1), which
 * xHCI 1.2 Table 5-27 defines as RW1CS:
 *
 *   "Ports may only be enabled by the xHC.  Software cannot enable a port by
 *    writing a '1' to this flag.  A port may be disabled by software writing a
 *    '1' to this flag. [...] PED shall automatically be [...] set to '1' when
 *    PR transitions from '1' to '0' after a successful reset."
 *
 * So a writeback that preserves PED disables the port -- and PED is guaranteed
 * to be set at exactly the moment we acknowledge a completed reset, which is
 * where it bit us.  Footnote 82 adds that writing PED and PR together is
 * undefined behaviour, so both have to go.
 *
 * The value is the same one FreeBSD and NetBSD both use (XHCI_PS_CLEAR in
 * their xhcireg.h): bits 8:0 (CCS, PED, OCA, PR, PLS), 23:16 (LWS and every
 * change bit), and 31 (WPR).  PP, PIC and the wake enables are outside it and
 * so survive a writeback, which is what we want.
 */
#define XHCI_PORT_CLEAR      0x80FF01FFU

/*
 * Post-reset recovery.  USB 2.0 s7.1.7.5 gives a device 10ms (TRSTRCY) after
 * the reset ends before it has to answer anything; talking to it sooner is
 * simply out of spec.  FreeBSD budgets double the minimum
 * (USB_PORT_RESET_RECOVERY, usb.h) and so do we.  Likewise a device is
 * allowed 2ms to take up a new address; FreeBSD waits 10
 * (USB_SET_ADDRESS_SETTLE).
 */
#define XHCI_PORT_RESET_RECOVERY_MS  20
#define XHCI_SET_ADDRESS_SETTLE_MS   10

/* ---- Runtime registers (from rtbase = mmio + RTSOFF) ---- */
#define XHCI_RT_MFINDEX      0x00
#define XHCI_RT_IR0          0x20   /* interrupter 0 */
#define XHCI_IR_IMAN         0x00
#define XHCI_IR_IMOD         0x04
#define XHCI_IR_ERSTSZ       0x08
#define XHCI_IR_ERSTBA       0x10   /* 64-bit */
#define XHCI_IR_ERDP         0x18   /* 64-bit */
#define XHCI_IMAN_IP         0x00000001
#define XHCI_IMAN_IE         0x00000002
#define XHCI_ERDP_EHB        0x00000008   /* event handler busy (W1C) */

/* ---- TRB (16 bytes) ---- */
struct xhci_trb {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed));

/*
 * Control-word flags, from Table 6-22.  Bit 1 carries two different fields
 * depending on the TRB type -- Toggle Cycle in a Link TRB, Evaluate Next TRB
 * in a transfer TRB -- hence the two names for one value.  XHCI_TRB_ENT was
 * 0x10, which is the Chain bit; harmless while nothing used it, and a silent
 * mis-set of CH the moment a multi-TRB TD (a data stage over 64K, or
 * isochronous) reached for it. [P3-04]
 */
#define XHCI_TRB_CYCLE       0x00000001
#define XHCI_TRB_TC          0x00000002   /* toggle cycle (link TRB) */
#define XHCI_TRB_ENT         0x00000002   /* evaluate next TRB (transfer TRB) */
#define XHCI_TRB_ISP         0x00000004   /* interrupt on short packet */
#define XHCI_TRB_CH          0x00000010   /* chain bit */
#define XHCI_TRB_IOC         0x00000020   /* interrupt on complete */
#define XHCI_TRB_IDT         0x00000040   /* immediate data */
#define XHCI_TRB_TYPE_SHIFT  10
#define XHCI_TRB_TYPE(t)     ((uint32_t)(t) << XHCI_TRB_TYPE_SHIFT)
#define XHCI_TRB_GET_TYPE(c) (((c) >> XHCI_TRB_TYPE_SHIFT) & 0x3F)
#define XHCI_TRB_GET_CC(s)   (((s) >> 24) & 0xFF)   /* completion code */
#define XHCI_TRB_GET_SLOT(c) (((c) >> 24) & 0xFF)
#define XHCI_TRB_GET_XLEN(s) ((s) & 0x00FFFFFF)     /* transfer-event residue */

/* TRB types */
#define TRB_NORMAL           1
#define TRB_SETUP            2
#define TRB_DATA             3
#define TRB_STATUS           4
#define TRB_LINK             6
#define TRB_ENABLE_SLOT      9
#define TRB_DISABLE_SLOT     10
#define TRB_ADDRESS_DEVICE   11
#define TRB_CONFIGURE_EP     12
#define TRB_EVAL_CONTEXT     13
#define TRB_RESET_ENDPOINT   14
#define TRB_STOP_ENDPOINT    15
#define TRB_SET_TR_DEQUEUE   16
#define TRB_NOOP_CMD         23
#define TRB_TRANSFER_EVENT   32
#define TRB_CMD_COMPLETE     33
#define TRB_PORT_STATUS_EVENT 34

/* Control-transfer TRT (transfer type) field, in Setup TRB control */
#define TRB_TRT_NO_DATA      (0u << 16)
#define TRB_TRT_OUT          (2u << 16)
#define TRB_TRT_IN           (3u << 16)
#define TRB_DIR_IN           (1u << 16)   /* Data/Status stage direction */

#define XHCI_CC_SUCCESS      1
#define XHCI_CC_BABBLE       3
#define XHCI_CC_USB_TX_ERROR 4
#define XHCI_CC_TRB_ERROR    5
#define XHCI_CC_STALL        6
#define XHCI_CC_SHORT_PACKET 13
/*
 * Completion codes an *abort* produces on the command ring.  Neither is the
 * result of a command we are waiting on: xHCI 1.2 s4.6.1.2 says aborting
 * completes the command that was executing with Command Aborted, and then
 * always posts Command Ring Stopped for the ring itself.  Both land on the
 * event ring after the abort, where a matcher that keys on TRB type alone
 * would take the first of them as the *next* command's result. [R-01]
 */
#define XHCI_CC_CMD_RING_STOPPED 0x18
#define XHCI_CC_CMD_ABORTED      0x19

/*
 * Completion codes that leave the endpoint Halted.  The controller stops
 * processing that endpoint's transfer ring entirely until a Reset Endpoint
 * command runs, so without recovery the FIRST such error silently kills the
 * endpoint for the rest of the session and every later transfer just times
 * out.  A multi-slot card reader reaches this on ordinary traffic -- an
 * empty slot answers some SCSI commands with a STALL.
 */
#define XHCI_CC_HALTS_EP(cc) \
    ((cc) == XHCI_CC_BABBLE || (cc) == XHCI_CC_USB_TX_ERROR || \
     (cc) == XHCI_CC_STALL)

/* ---- Contexts (32-byte here; CSZ=1 doubles to 64 -- we handle both via a
 * runtime stride).  Slot context + endpoint context layouts. ---- */
struct xhci_slot_ctx {
    uint32_t field[8];   /* [0]=route/speed/ctx-entries, [1]=root port, ... */
};
struct xhci_ep_ctx {
    uint32_t field[8];   /* [0]=ep state, [1]=type/maxpkt, [2..3]=deq ptr, ... */
};

/* Slot context field[0] */
#define XHCI_SLOT_CTX_ENTRIES_SHIFT 27
#define XHCI_SLOT_SPEED_SHIFT 20
/*
 * Slot context fields that identify a device behind a hub (xHCI 1.1 s6.2.2).
 * Root Hub Port Number alone names a port, not a device: without the Route
 * String the controller aims every command at whatever is attached directly to
 * that port, which for a downstream device is the hub in front of it.  MTT and
 * the TT fields tell it to drive a low/full-speed device through the
 * transaction translator of the high-speed hub above it.
 */
#define XHCI_SLOT_ROUTE_MASK  0x000FFFFFu   /* dword 0, bits 19:0 */
#define XHCI_SLOT_MTT         (1u << 25)    /* dword 0: multi-TT hub */
#define XHCI_SLOT_HUB         (1u << 26)    /* dword 0: this device is a hub */
#define XHCI_SLOT_NPORTS_SHIFT 24           /* dword 1, bits 31:24 */
#define XHCI_SLOT_TT_HUB_SHIFT 0            /* dword 2, bits 7:0 */
#define XHCI_SLOT_TT_PORT_SHIFT 8           /* dword 2, bits 15:8 */
/*
 * TT Think Time, dword 2 bits 17:16 (Table 6-6).  Required whenever Hub=1 and
 * Speed=High-Speed (s6.2.2.2); the encoding is the same 0..3 scale as the
 * wHubCharacteristics sub-field it comes from, 0 meaning "at most 8 FS bit
 * times of inter-transaction gap" and 3 meaning 32.  Left at 0 the controller
 * schedules a hub's TT transactions more tightly than the hub can service,
 * which shows up as intermittent transaction errors on the low/full-speed
 * devices below it.
 */
#define XHCI_SLOT_TTT_SHIFT   16
/*
 * Slot context Speed (dword 0, bits 23:20).  Deprecated in xHCI 1.2 but live
 * on every 1.0/1.1 part, and it is the *device's* speed, not the speed the
 * root port trained at -- those differ for anything behind a hub.  The
 * encoding is the default Protocol Speed ID assignment, which does not match
 * substrate's USB_SPEED_*, hence xhci_slot_speed().
 */
#define XHCI_SLOT_SPEED_FULL  1
#define XHCI_SLOT_SPEED_LOW   2
#define XHCI_SLOT_SPEED_HIGH  3
#define XHCI_SLOT_SPEED_SUPER 4
/* Slot context field[1] */
#define XHCI_SLOT_RHPORT_SHIFT 16
/* EP context field[1] */
#define XHCI_EP_TYPE_SHIFT   3
#define XHCI_EP_MPS_SHIFT    16
#define XHCI_EP_CERR_SHIFT   1
#define EP_TYPE_CONTROL      4
#define EP_TYPE_BULK_OUT     2
#define EP_TYPE_BULK_IN      6
#define EP_TYPE_INT_OUT      3
#define EP_TYPE_INT_IN       7
/*
 * EP context field[4]: Average TRB Length [15:0], Max ESIT Payload Lo [31:16].
 *
 * Not optional.  xHCI 1.2 Table 6-9: the Average TRB Length "value of this
 * field shall be greater than '0'.  The xHC shall use this parameter to
 * calculate system bus bandwidth requirements", and s4.14.1.1 warns that a
 * Configure Endpoint Command "may be rejected by the xHC with a Bandwidth
 * Error" when the numbers do not add up.  The same table requires the value 8
 * for control endpoints specifically.  Max ESIT Payload is the bytes moved per
 * service interval and is only meaningful for periodic endpoints.
 */
#define XHCI_EP_AVG_TRB_LEN(x)  ((uint32_t)(x) & 0xFFFFu)
#define XHCI_EP_MAX_ESIT_LO(x)  (((uint32_t)(x) & 0xFFFFu) << 16)
#define XHCI_EP_AVG_TRB_CTRL    8      /* spec-mandated for control EPs */
#define XHCI_EP_AVG_TRB_BULK    4096   /* a page, as both BSDs use */
/* EP context field[0] */
#define XHCI_EP_STATE_MASK   0x7
/* EP context field[0] stream fields */
#define XHCI_EP_MAXPSTREAMS_SHIFT 10          /* MaxPStreams (array = 2^(k+1)) */
#define XHCI_EP_INTERVAL_SHIFT 16             /* EP context DW0 bits 23:16 */
#define XHCI_EP_LSA          (1u << 15)       /* Linear Stream Array */
/* Stream context (16B): qwSctx0 = DCS(bit0) | SCT(bits1-3) | TR_DQ_PTR */
#define XHCI_SCTX_SCT_PRIM_TR (1u << 1)       /* SCT = primary transfer ring */
/* Doorbell: DB target = DCI[7:0] | (stream ID << 16) */
#define XHCI_DB_STREAM_SHIFT 16

/* Input control context: drop flags [0], add flags [1] */
struct xhci_input_ctrl_ctx {
    uint32_t drop_flags;
    uint32_t add_flags;
    uint32_t rsvd[6];
};

#endif /* _XHCI_H */
