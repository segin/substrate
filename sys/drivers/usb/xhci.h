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
#define XHCI_CAP_HCSPARAMS2  0x08
#define XHCI_CAP_HCCPARAMS1  0x10   /* bit2 = CSZ (64-byte contexts) */
#define XHCI_CAP_DBOFF       0x14   /* doorbell array offset */
#define XHCI_CAP_RTSOFF      0x18   /* runtime register-space offset */

#define XHCI_HCS1_MAXSLOTS(x)  ((x) & 0xFF)
#define XHCI_HCS1_MAXPORTS(x)  (((x) >> 24) & 0xFF)
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
#define XHCI_STS_CNR         0x00000800   /* controller not ready */
#define XHCI_CRCR_RCS        0x00000001   /* ring cycle state */

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
#define XHCI_PORT_PRC        0x00200000   /* port reset change (W1C) */
/* Bits that are RW1CS/change flags -- preserve them cleared when we RMW. */
#define XHCI_PORT_CHANGE_MASK 0x00FE0000

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

#define XHCI_TRB_CYCLE       0x00000001
#define XHCI_TRB_TC          0x00000002   /* toggle cycle (link TRB) */
#define XHCI_TRB_ENT         0x00000010   /* evaluate next TRB / chain-ish */
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
#define XHCI_CC_SHORT_PACKET 13

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
