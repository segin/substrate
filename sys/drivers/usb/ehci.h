/*
 * ehci.h — EHCI (USB 2.0) host controller register + descriptor definitions.
 *
 * EHCI is memory-mapped (BAR0).  The register file splits into a small
 * capability region (length/version/structural params) whose CAPLENGTH byte
 * gives the offset to the operational registers (USBCMD, USBSTS, ports, ...).
 *
 * Transfers use two on-chip data structures, both 32-byte aligned in DMA
 * memory: the Queue Head (QH), one per endpoint, linked into the asynchronous
 * (control/bulk) schedule ring; and the Queue Element Transfer Descriptor
 * (qTD), one per packet-run, chained off a QH.
 */
#ifndef _EHCI_H
#define _EHCI_H

#include <stdint.h>

/* Register the EHCI PCI driver (called from kmain, like uhci_init). */
void ehci_init(void);

/* ---- Capability registers (offsets from the MMIO base) ---- */
#define EHCI_CAP_CAPLENGTH   0x00   /* 8-bit: bytes to the operational regs */
#define EHCI_CAP_HCIVERSION  0x02   /* 16-bit BCD */
#define EHCI_CAP_HCSPARAMS   0x04   /* structural params (N_PORTS in bits 3:0) */
#define EHCI_CAP_HCCPARAMS   0x08   /* capability params (64-bit addressing) */

#define EHCI_HCSPARAMS_N_PORTS(x)   ((x) & 0x0F)
#define EHCI_HCCPARAMS_64BIT        0x01
/* EECP: PCI *config-space* offset of the extended capability list (0 = none). */
#define EHCI_HCCPARAMS_EECP(x)      (((x) >> 8) & 0xFF)

/*
 * ---- Extended capabilities (PCI config space, walked from EECP) ----
 *
 * Unlike xHCI's, EHCI's extended capabilities live in PCI config space and use
 * the standard capability-list encoding: ID in the low byte, config offset of
 * the next entry in the second byte.
 */
#define EHCI_EECP_ID(x)             ((x) & 0xFF)
#define EHCI_EECP_NEXT(x)           (((x) >> 8) & 0xFF)
#define EHCI_ECAP_ID_LEGACY         0x01   /* USB Legacy Support */

/* USB Legacy Support: bit 16 = HC BIOS Owned, bit 24 = HC OS Owned, both
 * addressed as bytes; USBLEGCTLSTS follows at +0x04. */
#define EHCI_LEGSUP_BIOS_SEM        0x02
#define EHCI_LEGSUP_OS_SEM          0x03
#define EHCI_LEGSUP_CTLSTS          0x04

/* ---- Operational registers (offsets from opbase = mmio + CAPLENGTH) ---- */
#define EHCI_OP_USBCMD       0x00
#define EHCI_OP_USBSTS       0x04
#define EHCI_OP_USBINTR      0x08
#define EHCI_OP_FRINDEX      0x0C
#define EHCI_OP_CTRLDSSEG    0x10   /* high 32 bits of 64-bit data structures */
#define EHCI_OP_PERIODICLIST 0x14
#define EHCI_OP_ASYNCLIST    0x18
#define EHCI_OP_CONFIGFLAG   0x40
#define EHCI_OP_PORTSC       0x44   /* array, one 32-bit reg per port */

/* USBCMD bits */
#define EHCI_CMD_RUN         0x00000001
#define EHCI_CMD_HCRESET     0x00000002
#define EHCI_CMD_PSE         0x00000010   /* periodic schedule enable */
#define EHCI_CMD_ASE         0x00000020   /* asynchronous schedule enable */
#define EHCI_CMD_IAAD        0x00000040   /* interrupt on async advance doorbell */
#define EHCI_CMD_ITC_SHIFT   16           /* interrupt threshold control */

/* USBSTS bits */
#define EHCI_STS_HCHALTED    0x00001000
#define EHCI_STS_ASS         0x00008000   /* async schedule status */
#define EHCI_STS_PSS         0x00004000   /* periodic schedule status */
#define EHCI_STS_IAA         0x00000020   /* interrupt on async advance */

/* CONFIGFLAG: route all ports to the EHCI (vs companion controllers) */
#define EHCI_CONFIGFLAG_CF   0x00000001

/* PORTSC bits */
#define EHCI_PORT_CONNECT      0x00000001   /* current connect status */
#define EHCI_PORT_CONNECT_CH   0x00000002   /* connect status change (W1C) */
#define EHCI_PORT_ENABLE       0x00000004   /* port enabled */
#define EHCI_PORT_ENABLE_CH    0x00000008   /* enable change (W1C) */
#define EHCI_PORT_OVERCURRENT  0x00000010
#define EHCI_PORT_OC_CH        0x00000020
#define EHCI_PORT_RESET        0x00000100   /* port reset */
#define EHCI_PORT_LINESTATUS   0x00000C00   /* bits 11:10: D+/D- line state */
#define EHCI_PORT_POWER        0x00001000
#define EHCI_PORT_OWNER        0x00002000   /* 1 = companion owns the port */
#define EHCI_PORT_LS_KSTATE    0x00000400   /* line status = K (low-speed dev) */

/* ---- Link-pointer terminator + type ---- */
#define EHCI_LINK_TERMINATE  0x00000001
#define EHCI_LINK_TYPE_QH    0x00000002   /* Typ field = 01b (queue head) */

/*
 * Queue Element Transfer Descriptor (qTD) — 32 bytes, 32-byte aligned.
 * next/alt_next are physical link pointers; token holds the PID, length,
 * toggle, and status; buffer[] are the (up to 5) page-aligned data pointers.
 */
struct ehci_qtd {
    uint32_t next;              /* next qTD (phys | T) */
    uint32_t alt_next;          /* alternate next qTD (short-packet path) */
    uint32_t token;             /* status/PID/bytes/toggle */
    uint32_t buffer[5];         /* buffer page pointers */
    uint32_t buffer_hi[5];      /* high 32 bits (64-bit only; 0 here) */
} __attribute__((aligned(32)));

/* qTD token fields */
#define EHCI_QTD_STATUS_ACTIVE   0x00000080
#define EHCI_QTD_STATUS_HALTED   0x00000040
#define EHCI_QTD_STATUS_BUFERR   0x00000020
#define EHCI_QTD_STATUS_BABBLE   0x00000010
#define EHCI_QTD_STATUS_XACTERR  0x00000008
#define EHCI_QTD_STATUS_MISSED   0x00000004
#define EHCI_QTD_STATUS_ERRMASK  0x0000007C
#define EHCI_QTD_PID_OUT         (0u << 8)
#define EHCI_QTD_PID_IN          (1u << 8)
#define EHCI_QTD_PID_SETUP       (2u << 8)
#define EHCI_QTD_CERR_SHIFT      10          /* error-retry counter (3) */
#define EHCI_QTD_IOC             0x00008000  /* interrupt on complete */
#define EHCI_QTD_BYTES_SHIFT     16          /* total bytes to transfer */
#define EHCI_QTD_TOGGLE          0x80000000  /* data toggle */
#define EHCI_QTD_BYTES_MAX       0x5000      /* 5 * 4KiB pages */

/*
 * Queue Head (QH) — 48 bytes, 32-byte aligned.  hlink chains QHs in the async
 * ring; the endpoint characteristics/capabilities describe the endpoint; the
 * overlay area (from current_qtd on) is where the controller executes the
 * currently-linked qTD.
 */
struct ehci_qh {
    uint32_t hlink;            /* horizontal link to next QH (phys | Typ | T) */
    uint32_t endp_char;        /* endpoint characteristics */
    uint32_t endp_cap;         /* endpoint capabilities */
    uint32_t current_qtd;      /* current qTD phys */
    /* --- transfer overlay (mirror of struct ehci_qtd) --- */
    uint32_t overlay_next;
    uint32_t overlay_alt_next;
    uint32_t overlay_token;
    uint32_t overlay_buffer[5];
    uint32_t overlay_buffer_hi[5];
} __attribute__((aligned(32)));

/* QH endpoint-characteristics fields */
#define EHCI_QH_ADDR_SHIFT       0           /* device address */
#define EHCI_QH_ENDPT_SHIFT      8           /* endpoint number */
#define EHCI_QH_EPS_SHIFT        12          /* endpoint speed */
#define EHCI_QH_EPS_FULL         (0u << 12)
#define EHCI_QH_EPS_LOW          (1u << 12)
#define EHCI_QH_EPS_HIGH         (2u << 12)
#define EHCI_QH_DTC              0x00004000  /* data-toggle control (use qTD) */
#define EHCI_QH_HEAD             0x00008000  /* head of the async ring */
#define EHCI_QH_MPL_SHIFT        16          /* max packet length */
#define EHCI_QH_CONTROL_EP       0x08000000  /* control endpoint (non-high-speed) */
/* QH endpoint-capabilities fields */
#define EHCI_QH_MULT_SHIFT       30          /* high-bandwidth pipe multiplier */
#define EHCI_QH_MULT_ONE         (1u << 30)

/*
 * Split-transaction fields.  A full- or low-speed device reached through a
 * high-speed hub is not addressed directly: the controller issues a start-split
 * to the hub's transaction translator and later a complete-split, and it needs
 * the hub's address and the port the device hangs off to do that.  Without
 * these (and with EPS left at HIGH) such a device is simply unreachable.
 */
#define EHCI_QH_SMASK_SHIFT      0           /* endp_cap: interrupt schedule mask */
#define EHCI_QH_CMASK_SHIFT      8           /* endp_cap: split completion mask */
#define EHCI_QH_HUBA_SHIFT       16          /* endp_cap: TT hub address (7 bits) */
#define EHCI_QH_PORT_SHIFT       23          /* endp_cap: TT hub port (7 bits) */
#define EHCI_QH_NRL_SHIFT        28          /* endp_char: NAK count reload */

#endif /* _EHCI_H */
