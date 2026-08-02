/*
 * ahci.c - AHCI (Advanced Host Controller Interface) Driver
 *
 * Full SATA host controller driver implementing:
 * - PCI-based AHCI controller detection and initialization
 * - Per-port command list, FIS receive, and command table DMA setup
 * - SATA device detection via port signature (COMRESET)
 * - ATA IDENTIFY for disk geometry
 * - DMA read/write via ATA READ/WRITE DMA EXT commands
 * - Block device registration for SATA disks (blkdev_t)
 * - SCSI transport registration for SATAPI devices (scsi_link_t)
 * - Integration with Substrate driver model (PCI bus type)
 *
 * Architecture: polling mode (no IRQ). Single command slot (slot 0).
 *
 * References:
 * - Serial ATA AHCI 1.3.1 Specification
 * - ATA/ATAPI Command Set (ACS-3)
 */

#include <string.h>
#include <stdio.h>
#include <kern/console.h>
#include <kern/pci.h>
#include <kern/device.h>
#include <kern/driver.h>
#include <kern/resource.h>
#include <kern/time.h>
#include <kern/sched.h>
#include <sys/dma.h>
#include <sys/errno.h>
#include <sys/lock.h>
#include <sys/irq.h>

/* Pause-spin iterations to await command completion before yielding the CPU
 * in ahci_port_issue_cmd() — long enough to catch a fast (KVM/emulated)
 * completion at low latency, short enough that a real-latency transfer yields
 * (cmd_lock is a mutex, so a contender sleeps rather than spins). */
#define AHCI_POLL_SPIN_LIMIT   2048

/* When the completion IRQ is wired up, spin only this briefly for an
 * instant completion (avoids a context switch for sub-microsecond transfers)
 * before sleeping until ahci_irq() wakes us -- vs AHCI_POLL_SPIN_LIMIT MMIO
 * reads (each a VM-exit under KVM) burning the CPU for the whole DMA. */
#define AHCI_IRQ_FASTSPIN      64

/*
 * [AHCI-10] Largest bounce buffer a single command will ask for.
 *
 * max_sectors used to advertise 4 MiB, so every large I/O called
 * pmm_alloc_contiguous for a 1024-page PHYSICALLY CONTIGUOUS run, memset it,
 * DMA'd, then memcpy'd out -- and on allocation failure the whole request
 * failed, with no fallback to a smaller chunk.  Once memory is fragmented a
 * contiguous 4 MiB run is exactly what you cannot get, so the root disk began
 * failing reads instead of degrading.  128 KiB is 32 contiguous pages, which
 * an allocator under pressure can still satisfy; the read/write loops already
 * chunk, so the only cost is more commands per request.
 */
#define AHCI_MAX_XFER_BYTES  (128u * 1024u)

/* COMRESET assert time.  SATA requires PxSCTL.DET=1 be held for at least
 * 1 ms; get_uptime_ms() advances in 4 ms ticks at HZ=250, so a deadline of
 * "+1" can expire on the very next tick after ~0 real time.  Ask for enough
 * ticks that at least 1 ms has provably elapsed. */
#define AHCI_COMRESET_HOLD_MS  10

/* [AHCI-15] How long to wait after asserting PxCMD.SUD for the PHY to come
 * up.  The spec's own deadline is 10 ms, but that assumes the platters are
 * already turning; on a controller with staggered spin-up the whole point is
 * that they are not, and a cold 7200 rpm drive needs on the order of a second
 * before it answers.  Still bounded, so a dead port costs one second once. */
#define AHCI_SPINUP_TIMEOUT_MS   1000

/* [AHCI-15] How long to wait for a link asked to leave Partial/Slumber to
 * report Active.  The transition is a PHY handshake measured in microseconds;
 * this is a generous ceiling, not an expected duration. */
#define AHCI_ICC_TIMEOUT_MS      100

/* ahci_port_issue_cmd() return for "the command failed AND the port could not
 * be quiesced".  Distinct from -1 because the HBA may still own the command
 * slot: the caller must leave every buffer it handed us alone rather than
 * returning it to the allocator. */
#define AHCI_CMD_WEDGED        (-2)

/* Per-port interrupt-enable mask: command-completion FIS interrupts plus the
 * fatal error bits the wait loop already checks (so an error wakes the waiter
 * immediately rather than after the re-check timeout). */
#define AHCI_PORT_IE_MASK  (HBA_PXIS_DHRS | HBA_PXIS_PSS | HBA_PXIS_DSS | \
                            HBA_PXIS_SDBS | HBA_PXIS_FATAL)
#include <drivers/storage/ahci/ahci.h>
#include <drivers/storage/blkdev.h>
#include <drivers/storage/scsi/scsi.h>

/*
 * ============================================================
 * Per-Port Driver State
 * ============================================================
 */

#define AHCI_PORT_TYPE_NONE     0
#define AHCI_PORT_TYPE_SATA     1
#define AHCI_PORT_TYPE_SATAPI   2
#define AHCI_PORT_TYPE_SEMB     3
#define AHCI_PORT_TYPE_PM       4

struct ahci_controller;

typedef struct ahci_port {
    int                port_num;
    int                type;        /* AHCI_PORT_TYPE_* */
    hba_port_t        *regs;        /* MMIO port registers */
    struct ahci_controller *ctrl;  /* owning HBA (for irq_ready) */
    char               io_wait;     /* completion wait channel; ahci_irq() wakes &io_wait */

    /* DMA areas */
    hba_cmd_header_t  *cmd_list;    /* Command list (32 entries, 1024 bytes) */
    dma_addr_t         cmd_list_dma;
    hba_fis_t         *fis_recv;    /* FIS receive area (256 bytes) */
    dma_addr_t         fis_recv_dma;
    hba_cmd_table_t   *cmd_table;   /* Command table for slot 0 */
    dma_addr_t         cmd_table_dma;

    /* Disk identity (from IDENTIFY) */
    uint64_t           sectors;
    uint32_t           sector_size;
    char               model[41];
    char               serial[21];
    char               firmware[9];
    int                lba48;

    /* [AHCI-19] IDENTIFY PACKET DEVICE word 62 bit 15: the device requires
     * the DMADIR bit in the PACKET command's Feature field to know which way
     * the data is going.  Devices that do NOT set it may abort a PACKET that
     * carries DMADIR, so it must not be asserted blindly. */
    int                dmadir;

    /* [AHCI-18] IDENTIFY word 82 bit 5 / word 85 bit 5: the device has a
     * volatile write cache, and it is enabled.  Only then is there anything
     * for FLUSH CACHE to push to media. */
    int                write_cache;

    /* Block device (for SATA disks) */
    blkdev_t           bdev;
    int                disk_index;  /* sata0, sata1, ... */

    /* Serialises the single shared command slot / cmd_table against
     * concurrent callers.  The AHCI engine here uses one command slot and
     * one command table per port; with kernel preemption enabled two threads
     * (e.g. two multi-sector reads from exec/ldd) can otherwise enter the
     * command path at once and clobber each other's FIS/PRDT, producing
     * corrupted or zero-filled reads.  A mutex (not a spinlock): the block
     * cache drops its bio spinlock before calling ->read (blkdev.c), so the
     * I/O path is sleepable, and ahci_port_issue_cmd() yields the CPU while
     * waiting for completion instead of pinning it (a spinlock would
     * disable preemption across the whole DMA, freezing the scheduler). */
    mutex_t            cmd_lock;

    /* PxIS bits the ISR has already consumed.  ahci_irq() must RW1C the
     * port's interrupt status to stop the interrupt reasserting, which
     * destroys exactly the FATAL bits the command waiter polls for -- so
     * the ISR ORs what it cleared in here and the waiter tests both.
     * Written by the ISR, cleared by the waiter before each command. */
    volatile uint32_t  pending_is;

    /* Set when the port could not be quiesced after a failed command.  The
     * HBA may still own the command slot and DMA into the buffers we handed
     * it, so nothing may be reclaimed and no further command may be issued
     * on this port. */
    int                wedged;
} ahci_port_t;

/*
 * ============================================================
 * Controller State
 * ============================================================
 */
typedef struct ahci_controller {
    hba_mem_t      *abar;           /* MMIO base (ioremap'd BAR5) */
    pci_device_t   *pci_dev;
    uint32_t        cap;            /* Cached CAP register */
    uint32_t        pi;             /* Ports Implemented bitmap */
    int             num_ports;      /* Number of ports (from CAP) */
    int             num_cmd_slots;  /* Number of command slots (from CAP) */
    ahci_port_t     ports[AHCI_MAX_PORTS];
    int             port_count;     /* Number of active ports */
    int             disk_count;     /* Running sata disk index */
    size_t          bar_sz;         /* bytes actually ioremap'd for BAR5 */
    uint8_t         irq;            /* PCI interrupt line */
    volatile int    irq_ready;      /* completion IRQ hooked + enabled */
} ahci_controller_t;

/*
 * Substrate supports multiple AHCI HBAs (e.g. several `-device ich9-ahci`
 * in qemu).  Each PCI controller gets its own slot so a second HBA does
 * not clobber the first's port / blkdev state, and disk names come from a
 * single global counter so disks enumerate sata0, sata1, sata2, ... across
 * all controllers instead of every HBA restarting at sata0 (which would
 * collide on /dev/storage/sata0 and wedge the root mount).
 */
#define AHCI_MAX_CONTROLLERS 4
static ahci_controller_t ahci_ctrls[AHCI_MAX_CONTROLLERS];
static int ahci_ctrl_count;     /* number of attached HBAs */
static int ahci_disk_count;     /* global running sata%d index */
static int ahci_initialized;

static void *ahci_dma_bounce_alloc(size_t size, dma_addr_t *dma_handle) {
    return dma_alloc_coherent(size, dma_handle);
}

static void ahci_dma_bounce_free(void *buf, size_t size) {
    dma_free_coherent(buf, size);
}

/* SCSI link for SATAPI devices */
static scsi_link_t ahci_scsi_link;
static int ahci_scsi_registered;

/*
 * ============================================================
 * Timing Helpers
 * ============================================================
 */

static inline uint64_t ahci_time_ms(void) {
    return (uint64_t)get_uptime_ms();
}

/*
 * ============================================================
 * Port Command Engine Control
 * ============================================================
 */

/* Stop command engine on a port */
static int ahci_port_stop(hba_port_t *port) {
    uint64_t deadline;

    /* Clear ST (command engine) */
    port->cmd &= ~HBA_PXCMD_ST;

    /* Wait for CR to clear (command list not running) */
    deadline = ahci_time_ms() + 500;
    while (port->cmd & HBA_PXCMD_CR) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port stop timeout (CR stuck)\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    /* Clear FRE (FIS receive) */
    port->cmd &= ~HBA_PXCMD_FRE;

    /* Wait for FR to clear */
    deadline = ahci_time_ms() + 500;
    while (port->cmd & HBA_PXCMD_FR) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port stop timeout (FR stuck)\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    return 0;
}

/* Start command engine on a port.  Returns 0 on success, -1 if CR never
 * cleared -- in which case NOTHING was enabled and the caller must not
 * assume the port is usable. */
static int ahci_port_start(hba_port_t *port) {
    uint64_t deadline;

    /* Wait until CR clears before setting ST */
    deadline = ahci_time_ms() + 500;
    while (port->cmd & HBA_PXCMD_CR) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port start timeout (CR stuck)\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    port->cmd |= HBA_PXCMD_FRE;
    port->cmd |= HBA_PXCMD_ST;

    /*
     * Wait for the HBA to acknowledge that the command engine is running
     * before anyone issues into it.  Setting ST only *requests* the start;
     * PxCMD.CR is the HBA saying it has begun fetching from the command list.
     *
     * Issuing before that is what made the first command on each port return
     * nothing on a Lenovo C460: IDENTIFY completed clean (DRDY, no DRQ, no
     * ERR, PxSERR and PxIS zero) having moved 0 of 512 bytes, while a retry
     * 50 ms later succeeded and returned the real model and capacity.  The
     * retry is what identified this as a start-up race rather than anything
     * about the PIO protocol.
     */
    deadline = ahci_time_ms() + 500;
    while (!(port->cmd & HBA_PXCMD_CR)) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port start timeout (CR never set)\n");
            return -1;
        }
        __asm__ volatile("pause");
    }
    return 0;
}

/*
 * Quiesce a port after a failed command, so the HBA provably no longer owns
 * the command slot or the buffers its PRDT points at.
 *
 * A plain stop/start is not enough.  When ahci_port_stop() gives up with
 * "CR stuck" the engine is still running, it never even reaches the FRE
 * clear, and ahci_port_start() then bails at its own CR wait having enabled
 * nothing -- leaving ST=0, CR=1, FRE=1, the slot outstanding, and the caller
 * free to hand the buffers back to the allocator.  AHCI 1.3.1 s10.4.2 says
 * the only recovery when CR will not clear is a port reset, so do that.
 *
 * Returns 0 when the port is quiesced and restarted (safe to reclaim
 * buffers), -1 when it is not (caller must reclaim nothing).
 */
static int ahci_port_recover(ahci_port_t *ap) {
    hba_port_t *port = ap->regs;
    uint64_t deadline;

    /* Clear latched errors first so the restart does not immediately trip. */
    port->serr = port->serr;
    port->is   = port->is;

    if (ahci_port_stop(port) == 0)
        return ahci_port_start(port);

    /* Stop failed: COMRESET.  DET=1 asserts the reset; it must be held for
     * at least 1 ms, then cleared, after which DET should read 3 (device
     * present, PHY communication established). */
    port->sctl = (port->sctl & ~HBA_PXSCTL_DET_MASK) | HBA_PXSCTL_DET_INIT;
    deadline = ahci_time_ms() + AHCI_COMRESET_HOLD_MS;
    while (ahci_time_ms() < deadline)
        __asm__ volatile("pause");
    port->sctl &= ~(uint32_t)HBA_PXSCTL_DET_MASK;

    deadline = ahci_time_ms() + 500;
    while ((port->ssts & HBA_PXSSTS_DET_MASK) != HBA_PXSSTS_DET_ACTIVE) {
        if (ahci_time_ms() > deadline) {
            kprint("ahci: port reset failed; port left wedged\n");
            return -1;
        }
        __asm__ volatile("pause");
    }

    port->serr = port->serr;
    port->is   = port->is;

    /* The reset dropped the engine, so CR must be clear now; if the stop
     * still will not take, give up rather than pretend. */
    if (ahci_port_stop(port) != 0) {
        kprint("ahci: engine still running after reset; port left wedged\n");
        return -1;
    }
    return ahci_port_start(port);
}

/*
 * ============================================================
 * Port Initialization
 * ============================================================
 */

static int ahci_port_alloc(ahci_port_t *ap) {
    /* Allocate Command List (1024 bytes, 1024-byte aligned) */
    ap->cmd_list = dma_alloc_coherent(sizeof(hba_cmd_header_t) * AHCI_MAX_CMD_SLOTS,
                                       &ap->cmd_list_dma);
    if (!ap->cmd_list) {
        kprint("ahci: failed to alloc command list\n");
        return -1;
    }

    /* Allocate FIS Receive Area (256 bytes, 256-byte aligned) */
    ap->fis_recv = dma_alloc_coherent(sizeof(hba_fis_t), &ap->fis_recv_dma);
    if (!ap->fis_recv) {
        kprint("ahci: failed to alloc FIS receive\n");
        dma_free_coherent(ap->cmd_list,
                          sizeof(hba_cmd_header_t) * AHCI_MAX_CMD_SLOTS);
        ap->cmd_list = NULL;
        return -1;
    }

    /* Allocate Command Table for slot 0 (128-byte aligned) */
    ap->cmd_table = dma_alloc_coherent(sizeof(hba_cmd_table_t), &ap->cmd_table_dma);
    if (!ap->cmd_table) {
        kprint("ahci: failed to alloc command table\n");
        dma_free_coherent(ap->fis_recv, sizeof(hba_fis_t));
        dma_free_coherent(ap->cmd_list,
                          sizeof(hba_cmd_header_t) * AHCI_MAX_CMD_SLOTS);
        ap->fis_recv = NULL;
        ap->cmd_list = NULL;
        return -1;
    }

    /* Point command header slot 0 at the command table */
    ap->cmd_list[AHCI_CMD_SLOT].ctba = (uint32_t)ap->cmd_table_dma;
    ap->cmd_list[AHCI_CMD_SLOT].ctbau = 0; /* 32-bit addresses */
    ap->cmd_list[AHCI_CMD_SLOT].prdtl = AHCI_MAX_PRDT_ENTRIES;

    return 0;
}

/*
 * [AHCI-11] Undo ahci_port_init.  It allocates three DMA regions and STARTS
 * the port's command engine, but the caller only decides whether a device is
 * present afterwards -- and the no-device path was a bare `continue`, which
 * leaked all three allocations and left ST/FRE set with the HBA pointing at
 * memory nothing tracked any more.
 */
static void ahci_port_teardown(ahci_port_t *ap) {
    if (!ap)
        return;

    /* Stop the engine first: the HBA must not be looking at these regions
     * when they go back to the allocator. */
    if (ap->regs)
        (void)ahci_port_stop(ap->regs);

    if (ap->cmd_table) {
        dma_free_coherent(ap->cmd_table, sizeof(hba_cmd_table_t));
        ap->cmd_table = NULL;
    }
    if (ap->fis_recv) {
        dma_free_coherent(ap->fis_recv, sizeof(hba_fis_t));
        ap->fis_recv = NULL;
    }
    if (ap->cmd_list) {
        dma_free_coherent(ap->cmd_list,
                          sizeof(hba_cmd_header_t) * AHCI_MAX_CMD_SLOTS);
        ap->cmd_list = NULL;
    }
    ap->type = AHCI_PORT_TYPE_NONE;
}

static int ahci_port_init(ahci_port_t *ap, hba_port_t *port_regs, int port_num) {
    ap->port_num = port_num;
    ap->regs = port_regs;
    ap->type = AHCI_PORT_TYPE_NONE;
    mutex_init(&ap->cmd_lock, "ahci_cmd");

    /* Stop command engine */
    if (ahci_port_stop(port_regs) < 0) {
        return -1;
    }

    /* Perform COMRESET to ensure clean port state */
    port_regs->sctl = (port_regs->sctl & ~HBA_PXSCTL_DET_MASK) | HBA_PXSCTL_DET_INIT;
    /* COMRESET must be asserted for at least 1ms (SATA spec) */
    {
        uint64_t comreset_end = ahci_time_ms() + 2;
        while (ahci_time_ms() < comreset_end)
            __asm__ volatile("pause");
    }
    port_regs->sctl &= ~HBA_PXSCTL_DET_MASK;  /* Clear DET to re-establish */

    /* Wait for device detection (DET=3 means phy communication established) */
    {
        uint64_t detect_deadline = ahci_time_ms() + AHCI_TIMEOUT_SPINUP;
        uint64_t empty_deadline = ahci_time_ms() + 200; /* 200ms fast-fail for empty ports */
        int empty_checked = 0;
        while ((port_regs->ssts & HBA_PXSSTS_DET_MASK) != HBA_PXSSTS_DET_ACTIVE) {
            if (ahci_time_ms() > detect_deadline) {
                break;  /* No device or slow device — will be caught by detect */
            }
            /* Fast-fail: if DET is still 0 after 200ms, no device attached */
            if (!empty_checked && ahci_time_ms() > empty_deadline) {
                empty_checked = 1;
                if ((port_regs->ssts & HBA_PXSSTS_DET_MASK) == 0) {
                    break;  /* No device responding at all */
                }
            }
            __asm__ volatile("pause");
        }
    }

    /* Clear errors accumulated during COMRESET */
    port_regs->serr = 0xFFFFFFFF;

    /* Allocate DMA memory */
    if (ahci_port_alloc(ap) < 0) {
        return -1;
    }

    /* Program CLB and FB registers */
    port_regs->clb  = (uint32_t)ap->cmd_list_dma;
    port_regs->clbu = 0;
    port_regs->fb   = (uint32_t)ap->fis_recv_dma;
    port_regs->fbu  = 0;

    /* Clear pending interrupts and errors */
    port_regs->serr = 0xFFFFFFFF;   /* Write-1-to-clear all error bits */
    port_regs->is   = 0xFFFFFFFF;   /* Write-1-to-clear all interrupt bits */

    /* Start command engine */
    ahci_port_start(port_regs);

    return 0;
}

/*
 * ============================================================
 * Device Detection
 * ============================================================
 */

/*
 * [AHCI-15] Spin the device up and bring the link out of a low-power state.
 *
 * Two independent things used to be assumed rather than done:
 *
 *  1. On an HBA that reports CAP.SSS (staggered spin-up), every port comes
 *     out of reset with PxCMD.SUD CLEAR and the attached drive not spinning.
 *     PxSSTS.DET therefore reads 0 -- "no device, no phy" -- and the probe
 *     loop's cheap pre-check skipped the port before anything could set SUD.
 *     On such a controller NO disks were ever found.  Software has to assert
 *     SUD (and PxCMD.POD where the port reports cold presence detection) and
 *     then wait for the PHY to come up.
 *
 *  2. Detection required PxSSTS.IPM == 1 (Active) exactly.  A link that has
 *     drifted into Partial (2) or Slumber (6) -- which the HBA may do on its
 *     own with aggressive link power management, and which a device may be
 *     left in by firmware -- read as "no device".  PxCMD.ICC exists precisely
 *     to request the Active state, and was never written.
 *
 * Returns 1 if the port has a live PHY afterwards, 0 if it does not.
 */
static int ahci_port_spinup(ahci_port_t *ap, uint32_t cap) {
    hba_port_t *port = ap->regs;
    uint64_t deadline;
    uint32_t cmd;

    cmd = port->cmd;

    if (cap & HBA_CAP_SSS) {
        /* Power the receptacle first where the port supports it, then
         * request spin-up.  Both are sticky bits in PxCMD. */
        if (cmd & HBA_PXCMD_CPD)
            cmd |= HBA_PXCMD_POD;
        cmd |= HBA_PXCMD_SUD;
        port->cmd = cmd;

        /*
         * AHCI 1.3.1 s10.1.1: after SUD is set, DET should read 1 or 3
         * within 10 ms.  A drive that must actually spin its platters can
         * take far longer to establish comms, so allow a second -- still
         * short next to the COMRESET paths above.
         */
        deadline = ahci_time_ms() + AHCI_SPINUP_TIMEOUT_MS;
        while ((port->ssts & HBA_PXSSTS_DET_MASK) != HBA_PXSSTS_DET_ACTIVE) {
            if (ahci_time_ms() > deadline)
                break;
            __asm__ volatile("pause");
        }
    }

    if ((port->ssts & HBA_PXSSTS_DET_MASK) != HBA_PXSSTS_DET_ACTIVE)
        return 0;

    /* Link is up.  If it is parked in Partial/Slumber, ask for Active. */
    if ((port->ssts & HBA_PXSSTS_IPM_MASK) != HBA_PXSSTS_IPM_ACTIVE) {
        cmd = port->cmd;
        cmd &= ~(uint32_t)HBA_PXCMD_ICC_MASK;
        cmd |= HBA_PXCMD_ICC_ACTIVE;
        port->cmd = cmd;

        deadline = ahci_time_ms() + AHCI_ICC_TIMEOUT_MS;
        while ((port->ssts & HBA_PXSSTS_IPM_MASK) != HBA_PXSSTS_IPM_ACTIVE) {
            if (ahci_time_ms() > deadline)
                break;
            __asm__ volatile("pause");
        }
    }

    /* Spinning up or waking the link latches PHY-change errors; clear them
     * so the first command does not trip on stale status. */
    port->serr = port->serr;

    return (port->ssts & HBA_PXSSTS_DET_MASK) == HBA_PXSSTS_DET_ACTIVE;
}

static int ahci_port_detect_device(ahci_port_t *ap) {
    uint32_t ssts = ap->regs->ssts;
    uint8_t  det = ssts & HBA_PXSSTS_DET_MASK;
    uint8_t  ipm = (ssts & HBA_PXSSTS_IPM_MASK) >> 8;

    /*
     * [AHCI-15] ipm == 0 means the PHY really has no device.  Anything else
     * (Active, Partial, Slumber, DevSleep) means one is attached;
     * ahci_port_spinup() has already asked for Active, and a device that
     * will not leave Slumber is still better addressed than declared absent.
     */
    if (det != HBA_PXSSTS_DET_ACTIVE || ipm == 0x00) {
        ap->type = AHCI_PORT_TYPE_NONE;
        return 0;   /* No device or not active */
    }

    switch (ap->regs->sig) {
    case SATA_SIG_ATA:
        ap->type = AHCI_PORT_TYPE_SATA;
        break;
    case SATA_SIG_ATAPI:
        ap->type = AHCI_PORT_TYPE_SATAPI;
        break;
    case SATA_SIG_SEMB:
        ap->type = AHCI_PORT_TYPE_SEMB;
        break;
    case SATA_SIG_PM:
        ap->type = AHCI_PORT_TYPE_PM;
        break;
    default:
        ap->type = AHCI_PORT_TYPE_SATA;  /* Assume SATA for unknown sigs */
        break;
    }

    return 1;   /* Device present */
}

/*
 * ============================================================
 * Command Execution (Polling)
 * ============================================================
 */

/*
 * Completion interrupt handler.  Shared-IRQ safe: returns 0 (not handled) when
 * this HBA has no pending interrupt, so other devices on the line still get a
 * look.  For each port flagged in HBA.IS, clear that port's latched PxIS and
 * wake any waiter parked in ahci_port_issue_cmd().  Clear order is PxIS then
 * HBA.IS (both write-1-to-clear), per AHCI 1.3.1 section 10.7.2.
 */
static int ahci_irq(unsigned int irq, void *dev_id, void *frame) {
    (void)irq; (void)frame;
    ahci_controller_t *ctrl = (ahci_controller_t *)dev_id;
    if (!ctrl || !ctrl->abar) return 0;

    uint32_t is = ctrl->abar->is;
    if (!is) return 0;                      /* not ours (shared line) */

    for (int p = 0; p < AHCI_MAX_PORTS; p++) {
        if (!(is & (1U << p))) continue;
        ahci_port_t *ap = &ctrl->ports[p];
        if (!ap->regs) continue;
        /* RW1C the port status -- but record what we cleared first.  The
         * command waiter's only error check is PxIS.FATAL, and clearing it
         * here without saving it leaves the waiter unable to see that the
         * command failed (PxCI is not cleared on a task-file error either),
         * so it spins to the full timeout on every recoverable device error. */
        uint32_t pis = ap->regs->is;
        ap->pending_is |= pis;
        ap->regs->is = pis;
        sched_wakeup(&ap->io_wait);          /* wake the command waiter, if any */
    }

    ctrl->abar->is = is;                     /* clear serviced HBA.IS bits (RW1C) */
    return 1;
}

/*
 * Arm completion interrupts on every active port and hook the HBA up via MSI,
 * then flip irq_ready so ahci_port_issue_cmd() switches from busy-polling to
 * sleeping until ahci_irq() wakes it.  Called once per controller after the
 * ports are probed (the probe itself runs polled, before this).  On any failure
 * the controller simply stays in polled mode.
 *
 * MSI (not the legacy INTx line) is used deliberately: MSI delivers to a
 * PRIVATE, edge-triggered LAPIC vector, so AHCI completions never share the
 * 8259 INTx line with other PCI devices.  Sharing INTx with a device that
 * asserts a level interrupt nothing acks (e.g. a polled NIC) storms the CPU;
 * a private MSI vector cannot.
 */
static void ahci_enable_interrupts(ahci_controller_t *ctrl) {
    hba_mem_t *abar = ctrl->abar;
    char buf[80];
    int vec;

    for (int p = 0; p < AHCI_MAX_PORTS; p++) {
        ahci_port_t *ap = &ctrl->ports[p];
        if (ap->type == AHCI_PORT_TYPE_NONE || !ap->regs) continue;
        ap->regs->is = ap->regs->is;         /* clear stale PxIS */
        ap->regs->ie = AHCI_PORT_IE_MASK;    /* enable completion/error IRQs */
    }

    abar->is = abar->is;                     /* clear stale HBA.IS */

    vec = irq_alloc_vector();
    if (vec < 0) {
        kprint("ahci: no free MSI vector; staying in polled mode\n");
        return;
    }
    if (request_irq((unsigned int)vec, ahci_irq, IRQF_SHARED, "ahci", ctrl) != 0) {
        irq_free_vector(vec);
        kprint("ahci: request_irq failed; staying in polled mode\n");
        return;
    }
    if (pci_enable_msi(ctrl->pci_dev, (uint8_t)vec) != 0) {
        free_irq((unsigned int)vec, ctrl);
        irq_free_vector(vec);
        kprint("ahci: HBA has no MSI capability; staying in polled mode\n");
        return;
    }

    ctrl->irq = (uint8_t)vec;
    abar->ghc |= HBA_GHC_IE;                 /* global interrupt enable */
    ctrl->irq_ready = 1;

    snprintf(buf, sizeof(buf),
             "ahci: MSI vector 0x%x wired; completion-driven I/O\n",
             (unsigned)vec);
    kprint(buf);
}

/*
 * Issue a command in slot 0 and wait for completion.
 * The caller must have filled in cmd_list[0] and cmd_table before calling.
 */
static int ahci_port_issue_cmd(ahci_port_t *ap, uint32_t timeout_ms) {
    hba_port_t *port = ap->regs;
    uint64_t deadline;

    /* A port we failed to quiesce may still have the HBA writing into the
     * previous command's buffers.  Issuing another command would hand it a
     * second set. */
    if (ap->wedged)
        return AHCI_CMD_WEDGED;

    /* Clear interrupt status, and the copy the ISR accumulates for us. */
    ap->pending_is = 0;
    port->is = 0xFFFFFFFF;

    /*
     * [AHCI-17] The command list, command table and PRDT are ordinary
     * (non-volatile) memory; PxCI is volatile MMIO.  Nothing stopped the
     * compiler from sinking those descriptor stores past this one, handing
     * the HBA a slot whose table it has not finished writing.  It has not
     * bitten at -O2 because the intervening volatile MMIO accesses happen to
     * order it, but that is luck, not a guarantee -- LTO or -O3 could
     * reorder it.  Make the dependency explicit.
     */
    __sync_synchronize();

    /* Issue command in slot 0 */
    port->ci = (1U << AHCI_CMD_SLOT);

    /* Poll for completion */
    deadline = ahci_time_ms() + timeout_ms;
    unsigned spins = 0;
    while (1) {
        /* Check if slot is done */
        if ((port->ci & (1U << AHCI_CMD_SLOT)) == 0) {
            break;
        }

        /* Check for fatal errors.  Test the ISR's accumulator too: once the
         * completion IRQ is live, ahci_irq() RW1Cs PxIS before we get here,
         * which would otherwise erase the very bits we are looking for and
         * leave us spinning until the timeout. */
        if ((port->is | ap->pending_is) & HBA_PXIS_FATAL) {
            char buf[96];
            snprintf(buf, sizeof(buf),
                     "ahci: port %d fatal error IS=0x%08x SERR=0x%08x TFD=0x%08x\n",
                     ap->port_num, port->is | ap->pending_is, port->serr,
                     port->tfd);
            kprint(buf);
            if (ahci_port_recover(ap) != 0) {
                ap->wedged = 1;
                return AHCI_CMD_WEDGED;
            }
            return -1;
        }

        /* Timeout check */
        if (ahci_time_ms() > deadline) {
            char buf[64];
            snprintf(buf, sizeof(buf),
                     "ahci: port %d command timeout\n", ap->port_num);
            kprint(buf);
            /*
             * The command is still outstanding (CI slot set): the HBA may
             * yet DMA into the command table / PRDT / data buffer.  If we
             * just returned, the caller would reclaim and reuse slot 0's
             * cmd_table for the next command while the controller finishes
             * the old transfer -> DMA into recycled memory (DRV-05).  Stop
             * the command engine to make the HBA relinquish the slot, clear
             * the latched errors, and restart, mirroring the fatal-error
             * path above (IDE quiesces on timeout the same way).
             *
             * ahci_port_recover() reports whether that actually worked.  If
             * it did not, the slot is still outstanding and the caller must
             * not give the buffers back to the allocator.
             */
            if (ahci_port_recover(ap) != 0) {
                ap->wedged = 1;
                return AHCI_CMD_WEDGED;
            }
            return -1;
        }

        /*
         * Completion wait.  cmd_lock is a mutex held across this wait, so a
         * contending thread sleeps on it rather than spinning.
         *
         * Once the completion IRQ is wired up (ap->ctrl->irq_ready), spin only
         * briefly for an instant completion, then sched_sleep_until() on this
         * port's channel: ahci_irq() clears PxIS and wakes us the moment the
         * DMA finishes, so the CPU idles (hlt) instead of burning thousands of
         * MMIO polls (each a VM-exit under KVM) for the whole transfer.  The
         * short re-check deadline is a lost-wakeup safety net -- if the IRQ
         * fires in the window between the CI check above and blocking, we wake
         * on the deadline and re-check CI rather than hang.
         *
         * Before IRQs are enabled (early boot / disk probe) fall back to the
         * original tight-spin-then-yield poll so those paths still work. */
        if (ap->ctrl && ap->ctrl->irq_ready && spins >= AHCI_IRQ_FASTSPIN) {
            uint32_t hz = get_hz();
            uint64_t d = get_ticks() + (hz >= 100 ? hz / 100u : 1u);  /* ~10 ms */
            sched_sleep_until(&ap->io_wait, d);
        } else if (spins < AHCI_POLL_SPIN_LIMIT) {
            spins++;
            __asm__ volatile("pause");
        } else {
            sched_yield();
        }
    }

    /* Check Task File for errors */
    if (port->tfd & (HBA_PXTFD_ERR | HBA_PXTFD_BSY)) {
        return -1;
    }

    return 0;
}

/*
 * Build a Register H2D FIS for an ATA command.
 */
static void ahci_build_h2d_fis(struct fis_reg_h2d *fis, uint8_t command,
                                 uint64_t lba, uint32_t count, uint8_t device) {
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;   /* C bit = 1 → this is a command FIS */
    fis->command  = command;
    fis->device   = device;

    fis->lba0 = (uint8_t)(lba);
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);

    fis->countl = (uint8_t)(count);
    fis->counth = (uint8_t)(count >> 8);
}

/*
 * Execute a single ATA DMA command (read or write) on a port.
 * data_virt must be a kernel virtual address suitable for DMA.
 */
static int ahci_ata_dma_cmd(ahci_port_t *ap, uint8_t command,
                             uint64_t lba, uint32_t sector_count,
                             void *data_virt, int is_write) {
    hba_cmd_header_t *hdr = &ap->cmd_list[AHCI_CMD_SLOT];
    hba_cmd_table_t *tbl  = ap->cmd_table;
    struct fis_reg_h2d *fis;
    void *dma_buf;
    dma_addr_t data_dma;
    uint32_t byte_count;

    if (ap->sector_size != 0 && sector_count > SIZE_MAX / ap->sector_size) {
        return -1;
    }
    byte_count = sector_count * ap->sector_size;

    /* Serialise the shared command slot / table against concurrent callers
     * (kernel preemption can otherwise interleave two reads, corrupting both).
     * Held across issue + completion poll + data copy. */
    mutex_lock(&ap->cmd_lock);

    /* Clear command table */
    memset(tbl, 0, sizeof(hba_cmd_table_t));

    /* Build command FIS */
    fis = (struct fis_reg_h2d *)tbl->cfis;
    ahci_build_h2d_fis(fis, command, lba, sector_count, 0x40); /* LBA mode */

    dma_buf = NULL;
    data_dma = 0;
    if (byte_count > 0) {
        /*
         * DMA into a coherent bounce buffer instead of arbitrary kernel/user
         * virtual memory. The generic dma_map_single() path is currently a
         * direct phys translation and does not provide scatter-gather.
         */
        dma_buf = ahci_dma_bounce_alloc(byte_count, &data_dma);
        if (!dma_buf) {
            mutex_unlock(&ap->cmd_lock);
            return -1;
        }
        if (is_write) {
            memcpy(dma_buf, data_virt, byte_count);
        }
        tbl->prdt[0].dba  = (uint32_t)data_dma;
        tbl->prdt[0].dbau = 0;
        tbl->prdt[0].dbc  = byte_count - 1;  /* 0-based */
        tbl->prdt[0].i    = 0;
    }

    /* Setup command header */
    hdr->cfl   = sizeof(struct fis_reg_h2d) / 4;  /* 5 DWORDs */
    hdr->a     = 0;
    hdr->w     = is_write ? 1 : 0;
    /*
     * [AHCI-21] P (Prefetchable) and C (Clear Busy upon R_OK) were both set on
     * every command.  C is the damaging one: AHCI 1.3.1 s4.2.2 says the HBA
     * shall clear PxTFD.STS.BSY and the PxCI bit "after transmitting this FIS
     * and receiving R_OK" -- that is, as soon as the COMMAND is acknowledged,
     * before the data phase has run.  The driver then polls CI, sees it clear,
     * reads PRDBC (still zero, nothing has moved yet) and reports a short
     * transfer of 0 bytes.  On a Lenovo C460 that made every IDENTIFY return
     * nothing, so both SATA disks came up as "0 sectors, no partition table".
     * QEMU completes the whole command regardless of C, which is why this only
     * ever appeared on hardware.
     *
     * C belongs on a Soft Reset and nothing else -- FreeBSD sets it in exactly
     * one place, alongside AHCI_CMD_RESET (ahci.c:1702), and this driver has no
     * soft-reset path at all.  P is only set for ATAPI by FreeBSD (ahci.c:1689)
     * and Linux; leave it clear here too.
     */
    hdr->p     = 0;
    hdr->r     = 0;
    hdr->b     = 0;
    hdr->c     = 0;
    hdr->pmp   = 0;
    /*
     * [AHCI-16] prdtl was hardcoded to 1 even when byte_count == 0, leaving
     * the HBA a PRDT entry that is entirely zero: dba = 0 and dbc = 0, which
     * on AHCI means a ONE-byte transfer to physical address 0.  A
     * zero-length command must advertise no PRDT entries at all.  Currently
     * unreachable (every caller passes sector_count >= 1) but it is what
     * blocks issuing a FLUSH CACHE, which carries no data.
     */
    hdr->prdtl = (byte_count > 0) ? 1 : 0;
    hdr->prdbc = 0;

    /* Issue and wait */
    int ret = ahci_port_issue_cmd(ap, AHCI_TIMEOUT_CMD);

    /*
     * [AHCI-04] hdr->prdbc is the HBA's report of how many bytes it actually
     * moved.  It is zeroed before every command and declared volatile in
     * ahci.h for exactly this read-back -- which never happened.  A short
     * transfer was therefore indistinguishable from a complete one, and
     * because the bounce buffer comes from dma_alloc_coherent (zero-filled),
     * the untransferred tail was handed back as genuine zero data under a
     * SUCCESS return.  Silent corruption, not a visible I/O error.
     */
    if (ret == 0) {
        uint32_t moved = hdr->prdbc;
        if (moved != byte_count) {
            kprintf("ahci: short transfer on port %d: %u of %u bytes\n",
                    ap->port_num, moved, byte_count);
            ret = -1;
        }
    }

    if (dma_buf) {
        if (!is_write && ret == 0) {
            memcpy(data_virt, dma_buf, byte_count);
        }
        /* AHCI_CMD_WEDGED means the port could not be quiesced, so the HBA may
         * still complete this transfer into dma_buf.  Handing those pages back
         * to the allocator would let it DMA a sector into whatever is allocated
         * next; deliberately leak them instead. */
        if (ret != AHCI_CMD_WEDGED)
            ahci_dma_bounce_free(dma_buf, byte_count);
    }

    mutex_unlock(&ap->cmd_lock);
    return ret;
}

/*
 * ============================================================
 * IDENTIFY DEVICE
 * ============================================================
 */

static int ahci_identify(ahci_port_t *ap) {
    uint16_t *id_buf;
    dma_addr_t id_dma;
    hba_cmd_header_t *hdr;
    hba_cmd_table_t *tbl;
    struct fis_reg_h2d *fis;
    uint8_t cmd;
    int ret;
    int i;

    id_buf = dma_alloc_coherent(512, &id_dma);
    if (!id_buf) {
        kprint("ahci: failed to alloc identify buffer\n");
        return -1;
    }

    /*
     * [AHCI-13] There is one command table per port and this is the third
     * producer that scribbles on it, but it was the only one not taking
     * cmd_lock.  That was safe only by the accident that probing runs
     * serially -- and ahci_register_disk publishes sataN while later ports
     * are still being probed, so the assumption was already thin.  Take the
     * lock like ahci_ata_dma_cmd and ahci_scsi_execute do.
     */
    mutex_lock(&ap->cmd_lock);

    memset(ap->cmd_table, 0, sizeof(hba_cmd_table_t));

    /* Choose IDENTIFY command based on device type */
    cmd = (ap->type == AHCI_PORT_TYPE_SATAPI)
              ? AHCI_ATA_CMD_IDENTIFY_PACKET
              : AHCI_ATA_CMD_IDENTIFY;

    /*
     * Build the command FIS through the same helper the working read path
     * uses, rather than by hand.  The two constructions had drifted: this one
     * left the device register 0 where ahci_build_h2d_fis() is called with
     * 0x40 (the LBA bit) everywhere else.  That was the ONLY field that
     * differed -- with lba and count both 0 the rest is byte-identical -- and
     * it is not the reason IDENTIFY returns nothing, because NetBSD sends 0
     * too (ata.c:836-840 never assigns r_device for WDCC_IDENTIFY) while
     * FreeBSD sends ATA_DEV_LBA (ata_xpt.c:373 via ata_28bit_cmd), and both
     * work on real drives.  Route it through the builder anyway so the two
     * paths cannot silently diverge again, and so identify matches the path
     * this hardware demonstrably accepts.
     */
    tbl = ap->cmd_table;
    fis = (struct fis_reg_h2d *)tbl->cfis;
    ahci_build_h2d_fis(fis, cmd, 0, 0, 0x40);   /* LBA mode, as elsewhere */

    /* Single PRDT entry for 512-byte identify data */
    tbl->prdt[0].dba  = (uint32_t)id_dma;
    tbl->prdt[0].dbau = 0;
    tbl->prdt[0].dbc  = 511;  /* 0-based */
    tbl->prdt[0].i    = 0;

    /* Command header */
    hdr = &ap->cmd_list[AHCI_CMD_SLOT];
    hdr->cfl   = sizeof(struct fis_reg_h2d) / 4;
    hdr->a     = 0;
    hdr->w     = 0;     /* D2H (read) */
    hdr->p     = 0;     /* [AHCI-21] see ahci_ata_dma_cmd() */
    hdr->r     = 0;
    hdr->b     = 0;
    hdr->c     = 0;     /* [AHCI-21] Soft Reset only; clearing BSY early made
                         * IDENTIFY report 0 of 512 bytes on real hardware */
    hdr->pmp   = 0;
    hdr->prdtl = 1;
    hdr->prdbc = 0;

    /*
     * Issue, and retry once if nothing arrived.  On the C460 the command
     * completes clean -- PxTFD says DRDY with DRQ and ERR clear, PxSERR and
     * PxIS are zero, CR and FR are both running -- and moves zero bytes.  The
     * FIS is now identical to the read path's, so what is left is either the
     * PIO protocol itself or the fact that this is the first command issued
     * after the port was started.  A second attempt tells those apart: if the
     * retry succeeds it is a start-up race, if it fails identically it is the
     * protocol.
     */
    for (int attempt = 0; attempt < 2; attempt++) {
        hdr->prdbc = 0;
        ret = ahci_port_issue_cmd(ap, AHCI_TIMEOUT_IDENTIFY);
        if (ret < 0) {
            /* See ahci_ata_dma_cmd(): a wedged port may still write id_buf. */
            if (ret != AHCI_CMD_WEDGED)
                dma_free_coherent(id_buf, 512);
            mutex_unlock(&ap->cmd_lock);
            return -1;
        }
        if (hdr->prdbc == 512)
            break;

        kprintf("ahci: port %d: IDENTIFY attempt %d moved %u of 512 bytes "
                "(tfd=0x%08x serr=0x%08x is=0x%08x cmd=0x%08x)\n",
                ap->port_num, attempt + 1, hdr->prdbc, ap->regs->tfd,
                ap->regs->serr, ap->regs->is, ap->regs->cmd);

        if (attempt == 0) {
            uint64_t until = ahci_time_ms() + 50;
            while (ahci_time_ms() < until)
                __asm__ volatile("pause");
        }
    }

    if (id_buf[0] == 0 && id_buf[27] == 0) {
        kprintf("ahci: port %d: IDENTIFY data is empty "
                "(prdbc=%u, first words %04x %04x %04x)\n",
                ap->port_num, hdr->prdbc, id_buf[0], id_buf[1], id_buf[2]);
    }

    /* Parse IDENTIFY data */
    /* Model string: words 27-46 (byte-swapped pairs) */
    for (i = 0; i < 20; i++) {
        uint16_t w = id_buf[27 + i];
        ap->model[i * 2]     = (char)(w >> 8);
        ap->model[i * 2 + 1] = (char)(w & 0xFF);
    }
    ap->model[40] = '\0';
    /* Trim trailing spaces */
    for (i = 39; i >= 0 && ap->model[i] == ' '; i--) {
        ap->model[i] = '\0';
    }

    /* Serial number: words 10-19 */
    for (i = 0; i < 10; i++) {
        uint16_t w = id_buf[10 + i];
        ap->serial[i * 2]     = (char)(w >> 8);
        ap->serial[i * 2 + 1] = (char)(w & 0xFF);
    }
    ap->serial[20] = '\0';
    for (i = 19; i >= 0 && ap->serial[i] == ' '; i--) {
        ap->serial[i] = '\0';
    }

    /* Firmware revision: words 23-26 */
    for (i = 0; i < 4; i++) {
        uint16_t w = id_buf[23 + i];
        ap->firmware[i * 2]     = (char)(w >> 8);
        ap->firmware[i * 2 + 1] = (char)(w & 0xFF);
    }
    ap->firmware[8] = '\0';
    for (i = 7; i >= 0 && ap->firmware[i] == ' '; i--) {
        ap->firmware[i] = '\0';
    }

    /*
     * [AHCI-19] For an ATAPI device the interesting bit is word 62 bit 15:
     * "DMADIR is required for PACKET DMA commands".  The PACKET builder used
     * to set DMADIR on every read unconditionally.  A device that does not
     * require it is entitled to treat the bit as reserved and abort the
     * command, which presents as an optical drive that enumerates and then
     * fails every read.  Record the capability and let the builder decide.
     *
     * The geometry parsing below is ATA-only -- an ATAPI device reports its
     * capacity through READ CAPACITY, not IDENTIFY -- so stop here rather
     * than leave ap->sectors holding a number that means nothing.
     */
    if (ap->type == AHCI_PORT_TYPE_SATAPI) {
        ap->dmadir      = (id_buf[62] & (1U << 15)) ? 1 : 0;
        ap->write_cache = 0;
        ap->sector_size = AHCI_SECTOR_SIZE;
        ap->lba48       = 0;
        ap->sectors     = 0;
        dma_free_coherent(id_buf, 512);
        mutex_unlock(&ap->cmd_lock);
        return 0;
    }

    /*
     * [AHCI-18] Word 82 bit 5 says the device HAS a volatile write cache;
     * word 85 bit 5 says it is currently enabled.  FLUSH CACHE is only
     * meaningful when both hold -- and issuing it to a device without the
     * cache is a command abort, not a no-op.
     */
    ap->write_cache = ((id_buf[82] & (1U << 5)) &&
                       (id_buf[85] & (1U << 5))) ? 1 : 0;

    /* Sector size.  The IDENTIFY response is attacker-controlled (a
     * malicious or buggy SATA device).  An out-of-range value here
     * cascades into byte_count overflow in PRDT setup and division by
     * zero in max_sectors math.  Accept only the four ATA-spec sector
     * sizes; otherwise fall back to 512. */
    ap->sector_size = AHCI_SECTOR_SIZE;
    if ((id_buf[106] & (1 << 14)) && !(id_buf[106] & (1 << 15))) {
        if (id_buf[106] & (1 << 12)) {
            uint32_t reported = ((uint32_t)id_buf[117] |
                                 ((uint32_t)id_buf[118] << 16)) * 2;
            switch (reported) {
            case 512: case 1024: case 2048: case 4096:
                ap->sector_size = reported;
                break;
            default:
                kprintf("ahci: ignoring nonsense sector_size=%u, using 512\n",
                        reported);
                break;
            }
        }
    }

    /* LBA48 support: word 83 bit 10, word 86 bit 10 */
    if ((id_buf[83] & (1 << 10)) && (id_buf[86] & (1 << 10))) {
        ap->lba48 = 1;
        ap->sectors = (uint64_t)id_buf[100] |
                      ((uint64_t)id_buf[101] << 16) |
                      ((uint64_t)id_buf[102] << 32) |
                      ((uint64_t)id_buf[103] << 48);
    } else {
        ap->lba48 = 0;
        ap->sectors = (uint64_t)id_buf[60] | ((uint64_t)id_buf[61] << 16);
    }

    /*
     * [AHCI-08] sector_size is already sanity-checked above; the capacity was
     * taken verbatim from IDENTIFY.  ahci_build_h2d_fis writes only lba0..lba5
     * -- 48 bits -- so a device reporting 2^48 + 5 sectors would let an access
     * to that sector silently wrap and land on LBA 5, i.e. straight through
     * the partition table.  Clamp to what the command set can actually
     * address.
     */
    {
        uint64_t max = ap->lba48 ? AHCI_LBA48_MAX_LBA : AHCI_LBA28_MAX_LBA;
        if (ap->sectors > max) {
            kprintf("ahci: device claims %llu sectors, clamping to %llu "
                    "(LBA%d addressing limit)\n",
                    (unsigned long long)ap->sectors,
                    (unsigned long long)max, ap->lba48 ? 48 : 28);
            ap->sectors = max;
        }
    }

    dma_free_coherent(id_buf, 512);
    mutex_unlock(&ap->cmd_lock);
    return 0;
}

/*
 * ============================================================
 * Block Device Interface (SATA Disks)
 * ============================================================
 */

static ahci_port_t *ahci_port_from_bdev(blkdev_t *bdev) {
    return (ahci_port_t *)bdev->priv;
}

static int ahci_bdev_read(blkdev_t *bdev, uint64_t sector,
                           uint32_t count, void *buffer) {
    ahci_port_t *ap = ahci_port_from_bdev(bdev);
    uint8_t *buf = (uint8_t *)buffer;
    uint32_t max_sectors;
    uint32_t chunk;

    if (!ap || ap->type != AHCI_PORT_TYPE_SATA) {
        return -1;
    }

    /* Max sectors per command: bounded by the bounce buffer we can actually
     * allocate contiguously (see AHCI_MAX_XFER_BYTES). */
    max_sectors = AHCI_MAX_XFER_BYTES / ap->sector_size;
    if (max_sectors == 0) max_sectors = 1;
    if (max_sectors > 65535) {
        max_sectors = 65535;
    }
    /* [AHCI-07] An LBA28 command counts at most 256 sectors. */
    if (!ap->lba48 && max_sectors > AHCI_LBA28_MAX_SECTORS) {
        max_sectors = AHCI_LBA28_MAX_SECTORS;
    }

    while (count > 0) {
        chunk = (count > max_sectors) ? max_sectors : count;

        /*
         * [AHCI-07] ap->lba48 was computed at identify time and then read
         * nowhere: both paths issued READ/WRITE DMA EXT unconditionally.
         * Those opcodes are 48-bit-only, so an LBA28 drive enumerated with a
         * correct capacity and then aborted every transfer.
         */
        if (ahci_ata_dma_cmd(ap,
                              ap->lba48 ? AHCI_ATA_CMD_READ_DMA_EXT
                                        : AHCI_ATA_CMD_READ_DMA,
                              sector, chunk, buf, 0) < 0) {
            return -1;
        }

        sector += chunk;
        count  -= chunk;
        buf    += (uint64_t)chunk * ap->sector_size;
    }

    return 0;
}

static int ahci_bdev_write(blkdev_t *bdev, uint64_t sector,
                            uint32_t count, const void *buffer) {
    ahci_port_t *ap = ahci_port_from_bdev(bdev);
    const uint8_t *buf = (const uint8_t *)buffer;
    uint32_t max_sectors;
    uint32_t chunk;

    if (!ap || ap->type != AHCI_PORT_TYPE_SATA) {
        return -1;
    }

    max_sectors = AHCI_MAX_XFER_BYTES / ap->sector_size;   /* [AHCI-10] */
    if (max_sectors == 0) max_sectors = 1;
    if (max_sectors > 65535) {
        max_sectors = 65535;
    }
    if (!ap->lba48 && max_sectors > AHCI_LBA28_MAX_SECTORS) {   /* [AHCI-07] */
        max_sectors = AHCI_LBA28_MAX_SECTORS;
    }

    while (count > 0) {
        chunk = (count > max_sectors) ? max_sectors : count;

        if (ahci_ata_dma_cmd(ap,
                              ap->lba48 ? AHCI_ATA_CMD_WRITE_DMA_EXT
                                        : AHCI_ATA_CMD_WRITE_DMA,
                              sector, chunk, (void *)buf, 1) < 0) {
            return -1;
        }

        sector += chunk;
        count  -= chunk;
        buf    += (uint64_t)chunk * ap->sector_size;
    }

    return 0;
}

/*
 * [AHCI-18] Push the drive's volatile write cache to media.
 *
 * AHCI_ATA_CMD_FLUSH_CACHE_EXT was defined and never issued, and bdev.ioctl
 * was left NULL, so sync(2) and unmount drained the kernel's bio cache into
 * the DEVICE and stopped there.  A disk with write caching on acknowledges
 * those writes from its own DRAM; on power loss the data is gone even though
 * every layer above reported success.
 *
 * This carries no data, which is why it needed AHCI-16 (prdtl must be 0 for
 * a zero-length command) before it could be issued at all.
 */
static int ahci_bdev_ioctl(blkdev_t *bdev, uint32_t request, void *arg) {
    ahci_port_t *ap = (ahci_port_t *)bdev->priv;

    (void)arg;

    if (!ap)
        return -EINVAL;

    switch (request) {
    case BLKIOC_FLUSH:
        if (bdev->dead)
            return -EIO;
        /* Nothing volatile to flush: report success rather than send a
         * command the device is entitled to abort. */
        if (!ap->write_cache)
            return 0;
        if (ahci_ata_dma_cmd(ap,
                             ap->lba48 ? AHCI_ATA_CMD_FLUSH_CACHE_EXT
                                       : AHCI_ATA_CMD_FLUSH_CACHE,
                             0, 0, NULL, 0) < 0) {
            return -EIO;
        }
        return 0;
    default:
        return -ENOTTY;
    }
}

static void ahci_register_disk(ahci_port_t *ap) {
    char buf[128];

    ap->disk_index = ahci_disk_count++;

    memset(&ap->bdev, 0, sizeof(blkdev_t));
    snprintf(ap->bdev.name, sizeof(ap->bdev.name), "sata%d", ap->disk_index);
    ap->bdev.sector_size   = ap->sector_size;
    ap->bdev.total_sectors = ap->sectors;
    ap->bdev.priv          = ap;
    ap->bdev.read          = ahci_bdev_read;
    ap->bdev.write         = ahci_bdev_write;
    ap->bdev.ioctl         = ahci_bdev_ioctl;   /* [AHCI-18] */

    blkdev_register_disk(&ap->bdev);

    snprintf(buf, sizeof(buf),
             "ahci: port %d: %s (%llu sectors, %u bytes/sect) -> /dev/storage/%s\n",
             ap->port_num, ap->model,
             (unsigned long long)ap->sectors, ap->sector_size,
             ap->bdev.name);
    kprint(buf);
}

/*
 * ============================================================
 * SCSI Transport Interface (SATAPI Devices)
 * ============================================================
 */

static int ahci_scsi_execute(scsi_link_t *link, scsi_request_t *req) {
    ahci_port_t *ap;
    hba_cmd_header_t *hdr;
    hba_cmd_table_t *tbl;
    struct fis_reg_h2d *fis;
    void *dma_buf;
    dma_addr_t data_dma;
    int port_idx;
    int ret;

    (void)link;

    if (!req || !req->device) {
        return -1;
    }

    /*
     * [AHCI-06] Reject transfer lengths the hardware fields cannot express
     * before building the command.  The ATAPI byte-count-limit is 16 bits
     * (so 0 and >= 65536 are both unrepresentable -- BCL 0 is illegal per
     * ACS-3), and the PRDT dbc field is 22 bits, so anything above 4 MiB
     * truncates silently.
     */
    if (req->data_len == 0 || req->data_len > 65534u) {
        kprintf("ahci: rejecting ATAPI transfer of %u bytes "
                "(byte-count-limit is 16-bit, 1..65534)\n", req->data_len);
        req->status = SCSI_STATUS_CHECK_CONDITION;
        req->data_xfer = 0;
        return -1;
    }

    /* Map SCSI target to AHCI port.
     * Target ID is the port index in our registration order. */
    port_idx = req->device->target;
    ap = NULL;
    for (int c = 0; c < ahci_ctrl_count && !ap; c++) {
        for (int i = 0; i < AHCI_MAX_PORTS; i++) {
            if (ahci_ctrls[c].ports[i].type == AHCI_PORT_TYPE_SATAPI) {
                if (port_idx == 0) {
                    ap = &ahci_ctrls[c].ports[i];
                    break;
                }
                port_idx--;
            }
        }
    }

    if (!ap) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        return -1;
    }

    /* Serialise the shared command slot / table (see ahci_ata_dma_cmd). */
    mutex_lock(&ap->cmd_lock);

    /* Build ATAPI command via AHCI */
    tbl = ap->cmd_table;
    memset(tbl, 0, sizeof(hba_cmd_table_t));

    /* Command FIS: ATA PACKET command */
    fis = (struct fis_reg_h2d *)tbl->cfis;
    memset(fis, 0, sizeof(*fis));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->pmport_c = 0x80;
    fis->command  = AHCI_ATA_CMD_PACKET;
    /*
     * Feature bit 0 = DMA mode, bit 2 = DMADIR (1=D2H read, 0=H2D write).
     *
     * [AHCI-19] DMADIR used to be set on every read regardless of the
     * device.  It is only defined for devices that report "DMADIR required"
     * in IDENTIFY PACKET DEVICE word 62 bit 15 -- typically SATA bridges in
     * front of a PATA optical drive.  A native SATA drive is free to treat
     * the bit as reserved and abort the PACKET, which looks like a drive
     * that enumerates fine and then fails every read.  ap->dmadir comes from
     * the IDENTIFY PACKET issued at probe.
     */
    fis->featurel = 0x01;                            /* DMA */
    if (ap->dmadir && !(req->flags & SCSI_REQ_WRITE))
        fis->featurel |= 0x04;                       /* DMADIR: device -> host */
    /*
     * [AHCI-06] The ATAPI byte-count-limit is 16 bits, and req->data_len is a
     * uint32.  scsi_ctl.c explicitly permits data_len == 65536, which
     * truncates to a BCL of 0 -- illegal per ACS-3 -- and data_len == 0
     * likewise gives BCL 0.  The same value also feeds the PRDT's 22-bit dbc
     * field, where anything over 4 MiB truncates silently.  Reject what
     * cannot be expressed rather than issuing a malformed command.
     */
    fis->lba1     = (uint8_t)(req->data_len);         /* Byte count low */
    fis->lba2     = (uint8_t)(req->data_len >> 8);    /* Byte count high */

    /* Copy SCSI CDB into ACMD field */
    memcpy(tbl->acmd, req->cdb,
           req->cdb_len > 16 ? 16 : req->cdb_len);

    /* Setup PRDT if there's data */
    hdr = &ap->cmd_list[AHCI_CMD_SLOT];
    hdr->prdtl = 0;
    dma_buf = NULL;
    data_dma = 0;

    if (req->data && req->data_len > 0) {
        dma_buf = ahci_dma_bounce_alloc(req->data_len, &data_dma);
        if (!dma_buf) {
            req->status = SCSI_STATUS_CHECK_CONDITION;
            mutex_unlock(&ap->cmd_lock);
            return -1;
        }
        if (req->flags & SCSI_REQ_WRITE) {
            memcpy(dma_buf, req->data, req->data_len);
        }
        tbl->prdt[0].dba  = (uint32_t)data_dma;
        tbl->prdt[0].dbau = 0;
        tbl->prdt[0].dbc  = req->data_len - 1;
        tbl->prdt[0].i    = 0;
        hdr->prdtl = 1;
    }

    /* Command header */
    hdr->cfl   = sizeof(struct fis_reg_h2d) / 4;
    hdr->a     = 1;    /* ATAPI command */
    hdr->w     = (req->flags & SCSI_REQ_WRITE) ? 1 : 0;
    hdr->p     = 1;    /* both references pair Prefetchable with ATAPI */
    hdr->r     = 0;
    hdr->b     = 0;
    hdr->c     = 0;    /* [AHCI-21] see ahci_ata_dma_cmd() */
    hdr->pmp   = 0;
    hdr->prdbc = 0;

    ret = ahci_port_issue_cmd(ap, req->timeout_ms ? req->timeout_ms : AHCI_TIMEOUT_CMD);

    if (dma_buf) {
        if (!(req->flags & SCSI_REQ_WRITE) && ret == 0) {
            memcpy(req->data, dma_buf, req->data_len);
        }
        /* See ahci_ata_dma_cmd(): never reclaim from a wedged port. */
        if (ret != AHCI_CMD_WEDGED)
            ahci_dma_bounce_free(dma_buf, req->data_len);
    }

    if (ret < 0) {
        req->status = SCSI_STATUS_CHECK_CONDITION;
        mutex_unlock(&ap->cmd_lock);
        return -1;
    }

    req->status = SCSI_STATUS_GOOD;
    /*
     * [AHCI-09] data_xfer was set to the full requested length regardless of
     * what the device actually returned, and scsi_ctl.c uses it as the
     * copyout length -- so a device answering an INQUIRY with 36 bytes had
     * the rest of the caller's buffer filled from the zero-filled bounce
     * allocation and reported as real data.  Report what the HBA says it
     * moved, clamped to what was asked for.
     */
    {
        uint32_t moved = hdr->prdbc;
        if (moved > req->data_len)
            moved = req->data_len;
        req->data_xfer = moved;
    }
    mutex_unlock(&ap->cmd_lock);
    return 0;
}

static int ahci_scsi_reset_device(scsi_link_t *link, scsi_device_t *dev) {
    (void)link;
    (void)dev;
    /* Port reset could be implemented via COMRESET */
    return 0;
}

static int ahci_scsi_reset_bus(scsi_link_t *link) {
    (void)link;
    return 0;
}

static void ahci_register_satapi_devices(void) {
    int satapi_count = 0;

    for (int c = 0; c < ahci_ctrl_count; c++) {
        for (int i = 0; i < AHCI_MAX_PORTS; i++) {
            if (ahci_ctrls[c].ports[i].type == AHCI_PORT_TYPE_SATAPI) {
                satapi_count++;
            }
        }
    }

    if (satapi_count == 0) {
        return;
    }

    /* Four controllers x 32 ports is more addresses than the SCSI midlayer
     * has target IDs.  Say so rather than register a max_targets the scan
     * cannot honour. */
    if (satapi_count > SCSI_MAX_TARGETS) {
        kprintf("ahci: %d SATAPI devices found but only %d SCSI targets "
                "available; the rest are not exposed\n",
                satapi_count, SCSI_MAX_TARGETS);
        satapi_count = SCSI_MAX_TARGETS;
    }

    /*
     * [AHCI-20] This used to bail out at the top when ahci_scsi_registered
     * was set, so only the FIRST controller's optical drives were ever
     * exposed: a second HBA found later added SATAPI ports that nothing
     * enumerated, and max_targets stayed frozen at the first controller's
     * count -- which also capped the scan even for drives on the original
     * link.  The target-to-port mapping in ahci_scsi_execute() is computed
     * over every controller on each request, so the link itself was always
     * capable of addressing them.
     *
     * Rather than register a second link for the same transport, widen the
     * existing one and rescan.  scsi_scan_bus() skips addresses that are
     * already registered, so this adds only what is new.
     */
    if (ahci_scsi_registered) {
        if (satapi_count > (int)ahci_scsi_link.max_targets) {
            ahci_scsi_link.max_targets = (uint8_t)satapi_count;
            kprintf("ahci: SATAPI device count grew to %d; rescanning\n",
                    satapi_count);
            scsi_scan_bus(&ahci_scsi_link, ahci_scsi_link.bus_id);
        }
        return;
    }

    memset(&ahci_scsi_link, 0, sizeof(ahci_scsi_link));
    snprintf(ahci_scsi_link.name, sizeof(ahci_scsi_link.name), "ahci0");
    ahci_scsi_link.bus_id              = 1;  /* After ATAPI bus 0 */
    ahci_scsi_link.max_targets         = (uint8_t)satapi_count;
    ahci_scsi_link.max_luns            = 1;
    ahci_scsi_link.adapter_queue_depth = 1;
    ahci_scsi_link.execute             = ahci_scsi_execute;
    ahci_scsi_link.reset_device        = ahci_scsi_reset_device;
    ahci_scsi_link.reset_bus           = ahci_scsi_reset_bus;

    if (scsi_register_link(&ahci_scsi_link) == 0) {
        char buf[64];
        snprintf(buf, sizeof(buf),
                 "ahci: registered SATAPI transport (%d devices)\n",
                 satapi_count);
        kprint(buf);
        ahci_scsi_registered = 1;
    }
}

/*
 * ============================================================
 * HBA Initialization
 * ============================================================
 */

static int ahci_hba_init(ahci_controller_t *ctrl) {
    hba_mem_t *abar = ctrl->abar;
    uint32_t version;
    char buf[128];

    /* BIOS/OS Handoff (BOHC): take ownership from BIOS if needed */
    if (abar->cap2 & (1U << 0)) {  /* CAP2.BOH - BIOS/OS Handoff supported */
        if (abar->bohc & (1U << 0)) {  /* BOS - BIOS owns semaphore */
            uint64_t bohc_deadline;
            abar->bohc |= (1U << 1);   /* Set OOS - OS requests ownership */
            /* Wait for BIOS to release (BOS clears) */
            bohc_deadline = ahci_time_ms() + 2000;
            while (abar->bohc & (1U << 0)) {
                if (ahci_time_ms() > bohc_deadline) {
                    kprint("ahci: BIOS handoff timeout, forcing ownership\n");
                    break;
                }
                __asm__ volatile("pause");
            }
        }
    }

    /* Enable AHCI mode (GHC.AE) */
    abar->ghc |= HBA_GHC_AE;

    /* Read capabilities */
    ctrl->cap = abar->cap;
    ctrl->pi  = abar->pi;
    ctrl->num_ports = (ctrl->cap & HBA_CAP_NP_MASK) + 1;

    /*
     * [AHCI-05] ctrl->pi comes straight from the device and every one of its
     * 32 bits used to be walked, dereferencing abar->ports[port] at offsets up
     * to 0x100 + 31*0x80 = 0x1080.  pci_iomap maps exactly bar_sz bytes and
     * nothing bounded the walk against it, so an HBA with a 0x1000-byte BAR5
     * and PI=0xFFFFFFFF had us reading AND WRITING past the end of the
     * ioremap region.
     *
     * Keep only the bits that fit inside the mapping we actually made and fit
     * our port array.
     *
     * CAP.NP deliberately does NOT bound this.  It is a count of the ports the
     * silicon supports, not the highest port number in use, and AHCI 1.3.1
     * s3.1.9 lets PI be sparse: a Lynx Point HBA reports CAP.NP=4 with
     * PI=0x12, meaning ports 1 and 4 -- two implemented ports, which is well
     * within the four the part supports.  Folding NP into the bit mask capped
     * PI at bit 3 and silently deleted port 4 along with whatever disk was
     * attached to it.
     */
    {
        unsigned mappable = 0;
        if (ctrl->bar_sz > 0x100)
            mappable = (unsigned)((ctrl->bar_sz - 0x100) / sizeof(hba_port_t));

        unsigned limit = AHCI_MAX_PORTS;
        if (mappable < limit) limit = mappable;

        uint32_t allowed = (limit >= 32) ? 0xFFFFFFFFu
                                         : ((1u << limit) - 1u);
        if (ctrl->pi & ~allowed) {
            kprintf("ahci: PI=0x%08x has ports past the %u the BAR5 mapping "
                    "covers (%u bytes); masking to 0x%08x\n",
                    ctrl->pi, limit, (unsigned)ctrl->bar_sz,
                    ctrl->pi & allowed);
            ctrl->pi &= allowed;
        }
    }
    ctrl->num_cmd_slots = ((ctrl->cap & HBA_CAP_NCS_MASK) >> HBA_CAP_NCS_SHIFT) + 1;
    version = abar->vs;

    snprintf(buf, sizeof(buf),
             "ahci: version %d.%d%d, %d ports, %d cmd slots, PI=0x%08x\n",
             (int)((version >> 16) & 0xFFFF),
             (int)((version >> 8) & 0xFF),
             (int)(version & 0xFF),
             ctrl->num_ports,
             ctrl->num_cmd_slots,
             ctrl->pi);
    kprint(buf);

    if (ctrl->cap & HBA_CAP_S64A) {
        kprint("ahci: 64-bit addressing supported (using 32-bit)\n");
    }
    if (ctrl->cap & HBA_CAP_SNCQ) {
        kprint("ahci: NCQ supported (not used)\n");
    }

    return 0;
}

/*
 * ============================================================
 * Port Probing
 * ============================================================
 */

static void ahci_probe_ports(ahci_controller_t *ctrl) {
    uint32_t pi = ctrl->pi;
    int port;
    char buf[128];

    ctrl->port_count = 0;

    for (port = 0; port < AHCI_MAX_PORTS; port++) {
        ahci_port_t *ap;
        const char *type_str;

        if (!(pi & (1U << port))) {
            continue;
        }

        ap = &ctrl->ports[port];
        ap->regs = &ctrl->abar->ports[port];

        /*
         * [AHCI-15] Spin the device up BEFORE deciding the port is empty.
         * On a CAP.SSS controller PxCMD.SUD is clear out of reset and DET
         * reads 0 for a perfectly good drive, so the old pre-check --
         *     if ((ssts & DET) == 0) continue;
         * -- skipped every port and the HBA came up with no disks at all.
         * On a controller without staggered spin-up this is just the same
         * cheap DET read it always was.
         */
        if (!ahci_port_spinup(ap, ctrl->cap)) {
            continue;  /* No device, no phy — skip expensive COMRESET */
        }

        if (ahci_port_init(ap, &ctrl->abar->ports[port], port) < 0) {
            snprintf(buf, sizeof(buf),
                     "ahci: port %d init failed\n", port);
            kprint(buf);
            continue;
        }
        ap->ctrl = ctrl;   /* back-pointer so issue_cmd can see irq_ready */

        if (!ahci_port_detect_device(ap)) {
            ahci_port_teardown(ap);   /* [AHCI-11] don't abandon it running */
            continue;
        }

        switch (ap->type) {
        case AHCI_PORT_TYPE_SATA:   type_str = "SATA disk";   break;
        case AHCI_PORT_TYPE_SATAPI: type_str = "SATAPI";      break;
        case AHCI_PORT_TYPE_SEMB:   type_str = "SEMB";        break;
        case AHCI_PORT_TYPE_PM:     type_str = "Port Mult";   break;
        default:                    type_str = "unknown";      break;
        }

        snprintf(buf, sizeof(buf),
                 "ahci: port %d: %s detected (sig=0x%08x)\n",
                 port, type_str, ap->regs->sig);
        kprint(buf);

        ctrl->port_count++;

        /*
         * [AHCI-19] IDENTIFY PACKET DEVICE for optical drives too, not just
         * IDENTIFY for disks.  ahci_identify() already picks the right
         * opcode from ap->type; it simply was never called for SATAPI, so
         * ap->dmadir stayed 0 and the PACKET builder guessed instead.  A
         * failure here is not fatal for an ATAPI device -- it just means we
         * default to not asserting DMADIR -- so only disks get the retry
         * complaint below.
         */
        if (ap->type == AHCI_PORT_TYPE_SATAPI) {
            if (ahci_identify(ap) != 0) {
                snprintf(buf, sizeof(buf),
                         "ahci: port %d: IDENTIFY PACKET failed; "
                         "assuming no DMADIR\n", port);
                kprint(buf);
                ap->dmadir = 0;
            }
        }

        /* IDENTIFY for SATA disks */
        if (ap->type == AHCI_PORT_TYPE_SATA) {
            if (ahci_identify(ap) == 0) {
                ahci_register_disk(ap);
            } else {
                snprintf(buf, sizeof(buf),
                         "ahci: port %d: IDENTIFY failed\n", port);
                kprint(buf);
            }
        }
    }

    if (ctrl->port_count == 0) {
        kprint("ahci: no devices found\n");
    }
    /* SATAPI/SCSI registration happens once, after every HBA is attached
     * (see ahci_init), so a single SCSI link can span all controllers. */
}

/*
 * ============================================================
 * PCI Driver Model Integration
 * ============================================================
 */

static int ahci_pci_attach(struct device *dev) {
    pci_device_t *pdev;
    void *mmio_base;
    size_t bar_sz;

    pdev = pci_find_device_by_kdev(dev);
    if (!pdev) {
        return -1;
    }

    /* Verify BAR5 is memory-mapped */
    if (pci_bar_type(pdev, 5) != PCI_BAR_MEM32) {
        kprint("ahci: BAR5 is not a 32-bit memory BAR\n");
        return -1;
    }

    bar_sz = pci_bar_size(pdev, 5);
    if (bar_sz == 0) {
        kprint("ahci: BAR5 size is 0\n");
        return -1;
    }

    /* Enable bus mastering and memory space */
    pci_write_config16(pdev->bus, pdev->slot, pdev->func,
                       PCI_CONFIG_COMMAND,
                       pci_read_config16(pdev->bus, pdev->slot, pdev->func,
                                         PCI_CONFIG_COMMAND) |
                       PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);

    /* Map AHCI registers */
    mmio_base = pci_iomap(pdev, 5, bar_sz);
    if (!mmio_base) {
        kprint("ahci: failed to iomap BAR5\n");
        return -1;
    }

    if (ahci_ctrl_count >= AHCI_MAX_CONTROLLERS) {
        kprint("ahci: too many AHCI controllers; ignoring this one\n");
        iounmap(mmio_base);   /* [AHCI-12] don't leak the BAR5 mapping */
        return -1;
    }

    ahci_controller_t *ctrl = &ahci_ctrls[ahci_ctrl_count];
    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->abar    = (hba_mem_t *)mmio_base;
    ctrl->bar_sz  = bar_sz;          /* [AHCI-05] bound the PI walk to this */
    ctrl->pci_dev = pdev;

    if (ahci_hba_init(ctrl) < 0) {
        /* [AHCI-12] Currently unreachable -- ahci_hba_init always returns 0 --
         * but it must not leak the mapping if that ever changes. */
        kprint("ahci: HBA init failed\n");
        ctrl->abar = NULL;
        iounmap(mmio_base);
        return -1;
    }

    ahci_ctrl_count++;          /* commit the controller before probing */
    ahci_probe_ports(ctrl);     /* probe runs polled (irq_ready still 0) */
    ahci_enable_interrupts(ctrl);   /* switch to completion-driven I/O */
    return 0;
}

/* PCI ID table: Class 01h (Mass Storage), Subclass 06h (SATA), ProgIF 01h (AHCI) */
static const device_id_t ahci_pci_ids[] = {
    { DEVICE_ID_ANY, DEVICE_ID_ANY, 0x00010601U, 0x00FFFFFF, 0 },
    { 0, 0, 0, 0, 0 },
};

static struct driver ahci_pci_driver = {
    .name     = "ahci",
    .id_table = ahci_pci_ids,
    .attach   = ahci_pci_attach,
};

/*
 * ============================================================
 * Module Entry Point
 * ============================================================
 */

void ahci_init(void) {
    if (ahci_initialized) {
        return;
    }

    ahci_ctrl_count = 0;
    ahci_disk_count = 0;

    if (!pci_present()) {
        return;
    }

    /* driver_register synchronously attaches every matching PCI HBA, so by
     * the time it returns ahci_ctrls[0..ahci_ctrl_count) are all probed. */
    (void)driver_register(&ahci_pci_driver, &pci_bus_type);

    /* Register SATAPI (ATAPI) devices across all controllers in one SCSI
     * link, now that every HBA has been probed. */
    ahci_register_satapi_devices();

    ahci_initialized = 1;
}
