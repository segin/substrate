# Substrate e1000e and r8168 driver audit

**Scope:** `sys/drivers/net/e1000e.c` (948 lines) and `sys/drivers/net/r8168.c` (824 lines). Every line cited below was re-read against the current tree.

**How findings were kept and rated**
- Each finding survived at least 2 of 3 independent skeptics.
- 82 verified reports were merged into 56 findings. Merging combined true duplicates, which share the same root cause and the same fix.
- Severity is the most defensible rating among the verifier votes. For merged findings the votes were pooled.
- "Upheld" gives how many skeptics confirmed the finding.

## Summary

| Driver | High | Medium | Low | Total |
|---|---|---|---|---|
| e1000e | 3 | 15 | 21 | 39 |
| r8168 | 2 | 5 | 10 | 17 |
| **Total** | **5** | **20** | **31** | **56** |

**Real targets**
- I219-V 8086:1570 (Sunrise Point, Skylake laptop)
- I217-LM 8086:153A (Lynx Point, Haswell laptop)
- RTL8168H, hwrev 0x54000000 (Whiskey Lake laptop)

The only part tested so far is the 82574L under QEMU. QEMU does not model most of the defects below.

---

## e1000e

### High

#### E1. The TX ring can fill completely, so TDT lands on TDH and the hardware sees an empty ring
- **Location:** `e1000e.c:474` (slot check), `e1000e.c:496-497` (tail write). Upheld 3/3, two reports merged.
- **What is wrong:**
  - `e2k_xmit` only checks that the slot it is about to fill has retired (`cmd != 0 && !DD`). It never keeps a descriptor free.
  - On a fresh ring every slot has `cmd == 0`, so 16 frames are accepted back to back. The 16th writes `TDT = (15+1) % 16 = 0`, which equals TDH.
  - The same thing happens in steady state. Slot k is retired, so TDH is at k+1. If slots k+1 onward are still pending, refilling k writes `TDT = k+1 = TDH`.
  - On timeout, `tx_cur` is never advanced, and no code path ever resets or re-arms the TX ring.
- **Correct hardware behaviour:**
  - On the legacy TX queue, TDH == TDT means "no work". Software may own at most N-1 descriptors and must never advance the tail onto the head.
  - The MAC stops transmit DMA while the link is down, so queued descriptors do not retire then.
- **Failure scenario:**
  1. The cable is unplugged at boot, or autonegotiation is still running.
  2. DHCP, ARP and ND retries queue 16 frames while TDH stays at 0. The 16th write sets TDT=0.
  3. The hardware treats the ring as empty, so no queued frame is ever sent and no DD bit is ever written.
  4. The 17th xmit finds slot 0 with `cmd != 0` and DD clear. It spins to the limit and returns -EIO, and so does every later send.
  5. TX stays dead until reboot, even after the link returns.

  A 16-frame burst on a 10/100 link can cause the same overlap.
- **Fix sketch:** keep a reclaim index and refuse when the ring is full.
  ```c
  uint32_t next = (slot + 1) % E1K2_TX_DESCS;
  struct e1k2_tx_desc *n = &e2k.tx_ring[next];
  if (n->cmd != 0 && !(n->status & TXD_STAT_DD)) {   /* next is still HW-owned */
      spinlock_release_irq(&e2k_tx_lock, flags);
      e2k.netdev.tx_dropped++;
      return -ENOBUFS;                               /* never write TDT == TDH */
  }
  ```
  Also drop frames immediately while `STATUS.LU` is clear, and pair this with the recovery path in E15.

#### E2. Ultra Low Power (ULP) is never exited on I218-LP and I219 PHYs
- **Location:** `e1000e.c:367` (the "Ultra Low Power mode?" message). Bring-up continues regardless and registers eth0 at line 862. Upheld 3/3.
- **What is wrong:** when MDIO fails, `e2k_phy_bringup()` logs a message and returns. The driver never touches any of the following:
  - H2ME (0x5B50)
  - FWSM.ULP_CFG_DONE (0x400)
  - the PHY ULP configuration register (page 779, register 16)
  - FEXTNVM7.DISABLE_SMB_PERST

  A MAC reset does not clear the PHY's sticky ULP state, and it does not clear a ULP request held by the Management Engine (ME).
- **Correct hardware behaviour:** on every ULP-capable PCH PHY, ULP must be forcibly exited before the first PHY access, because its prior state is unknown. That covers all Lynx Point and later parts except I217-LM/V (0x153A/0x153B) and I218-LM2/V2 (0x15A0/0x15A1).
  - **ME present (FWSM.FW_VALID set):**
    1. Clear H2ME.ULP (0x800) and set H2ME.ENFORCE_SETTINGS (0x1000).
    2. Poll until FWSM.ULP_CFG_DONE clears: 10 ms steps, about 300 ms total (about 1 s on Cannon Point and later).
    3. Clear ENFORCE_SETTINGS.
  - **No ME** (all under the software flag):
    1. Toggle LANPHYPC.
    2. Clear force-SMBus in the PHY (CV_SMB_CTRL) and in CTRL_EXT.FORCE_SMBUS.
    3. Set K1_ENABLE in the PHY PM control register.
    4. In the ULP config register, clear IND, STICKY_ULP, RESET_TO_SMBUS, WOL_HOST, INBAND_EXIT, EN_ULP_LANPHYPC, DIS_CLR_STICKY_ON_PERST and DISABLE_SMB_PERST, then set START to commit.
    5. Clear FEXTNVM7.DISABLE_SMB_PERST.
    6. Reset the PHY and wait 50 ms.
- **Failure scenario:**
  1. An I219-V is shut down with Wake-on-LAN off by a driver that enters ULP on the way to Sx. That is the usual shutdown path.
  2. STICKY_ULP and disabled SMBus release on PERST# make the state survive the platform reset.
  3. On the next boot every MDIO read fails and eth0 registers anyway.
  4. STATUS.LU never sets, so there is no link or traffic until power is fully removed.
- **Fix sketch:**
  - Add a per-ID "ULP-capable" flag and an `e2k_ulp_disable()` that runs before `e2k_phy_bringup()`.
  - This needs paged PHY access (pages ≥ 768 sit at MDIO address 1, behind a page-select write).
  - If the PHY is still unreachable afterwards, report the interface as having no carrier instead of claiming RUNNING.
  - The I217-LM target is excluded from the ULP flow and is not affected by this finding.

#### E3. I219 transmit errata (IOSFPC data corruption, TARC0 multi-request) not applied
- **Location:** `e1000e.c:849`. The TCTL enable is followed by nothing specific to K_SPT. Upheld 3/3, two reports merged.
- **What is wrong:** IOSFPC (0x0F28) and TARC0 (0x3840) are never defined or written.
- **Correct hardware behaviour:** once TCTL is programmed on the Sunrise Point / Kaby Lake MAC generation, two writes are required:
  - IOSFPC |= 0x00010000. This works around transmit data corruption.
  - TARC0: clear the multi-request field (0x30000000), then set 0x20000000. This limits transmit DMA to two outstanding read requests instead of three, per the I218/I219 specification update item "Buffer overrun while the I219 is processing DMA transactions".
- **Failure scenario:**
  1. On the I219-V, sustained transmit (a bulk TCP upload) keeps three read requests in flight.
  2. The documented buffer overrun then either corrupts payload on the wire (bad checksums at the peer) or hangs the transmit unit.
  3. In the hang case DD stops, the ring fills, and every xmit returns -EIO. Nothing recovers it.
- **Fix sketch:**
  ```c
  if (e2k_is_spt_gen()) {      /* 0x156F,0x1570,0x15B7-0x15B9,0x15D6-0x15D8,0x15E3 */
      e2k_write(R_IOSFPC, e2k_read(R_IOSFPC) | 0x00010000u);
      uint32_t t = e2k_read(R_TARC0);
      e2k_write(R_TARC0, (t & ~0x30000000u) | 0x20000000u);
  }
  ```
  Gate this on the Sunrise Point / Kaby Lake IDs, not on all of K_SPT. The Cannon Point and later I219 IDs in the table (0x15BB-0x15BE, 0x15DF-0x15E2, 0x0D4C-0x0D55) do not take this workaround. Apply it after the TARC fixes in E6.

### Medium

#### E4. MAC reset is issued with TX re-enabled and without the PCIe master-disable handshake
- **Location:** `e1000e.c:791`. Quiesce at 757-762, I219 flush at 778 (calls 591 and 617). Upheld 3/3, two reports merged.
- **What is wrong:**
  - The only quiesce (IMC, clear RCTL.EN and TCTL.EN, 10 ms) runs before `e2k_flush_desc_rings()`.
  - The flush sets TCTL.EN (591) and never clears it again. It may also pulse RCTL.EN with no settle delay afterwards (617).
  - CTRL.RST is then written immediately at 791.
  - CTRL.GIO_MASTER_DISABLE (bit 2) is never set, and STATUS.GIO_MASTER_ENABLE (bit 19) is never polled, on any part.
- **Correct hardware behaviour:** after any I219 descriptor flush, and on the 82574 and on all PCH parts:
  1. Set CTRL.GIO_MASTER_DISABLE.
  2. Poll STATUS bit 19 until it clears, up to 800 × 100 µs. Continue with a warning if it never clears.
  3. Write IMC = all ones, RCTL = 0 and TCTL = PSP (EN clear), then flush.
  4. Wait 10 ms.
  5. Assert CTRL.RST.

  The purpose is to make sure no TLP is still outstanding when the MAC resets.
- **Failure scenario:**
  1. An I219 is left with FLUSH_DESC_REQUIRED set after a PXE or UEFI network boot.
  2. The flush leaves the transmitter enabled, or leaves a receive pulse microseconds old.
  3. CTRL.RST lands while a descriptor fetch or write-back is still in flight. The PCIe completion is lost, after which MMIO reads return all ones or the next register access hangs.

  On the 82574 and I217, only the missing handshake applies, and exposure is small.
- **Fix sketch:** move the quiesce block so it runs after the flush, add the master-disable poll, and only then write `CTRL_RST`.

#### E5. The I219 RX flush enables the receiver on the firmware's stale RX ring
- **Location:** `e1000e.c:614`. RX ring registers are not programmed until 836-840. Upheld 2/3.
- **What is wrong:** `e2k_flush_desc_rings()` points only the TX ring at driver memory (590). The RX recovery step does the following with RDBAL, RDBAH, RDLEN, RDH and RDT still holding whatever a pre-boot network stack left:
  - opens the RX prefetch threshold to 31 (612)
  - sets RCTL.EN for 1 ms (614)

  The flush function's own comment says it runs on its own rings, which is false for RX.
- **Correct hardware behaviour:** before the receiver is enabled even briefly, the RX ring registers must describe a ring the driver owns. Either set RDH = RDT = 0 so no descriptor is owned by hardware, or hand the hardware driver-owned buffers.
- **Failure scenario:**
  1. A UEFI PXE stack leaves RDT ahead of RDH with its buffers in boot-services memory, which the OS has since reclaimed.
  2. While RCTL.EN is high, broadcast frames are DMA-written into those reclaimed pages, and descriptor status is written back into the old ring.
  3. Kernel or user memory is silently corrupted.
- **Fix sketch:** program `RDBAL/RDBAH/RDLEN` to `rx_ring_phys` with `RDH = RDT = 0` at the initial quiesce (after clearing RCTL.EN) and before any flush. One skeptic noted that descriptors already in the on-chip cache are not invalidated by this. The fix still stops new fetches against stale memory.

#### E6. TCTL is written from scratch (clearing MULR) and the required TARC0/TARC1 bits are never set
- **Location:** `e1000e.c:849` (TCTL literal), `e1000e.c:843-845` (TX setup without TARC). Upheld 3/3, two reports merged.
- **What is wrong:**
  - TCTL is written as a literal, so TCTL.MULR (bit 28, set at reset) is cleared on every part.
  - TARC0 (0x3840) and TARC1 (0x3940) are never written.
  - The driver sets FEXTNVM11.DISABLE_MULR_FIX (581), a workaround that only matters when MULR is in use, and then turns MULR off.
- **Correct hardware behaviour:**
  - **TCTL:** program it read-modify-write. Clear CT, then OR in EN, PSP, RTLC, CT and MULR. This applies to 82571-class and all PCH MACs.
  - **PCH parts, after every reset and before the TX enable:**
    - TARC0 |= bits 23, 24, 26 and 27.
    - TARC1 |= bits 24, 26 and 30.
    - TARC1 bit 28 = NOT TCTL.MULR.
  - **82574:** TARC0 clear bits 30:27 and set bit 26. This is tracked in E19.
- **Failure scenario:** on real I217-LM, I219-V and 82574L silicon, transmit arbitration runs at reset values the datasheet marks as invalid. The plausible result is TX stalls under load (TDH stops, DD never sets, -EIO). It has not been observed. QEMU ignores these registers.
- **Fix sketch:**
  ```c
  uint32_t tctl = e2k_read(R_TCTL) & ~(0xFFu << TCTL_CT_SHIFT);
  tctl |= TCTL_EN | TCTL_PSP | TCTL_RTLC | TCTL_MULR | (0x0F << TCTL_CT_SHIFT);
  if (e2k_is_pch()) {
      e2k_write(R_TARC0, e2k_read(R_TARC0) | (1u<<23)|(1u<<24)|(1u<<26)|(1u<<27));
      uint32_t t1 = e2k_read(R_TARC1) | (1u<<24)|(1u<<26)|(1u<<30);
      t1 = (tctl & TCTL_MULR) ? (t1 & ~(1u<<28)) : (t1 | (1u<<28));
      e2k_write(R_TARC1, t1);
  }
  e2k_write(R_TCTL, tctl);
  ```

#### E7. SMBus mode left by previous software is never cleared; no LANPHYPC recovery
- **Location:** `e1000e.c:807` (CTRL_EXT read-modify-write keeps FORCE_SMBUS). The PHY failure path is at 366-369. Upheld 3/3.
- **What is wrong:** none of the following is defined or used anywhere in the driver:
  - CTRL_EXT.FORCE_SMBUS (bit 11)
  - CTRL_EXT.LPCD (bit 2)
  - CTRL.LANPHYPC_OVERRIDE and LANPHYPC_VALUE (bits 16/17)
  - the FEXTNVM3 PHY config counter
  - the PHY's CV_SMB_CTRL force bit
- **Correct hardware behaviour** on I217, I218 and I219, after reset:
  - **PHY reachable and FWSM.FW_VALID clear:** clear the force-SMBus bit in the PHY (CV_SMB_CTRL bit 0) and in CTRL_EXT.
  - **PHY unreachable:**
    1. Set CTRL_EXT.FORCE_SMBUS, wait 50 ms, retry.
    2. If it still fails and FWSM.RSPCIPHY allows it: set the FEXTNVM3 PHY config counter to 50 ms, drive LANPHYPC low (OVERRIDE=1, VALUE=0) for 1 ms, release, poll CTRL_EXT.LPCD for up to about 100 ms, then wait 30 ms.
    3. Retry. Clear CTRL_EXT.FORCE_SMBUS and retry once more.
- **Failure scenario:**
  1. An I219-V with no ME is warm-rebooted after a shutdown path that forced SMBus mode.
  2. MDIO fails, STATUS.LU never sets, and eth0 registers with no link.
  3. Only removing all power recovers it.
- **Fix sketch:** add an `e2k_phy_access_recover()` that implements the sequence above and runs before `e2k_phy_bringup()`. Share it with the ULP exit in E2.

#### E8. IPv6 all-nodes and solicited-node multicast are rejected by the receive filter
- **Location:** `e1000e.c:851` (RCTL = EN|BAM|…, no MPE). The MTA is zeroed at 823-824. Upheld 3/3.
- **What is wrong:**
  - RCTL.MPE is only set by `e2k_set_allmulti`, which runs only when the first IPv4 group is joined.
  - The IPv6 input path accepts ff02::1 and the solicited-node group of the interface address. Those arrive as 33:33:00:00:00:01 and 33:33:ff:xx:xx:xx, and no MTA bit is ever set for them.
- **Correct hardware behaviour:** a group-addressed frame that is not broadcast passes only in one of these cases:
  - RCTL.MPE is set;
  - it matches a valid RAR;
  - its RCTL.MO-selected 12 address bits index a set bit in the 4096-bit MTA. With MO=0 those are bits 47:36: index = `(mac[5] << 4) | (mac[4] >> 4)`, word = index >> 5, bit = index & 31.
- **Failure scenario:**
  1. An interface has fec0::3, or an address set by ioctl, and no IPv4 group joined.
  2. A peer's Neighbor Solicitation to 33:33:ff:00:00:03 is dropped in hardware.
  3. Inbound-initiated IPv6 fails, and Router Advertisements are never seen.

  Exchanges the host starts itself partly work, because its own NS carries a source link-layer option.
- **Fix sketch:** set the MTA bits for 33:33:00:00:00:01 and for 33:33:ff plus the low 24 bits of each IPv6 address, and recompute them on every address change. Doing it through a new netdev filter hook is cleanest. The simplest alternative is to keep RCTL.MPE set whenever IPv6 is configured.

#### E9. Unbounded RX drain in hard-IRQ context livelocks the CPU under sustained traffic
- **Location:** `e1000e.c:391`. Upheld 3/3.
- **What is wrong:**
  - `e2k_rx_drain` is a `for (;;)` loop that exits only when it finds a descriptor with DD clear.
  - Every consumed descriptor is handed back at once through RDT.
  - Each frame runs the full input stack inside the ISR with IF=0, and the EOI has not been sent yet.
- **Correct hardware behaviour:**
  - The MAC refills each returned descriptor as soon as a frame arrives, so a consumer that keeps handing descriptors back can be held in the loop forever at line rate.
  - Running out of descriptors is harmless: the MAC counts RNBC/MPC, raises RXO and resumes when the tail advances.
- **Failure scenario:** a broadcast storm or ARP flood at 1 Gb/s (about 672 ns per minimum-size frame) on the effectively single-CPU kernel keeps the ISR running. Timer, keyboard and scheduler are starved, and the machine appears frozen until the traffic stops.
- **Fix sketch:**
  - Limit each call to `E1K2_RX_DESCS` descriptors.
  - ICR is read-to-clear, so if the budget runs out, write `ICS = ICR_RXT0` to re-raise the cause, and let the timer in after the EOI.
  - Better still, mask the RX causes in IMS and finish draining in deferred context.

#### E10. Low Power Link Up and Gigabit-Disable are never cleared, so link can be capped below 1000
- **Location:** `e1000e.c:353` (`e2k_phy_bringup` only touches BMCR.PDOWN). Upheld 3/3.
- **What is wrong:**
  - On PCH parts the driver never writes the PHY OEM bits register (page 768, reg 25 at MDIO address 1: LPLU 0x0004, GbE-disable 0x0040, restart-AN 0x0400).
  - On the 82574 it never writes the MAC PHY_CTRL register (0x0F10: D0A_LPLU 0x02, GBE_DISABLE 0x40).
  - Only the MAC is reset, so state left by a previous Sx or Wake-on-LAN path persists.
- **Correct hardware behaviour:** in D0, clear GbE-disable and D0 LPLU and restart autonegotiation. On PCH, do it with a read-modify-write of the OEM bits register, gated on FWSM.RSPCIPHY.
- **Failure scenario:** an I219-V warm-rebooted after a WoL or S5 path links at 100 Mb/s, or 10 Mb/s with LPLU, on a gigabit switch, and reports it as a normal link-up.
- **Fix sketch:** add paged PHY access, then clear `GBE_DIS|LPLU` and set `RESTART_AN` in OEM bits (PCH), or clear `PHY_CTRL.GBE_DISABLE|D0A_LPLU` (82574), then restart autonegotiation.

#### E11. No I219 speed-dependent link-up fixups (TIPG, PHY pointer gap)
- **Location:** `e1000e.c:452` (LSC only logs), `e1000e.c:848` (TIPG fixed at IPGT=8). Upheld 3/3, two reports merged.
- **What is wrong:** TIPG is written once and never revisited. PHY page 776 reg 20 is never touched, and the driver has no paged PHY access at all.
- **Correct hardware behaviour,** on every link-up:
  - **TIPG.IPGT:**
    - 0xFF at 10 Mb/s half duplex. This applies to all PCH parts, the I217 included.
    - 0x0C at 10/100 full duplex on Sunrise Point and later.
    - 0x08 otherwise.
  - **Sunrise Point and later, PHY page 776 reg 20:** at 1000 Mb/s, raise the pointer-gap field (bits 11:2) to at least 0x18. At 10/100, write 0xC023.
  - The EMI RX-config latency setting is adjusted in the same step.
- **Failure scenario:** an I219-V at 1000 Mb/s with a power-on pointer gap below 0x18, or on a 100 Mb/s full-duplex dock port, sees TX loss, retransmits or stalls.
- **Fix sketch:** add a link-change worker running outside the hard IRQ, because PHY access can busy-wait for up to 1 s. It reads STATUS speed and duplex, rewrites TIPG.IPGT, and applies the 776/20 fixup under the software flag.

#### E12. Lynx Point+ PLL clock-gate, K1 clock request, FEXTNVM4 beacon duration and K1-off fixups never applied
- **Location:** `e1000e.c:416` (link handling is logging only), `e1000e.c:809-810` (the only PCH post-reset tweak is KABGTXD). Upheld 3/3, two reports merged.
- **What is wrong:** FEXTNVM4 (0x0024), FEXTNVM6 (0x0010) and PCIEANACFG (0x0F18) are never defined, and the PHY PLL and PM registers are never touched.
- **Correct hardware behaviour,** on every link check on I217, I218 and I219:
  - PHY page 772 reg 28, low 11 bits (mask 0x07FF): 0x3E8 at 10/100, 0xFA at 1000.
  - At 1000 Mb/s, set the K1 clock-request bit (0x0200) in PHY page 770 reg 17.
  - Force FEXTNVM4 bits 2:0 to 7 (8 µs beacon). This is the I217 packet-loss erratum fix; reset can reload a wrong NVM value.
  - Sunrise Point only: copy PCIEANACFG bit 31 into FEXTNVM6 bit 31 (K1-off enable).
- **Failure scenario:** on the I217-LM and I219-V, frames are lost intermittently at K1 entry and exit (idle-to-busy transitions). At 10/100, a too-short PLL clock-gate time causes link flaps.
- **Fix sketch:** put this in the same link-change worker as E11. Write FEXTNVM4 unconditionally on each check, and apply the speed-dependent PHY writes when the link is up.

#### E13. I218 low-power-platform IDs lack the 1 Gb/s K1 DMA-stall workaround
- **Location:** `e1000e.c:889-894`. 0x155A, 0x1559, 0x15A2 and 0x15A3 are all lumped into K_LPT. Upheld 3/3.
- **What is wrong:** nothing distinguishes these four IDs. There is no Kumeran register access (KMRNCTRLSTA, 0x0034) and no FEXTNVM6 access.
- **Correct hardware behaviour,** on these four IDs only:
  - **Link up at 1 Gb/s,** under the software flag:
    1. Read Kumeran K1_CONFIG (offset 0x07) and clear K1_ENABLE (bit 1).
    2. Wait about 10 µs.
    3. Set FEXTNVM6 bit 8 (REQ_PLL_CLK).
    4. Restore K1_CONFIG.
  - **Link down or 10/100:** clear FEXTNVM6 bit 8.
  - **PHY revision ≤ 5 at 10 Mb/s or 100 Mb/s half duplex:** also program the in-band link-status TX timeout (5×10 µs at 100 half, 50×10 µs at 10).
- **Failure scenario:** an I218-LM on a Haswell-ULT or Broadwell-U laptop at 1 Gb/s with K1 enabled misses two DMA completions. Receive write-backs then stop while the link stays up, and only a reset recovers it. Neither real target is one of these IDs.
- **Fix sketch:** add a per-ID quirk flag and implement the sequence in the link-change worker.

#### E14. Manageability ARP interception (MANC.ARP_EN) left enabled after the driver takes ownership
- **Location:** `e1000e.c:807` (DRV_LOAD set; MANC and MANC2H never touched). Upheld 2/3.
- **What is wrong:** when manageability pass-through is active, firmware commonly leaves MANC.ARP_EN (bit 13) set. ARP requests matching the management filter are then diverted away from the host ring.
- **Correct hardware behaviour:** pass-through is active when MANC.RCV_TCO_EN is set, the FWSM mode field reports pass-through/iAMT, and manageability clock gating is off. In that case:
  - **On takeover:** read MANC (0x5820), clear ARP_EN, set EN_MNG2HOST (bit 21), and OR the port 623/664 bits (bits 5 and 6) into MANC2H (0x5860).
  - **On release:** reverse this.
- **Failure scenario:** on an I217-LM vPro laptop with AMT in shared-IP mode, firmware stops answering ARP once DRV_LOAD is set, while the hardware still diverts ARP requests. The host stops being reachable by IPv4 once peers' ARP caches expire, and the drops show up in no counter.
- **Fix sketch:** add the takeover writes right after DRV_LOAD, gated on the pass-through condition. One skeptic argued iAMT shared-IP mode leaves MANC alone, so test the gate condition on real hardware.

#### E15. No transmit-hang or link-loss recovery; a stalled TX queue never drains
- **Location:** `e1000e.c:481`. Upheld 3/3.
- **What is wrong:**
  - A timed-out DD wait only counts a drop.
  - LSC only logs, TXDW is ignored, and there is no watchdog.
  - `e2k_setup_tx_ring` and TCTL programming run only at attach.
- **Correct hardware behaviour:**
  - Descriptors queued before a link loss are not completed while the link is down.
  - A transmit unit hang shows up as TDH stopped with TDT ahead of it.
  - In either case the required recovery is: disable the transmitter, run the I219 descriptor-ring flush if config 0xE4 requests it, do the reset sequence from E4, then reprogram the rings, TXDCTL, TIPG and TCTL.
- **Failure scenario:** a switch-port flap or cable pull while frames are queued, or an I219 hang under load, leaves every later send failing with -EIO after a spin with IRQs off, until reboot.
- **Fix sketch:**
  - A periodic watchdog: if TDH has not moved for 2-5 s while TDH != TDT, or the link dropped with work pending, reinitialise.
  - Gate xmit on STATUS.LU.

#### E16. The TX slot wait spins for an uncalibrated count with interrupts disabled
- **Location:** `e1000e.c:476`. Upheld 3/3, two reports merged.
- **What is wrong:**
  - The DD wait runs inside `spinlock_acquire_irq`, so interrupts are off whatever the caller's state was.
  - It uses a fixed 1,000,000 or 10,000 iterations, not a time bound. At -O0 that is about 1.5-2 ms or 15-20 µs.
  - The comment's premise ("long spin is safe when the caller had interrupts on") is wrong.
- **Correct hardware behaviour:** retiring the oldest frame takes up to one frame time (about 1.2 ms at 10 Mb/s, 123 µs at 100 Mb/s) plus half-duplex backoff. A transmit path must not busy-wait for that with interrupts masked.
- **Failure scenario:**
  - Frames sent from the RX ISR get a 15-20 µs budget, which is shorter than one frame time at 10/100. ARP replies, ICMP echo and TCP ACKs are therefore dropped whenever the ring holds a frame in that slot.
  - With TX stalled (link down), every send masks interrupts for milliseconds.
- **Fix sketch:** return `-ENOBUFS` at once when the slot is still owned (see E1). Reclaim on TXDW or on the next xmit, and never spin inside the IRQ-off lock.

#### E17. A timed-out software-flag request is never withdrawn
- **Location:** `e1000e.c:287-293`. Callers return -2 without releasing (331, 341). Upheld 3/3, three reports merged.
- **What is wrong:** the second loop writes the SWFLAG / MDIO_SW_OWNERSHIP bit (0x20) every millisecond. On timeout it returns -1 with the request still written.
- **Correct hardware behaviour:** writing the bit is a request that the arbiter may grant later. If ownership is not granted within the timeout, software must write the bit back to 0 before reporting failure. This applies to both the 82574 MDIO-ownership bit and the PCH SWFLAG.
- **Failure scenario:**
  1. On an I217-LM with AMT, ME holds the semaphore for more than 1 s during bring-up.
  2. The request is granted after the driver has given up, and the flag stays owned for the rest of the boot.
  3. ME and the hardware PHY/LCD configuration loader are locked out of the PHY.

  The reset path (789/794) is not affected, because CTRL.RST and the unconditional release clear it.
- **Fix sketch:**
  ```c
  if (ms >= E1K2_SWFLAG_MS) {
      e2k_write(R_EXTCNF_CTRL, e2k_read(R_EXTCNF_CTRL) & ~EXTCNF_SWFLAG);
      return -1;
  }
  ```
  Also write the request once and then poll, instead of rewriting it every iteration.

#### E18. I219 legacy INTx is used without disabling IOSF sideband clock gating
- **Location:** `e1000e.c:853` (the interrupt ladder can settle on IRQ_LINE or IRQ_ROUTED). Upheld 3/3.
- **What is wrong:** FEXTNVM7 and FEXTNVM9 are never touched, so nothing specific to legacy-interrupt mode is applied on K_SPT.
- **Correct hardware behaviour:**
  - On Sunrise Point and later, INTx assert and deassert are IOSF sideband messages.
  - In legacy-interrupt mode only, after every MAC reset, set FEXTNVM7 (0x000E4) bit 2 (SIDE_CLK_UNGATE) and FEXTNVM9 (0x5BB4) bits 11 and 12 (IOSFSB_CLKGATE_DIS, IOSFSB_CLKREQ_DIS).
  - None of this is needed with MSI.
- **Failure scenario:**
  1. MSI fails to verify on the I219-V, and the driver falls back to INTx.
  2. While idle, an assert message is lost to clock gating.
  3. ICR holds RXT0 with no interrupt, and with no RX polling, receive stops permanently. A lost deassert instead causes an interrupt storm on the line.
- **Fix sketch:** on K_SPT, set both registers before installing or probing any non-MSI interrupt kind (or unconditionally on K_SPT before the ladder), and re-apply after each reset.

### Low

#### E19. 82574/82583 required init bits and PCIe errata workarounds missing
- **Location:** `e1000e.c:807`. Upheld 3/3.
- **What is wrong:** only CTRL_EXT bit 22 and TXDCTL bit 22 are set.
- **Correct hardware behaviour,** on the 82574/82583 after reset:
  - CTRL: clear bit 29.
  - CTRL_EXT: clear bit 23, set bit 22.
  - TARC0: clear bits 30:27, set bit 26.
  - GCR (0x5B00): set bit 22.
  - GCR2 (0x5B64): set bit 0. This is the workaround for unreliable PCIe completions, which are worst under ASPM.
  - Disable ASPM L0s/L1 in PCIe Link Control when Link Capabilities advertises them.
- **Failure scenario:** on a real 82574L with ASPM enabled by the BIOS, Tx timeouts or RX stalls occur. QEMU does not reproduce this.
- **Fix sketch:** add a K_82574 post-reset block with these writes, plus the Link Control change in config space. Whether GCR bit 27 (L1-without-L0s) must be cleared was not established, so omit it.

#### E20. NFS receive filtering left enabled on PCH MACs (descriptor write-back erratum)
- **Location:** `e1000e.c:835`. Upheld 3/3.
- **What is wrong:** RFCTL.NFSW_DIS (bit 6) and NFSR_DIS (bit 7) are left at their reset value of 0.
- **Correct hardware behaviour:** ICH/PCH MACs can corrupt RX descriptor write-back on NFSv2/UDP traffic. Set both bits after every reset, before enabling RX.
- **Failure scenario:** NFSv2/UDP frames addressed to the host are dropped or delivered with the wrong length.
- **Fix sketch:** `rfctl = (rfctl & ~RFCTL_EXSTEN) | (e2k_is_pch() ? 0xC0 : 0);`

#### E21. Descriptor rings accessed through non-volatile pointers with no barriers
- **Location:** `e1000e.c:477` (DD poll); stores at 486-497. Upheld 2/3, two reports merged.
- **What is wrong:**
  - The DD poll loop contains no volatile access and no barrier. When built at -O2, GCC deletes the loop, so any in-flight slot becomes an immediate -EIO.
  - Descriptor stores are not ordered against the TDT write.
- **Correct hardware behaviour:**
  - DD is written by device DMA, so every poll must re-read memory.
  - All descriptor fields must be in memory before the TDT doorbell. On x86 a compiler barrier is enough.
- **Failure scenario:** the bug is latent today because the kernel builds at -O0. It appears as soon as optimisation is enabled.
- **Fix sketch:** read status through a `volatile` view and put `__asm__ volatile("" ::: "memory")` before the TDT and RDT writes.

#### E22. Interrupt probe accepts a foreign interrupt on a shared or misrouted INTx line
- **Location:** `e1000e.c:683-695` with 439-444. Upheld 2/3.
- **What is wrong:** any handler invocation that sees INT_ASSERTED during the window counts as proof. The device is deliberately asserting during the window, so a foreign interrupt on the same vector passes the test.
- **Correct hardware behaviour:** a valid probe has three steps:
  1. With IMS=0 and no causes pending, the line stays quiet.
  2. After the ICS write, an interrupt arrives with LSC set in ICR.
  3. After the ICR read and IMC, the line goes quiet again.
- **Failure scenario:** a wrong routed GSI shared with USB is reported as "verified", so RX is driven only by USB traffic. MSI is not affected.
- **Fix sketch:** add a baseline window and a post-ack quiet check, and require `icr & ICR_LSC` on the invocation that counts.

#### E23. No interrupt throttling; TXDW unmasked although the handler ignores it
- **Location:** `e1000e.c:163`. Upheld 3/3.
- **What is wrong:** ITR (0x00C4), RDTR and RADV all stay 0, so there is one interrupt per frame. TXDW is unmasked, RS is set on every descriptor, and the handler does nothing for TXDW.
- **Correct hardware behaviour:** a single vector typically runs with ITR ≈ 488 (about 8000 interrupts/s). Causes the handler does not service should stay masked.
- **Failure scenario:** the interrupt rate doubles during bulk TX, and per-interrupt overhead grows under floods (the livelock itself is E9).
- **Fix sketch:** `e2k_write(R_ITR, 488)` and remove `ICR_TXDW` from `E1K2_IMS`.

#### E24. The link-state report after unmask races the LSC handler
- **Location:** `e1000e.c:874-875` versus the ISR at 452. Upheld 2/3.
- **What is wrong:** IMS (including LSC) is unmasked at 866 before an unserialised read-compare-store of `link_status`.
- **Correct hardware behaviour:** the initial STATUS.LU sample must be taken before LSC is unmasked, or must be excluded against the handler.
- **Failure scenario:** the log shows a stale "link down", and the next real link-down is not reported. Only logging is affected.
- **Fix sketch:** sample the link before `R_IMS`, or wrap the call in an IRQ-disabling lock.

#### E25. PHY register value 0xFFFF accepted; the BMCR write then sets reset, loopback and isolate
- **Location:** `e1000e.c:371`, `e1000e.c:377`. Upheld 3/3.
- **What is wrong:**
  - ID or BMCR reads of all ones are treated as valid.
  - MDIC does not check that the echoed register field (bits 20:16) matches the register requested.
  - An all-ones BMCR has PDOWN set, so the driver writes back 0xF7FF. That sets RESET, LOOPBACK, ISOLATE and the forced speed/duplex bits.
- **Correct hardware behaviour:** an ID of 0xFFFF, or a register-echo mismatch, means the PHY is not accessible. Retry, then leave the PHY untouched.
- **Failure scenario:** an unauthorised PHY soft reset while firmware owns the PHY, or a link flap. Usually the write simply goes nowhere.
- **Fix sketch:**
  - Reject `id1 == 0xFFFF || id1 == 0` and register-echo mismatches.
  - Clear only PDOWN, and only from a BMCR value that has been validated.

#### E26. Attach failure paths leave DMA running, leak memory and leave DRV_LOAD asserted
- **Location:**
  - `e1000e.c:772-774` (allocation failure)
  - `e1000e.c:815-816` (no MAC address)
  - `e1000e.c:853-854` (no interrupt)
  - `e1000e.c:807` (DRV_LOAD set)
  - Detach at 933 is a no-op.

  Upheld 2/3, two reports merged.
- **What is wrong:** after bus mastering is enabled, every failure returns -1 without cleanup. On the interrupt-failure path, RCTL.EN and TCTL.EN are already set with the RX ring armed, and DRV_LOAD stays set.
- **Correct hardware behaviour:** on a failed attach, in this order:
  1. IMC all ones; clear RCTL.EN and TCTL.EN.
  2. Reset the MAC or clear COMMAND.BME.
  3. Clear CTRL_EXT.DRV_LOAD so firmware reclaims the port.
  4. Free the DMA memory and unmap BAR0.
- **Failure scenario:** after a rare attach failure, the NIC keeps DMA-ing up to 31 frames into leaked memory, and AMT believes a host driver owns the port.
- **Fix sketch:** add a single `fail:` unwind label in `e2k_setup`.

#### E27. The ISR treats an all-ones ICR read as a real interrupt
- **Location:** `e1000e.c:440-443`. Upheld 2/3.
- **What is wrong:** an ICR of 0xFFFFFFFF passes both checks. The handler then drains, logs a false "link up, 1000 Mb/s full duplex", and claims the interrupt.
- **Correct hardware behaviour:** ICR can never legitimately read as all ones. It means the function is not responding, so the interrupt is not ours and no register should be touched.
- **Failure scenario:** after the device stops responding, every interrupt on a shared line is wrongly claimed and a false link message is logged.
- **Fix sketch:** `if (icr == 0 || icr == 0xFFFFFFFFu) return 0;`

#### E28. 82574/82583 reset lacks MDIO ownership and the post-reset NVM/PHY-config wait
- **Location:** `e1000e.c:786-792`. Upheld 2/3.
- **What is wrong:** K_82574 gets a bare CTRL.RST followed by a fixed 20 ms wait. EECD.AUTO_RD is never polled.
- **Correct hardware behaviour:**
  1. Take EXTCNF_CTRL bit 5 before RST and release it right after.
  2. Poll EECD (0x0010) bit 9 for up to 10 ms.
  3. Wait 25 ms more for the NVM-driven PHY configuration before touching the NVM or PHY.
- **Failure scenario:** on real 82574 silicon, PHY bring-up races the NVM PHY-init sequence. QEMU resets instantly, so it never shows.
- **Fix sketch:** add these steps to the non-PCH reset branch.

#### E29. Packet-buffer ECC and memory-error handling never enabled on Lynx Point+
- **Location:** `e1000e.c:809-810`. Upheld 2-3/3, three reports merged.
- **What is wrong:** PBECCSTS (0x0100C) and CTRL.MEHE (bit 19) are never written.
- **Correct hardware behaviour:** on Lynx Point and later, after reset, set PBECCSTS bit 16 (ECC enable) and CTRL bit 19. Handling ECCER (ICR bit 22) with a MAC reset is optional hardening.
- **Failure scenario:** an on-die SRAM bit flip corrupts a frame with no indication. TCP and UDP checksums still catch most cases.
- **Fix sketch:** inside the `e2k_is_pch()` post-reset block, OR `0x00010000` into PBECCSTS and `0x00080000` into CTRL.

#### E30. Frames shorter than 17 bytes passed to hardware despite the PSP minimum
- **Location:** `e1000e.c:462-463`. Upheld 2/3.
- **What is wrong:** any length from 1 to 16 is accepted. AF_PACKET (root only) can submit such frames.
- **Correct hardware behaviour:** hardware short-packet padding is specified only for buffers of at least 17 bytes. Shorter frames must be padded in software.
- **Failure scenario:** a runt or malformed frame on the wire, or possibly a TX stall.
- **Fix sketch:** zero-fill the bounce buffer to 60 bytes and program that length, as the r8168 driver already does.

#### E31. The probe's ICR reads discard RX causes latched after the receiver was enabled
- **Location:** `e1000e.c:687` and `e1000e.c:693` (RCTL.EN is set at 851, before the probe at 853). Upheld 2/3.
- **What is wrong:** frames arriving during the probe window have their RXT0 cleared by read-to-clear ICR. The final unmask at 866 then finds no pending cause, so those descriptors sit until a later frame raises a new RX interrupt.
- **Correct hardware behaviour:** only clear stale causes while the receiver is disabled, or service the ring after the unmask.
- **Failure scenario:** delivery of a few early frames is delayed. This heals itself on the next RX interrupt.
- **Fix sketch:** enable RCTL after the interrupt is proven, or write `ICS = ICR_RXT0` after the final IMS write.

#### E32. Autonegotiation advertisement only fixed when the PHY was powered down
- **Location:** `e1000e.c:376`. Upheld 3/3.
- **What is wrong:** ANAR (reg 4) and 1000BASE-T control (reg 9) are never written. BMCR is written only when PDOWN is set.
- **Correct hardware behaviour:** copper link setup always advertises 10/100 half and full plus 1000 full, then sets BMCR.ANENABLE and BMCR.ANRESTART.
- **Failure scenario:** a speed or duplex forced by a previous driver survives a warm reboot, giving a duplex mismatch against an autonegotiating switch.
- **Fix sketch:** write reg 4, reg 9 and BMCR unconditionally (gated on FWSM.RSPCIPHY on PCH).

#### E33. LTR/OBFF registers never programmed on Lynx Point+
- **Location:** `e1000e.c:452`. Upheld 2/3.
- **What is wrong:** LTRV (0x00F8), SVT (0x00F4) and SVCR (0x00F0) are never written.
- **Correct hardware behaviour:**
  - **On link up:**
    1. Compute latency = (PBA.RXA × 1024 − 2 × max frame) × 8 / speed.
    2. Encode it as an LTR value/scale and clamp it to the platform maximum in config space.
    3. Write LTRV for snoop and no-snoop, with the requirement bits and SEND set.
    4. Set SVT.OFF_HWM and SVCR.OFF_EN|OFF_MASKINT.
  - **On link down:** write LTRV with the requirement bits clear.
- **Failure scenario:** RX overruns during deep package C-states. This is latent while the kernel idles with HLT only.
- **Fix sketch:** add this to the link-change worker before any deep-idle support lands.

#### E34. Function never moved to D0 before MMIO bring-up
- **Location:** `e1000e.c:746-751`. Upheld 2/3.
- **What is wrong:** PMCSR is never read or written, here or anywhere in the PCI layer. The PM capability ID is defined but unused.
- **Correct hardware behaviour:** if PMCSR.PowerState is not D0, write D0, wait 10 ms, and clear PME_Status and PME_En. Treat an all-ones STATUS as "inaccessible".
- **Failure scenario:** a NIC handed over in D3hot without a platform reset fails attach with a misleading "no valid MAC address" after a 1 s software-flag timeout.
- **Fix sketch:** do the D0 transition generically in the PCI core.

#### E35. PCH bring-up never forces snooped, strictly ordered DMA
- **Location:** `e1000e.c:807`. Upheld 2/3.
- **What is wrong:** the GCR no-snoop bits (0-5) and CTRL_EXT.RO_DIS (bit 17) are left as found.
- **Correct hardware behaviour:** on integrated MACs other than ICH8, clear GCR bits 0-5 and set RO_DIS after reset.
- **Failure scenario:** only reachable with a non-default NVM or firmware state. The reset defaults are already snooped.
- **Fix sketch:** add two writes in the PCH post-reset block.

#### E36. The firmware-line INTx path never clears the PCI Command INTx Disable bit
- **Location:** `e1000e.c:631-633`, `e1000e.c:746-749`. Upheld 3/3.
- **What is wrong:**
  - The Command write keeps bit 10 as it was found.
  - Only the routed path clears bit 10, and its teardown sets it again.
- **Correct hardware behaviour:** a function cannot assert INTx while bit 10 is set. Clear it before any legacy INTx use or probe.
- **Failure scenario:** a stale bit 10 makes the correct firmware line fail its probe, leaving the driver on an unverified MSI that does not work.
- **Fix sketch:** clear `PCI_COMMAND_INTX_DISABLE` in the IRQ_LINE branch.

#### E37. I217/I219 PHY copper setup incomplete (CRS-on-transmit, downshift, MDI-X)
- **Location:** `e1000e.c:353-384`. Upheld 3/3.
- **What is wrong:** PHY page 0 regs 22 and 18 are never written.
- **Correct hardware behaviour:**
  - Reg 22 |= 0x8000 (assert CRS on transmit; required for half duplex) | 0x0C00 (automatic downshift).
  - Reg 18: set the MDI-X field (mask 0x0600) to 0x0400 (auto).
  - Use page-0 page-select semantics for registers above 15.
- **Failure scenario:** late collisions on half-duplex hubs, or no link over 2-pair cabling.
- **Fix sketch:** add these writes to copper link setup.

#### E38. Receive-address entries other than RAR0 never cleared
- **Location:** `e1000e.c:821`. Upheld 2/3.
- **What is wrong:** RAL/RAH 1..N are never zeroed. That is entries 1..14 on the 82574, and the 11 shared entries on Lynx Point+.
- **Correct hardware behaviour:** zero every entry from 1 to N so Address Valid is clear. On Lynx Point+, respect the FWSM write-lock field (bits 9:7).
- **Failure scenario:** stale foreign unicast filters waste ring slots and leak frames to raw listeners.
- **Fix sketch:** add a loop after `e2k_write_rar0`.

#### E39. The 1518-byte limit ignores the hardware-appended FCS
- **Location:** `e1000e.c:463` (IFCS set at 494). Upheld 3/3.
- **What is wrong:** with IFCS set, a 1518-byte buffer becomes a 1522-byte frame on the wire, and the driver does not check for an 802.1Q tag.
- **Correct hardware behaviour:** with insert-FCS, the buffer may be at most 1514 bytes, or 1518 when bytes 12-13 hold the TPID 0x8100.
- **Failure scenario:** raw senders produce untagged giants that strict receivers discard, while xmit reports success.
- **Fix sketch:** `max = (frame[12]==0x81 && frame[13]==0x00) ? 1518 : 1514;`

---

## r8168

### High

#### R1. PHY never woken, reset or restarted at bring-up
- **Location:**
  - `r8168.c:603` (CR.RST only)
  - `r8168.c:760-764` (eth0 registered UP|RUNNING regardless of link)
  - `r8168.c:116-121` (the PHY-wake quirk deliberately dropped)

  Upheld 3/3, three reports merged.
- **What is wrong:** CR.RST resets only the MAC. The driver has no PHY access path, never selects PHY page 0, never clears PHY reg 0x0E, never clears BMCR power-down or isolate, never restarts autonegotiation, and never writes PMCH (0x6F).
- **Correct hardware behaviour,** after the MAC reset and before enabling the datapath:
  1. On PMCH-gated steppings (8168D, 8168E, 8105E/8106E/8401E/8402), set PMCH bit 7. On the 8401E also clear bit 3 of 0xD1.
  2. On every 8168 stepping, including the 8168G/GU/H, write PHY reg 0x1F = 0 (page 0), then reg 0x0E = 0 (leave power-save).
  3. BMCR reset (this clears PDOWN and ISO). Advertise 10/100/1000 in ANAR and the 1000BASE-T control register with auto-MDIX on. Then BMCR.ANENABLE|STARTNEG.

  PMCH bit 7 is not the PHY power gate on the 8168H.
- **Failure scenario:**
  1. The RTL8168H is warm-rebooted after a shutdown path that powered the PHY down with WoL off, or after a UEFI "LAN off" or network-stack stop.
  2. PHYstatus shows no link indefinitely, and DHCP times out.
  3. eth0 is reported RUNNING with no error.

  A clean cold boot usually works.
- **Fix sketch:**
  - Add a PHY accessor: PHYAR (0x60) on older parts, the GPHY OCP window (0xB8) on the 8168G generation.
  - Implement the wake / reset / autonegotiation sequence above, with PMCH handling as a stepping quirk.
  - Derive the RUNNING flag from PHYstatus and LINKCHG instead of hard-coding it.

#### R2. MSI handler acks only the bits it read, with IMR still live, so interrupts are lost permanently
- **Location:** `r8168.c:263-270`. Upheld 3/3, three reports merged.
- **What is wrong:** ISR is read once and the same value written back. IMR is never masked or rewritten, and ISR is never re-read before return.
- **Correct hardware behaviour:**
  - In MSI mode a message is sent only when (ISR & IMR) goes from zero to non-zero.
  - The handler must either write IMR = 0 on entry and restore the mask on exit (restoring it while bits are still pending produces a fresh message), or loop until (ISR & IMR) == 0.
- **Failure scenario:**
  1. On MSI, which is the path the UEFI target uses, a TOK or ROK event latches between the ISR read and the write-1-to-clear.
  2. That bit is never cleared, so (ISR & IMR) never returns to zero and no further MSI is ever sent.
  3. RX fills, RDU latches silently, and eth0 goes deaf until reboot. TX keeps working because it reclaims by polling.
- **Fix sketch:**
  ```c
  rt_w16(R_IMR, 0);
  for (;;) {
      uint16_t isr = rt_r16(R_ISR);
      if (isr == 0xFFFF || !(isr & R8168_IMR)) break;
      rt_w16(R_ISR, isr);
      /* service isr ... */
  }
  rt_w16(R_IMR, R8168_IMR);   /* re-arms the message edge; also flushes posted writes */
  ```

### Medium

#### R3. The 0x8136 Fast Ethernet (810xE) family is claimed but falls into 8168G quirks, losing RXENB and the PHY wake
- **Location:** `r8168.c:799` (ID claim), `r8168.c:571` (unknown-stepping default). Upheld 3/3, four reports merged.
- **What is wrong:**
  - No 810xE TCR revision is in the table, so every one gets MACSTAT|RXDV_GATED|TXRXEN_LATER|EARLYOFFV2.
  - The 8100E/8101E then get CPCR without RXENB (696), so the RX descriptor engine never starts.
  - Every pre-8168G part gets the late TE/RE ordering, and the MISC bit-19 and early-off writes.
  - The 8105E, 8106E, 8401E and 8402 never get their PMCH PHY power-up.
- **Correct hardware behaviour:**

  | Stepping (TCR rev) | C+ command word | Other requirements |
  |---|---|---|
  | 8100E (0x30800000, 0x38800000), 8101E (0x34000000) | RXENB\|TXENB, no MACSTAT_DIS | — |
  | 8102E (0x34800000), 8102EL (0x24800000, 0x24C00000), 8103E (0x34C00000) | MACSTAT_DIS\|TXENB | TE/RE before TCR/RCR; no RXDV write |
  | 8401E (0x24000000), 8105E (0x40800000, 0x40C00000), 8402 (0x44000000), 8106E (0x44800000) | MACSTAT form | Also need PMCH bit 7 set (8401E: also clear 0xD1 bit 3) |

  Only 8168G-generation and later parts take the RXDV-gate clear and the enable-after-config order.
- **Failure scenarios:**
  - An RTL8101E attaches as "UNKNOWN stepping". It transmits but never receives, so ARP and DHCP time out.
  - An 8106E whose PHY was powered down through PMCH has no link.
- **Fix sketch:**
  - Add table entries with these quirk sets plus a PMCH-wake quirk, or stop claiming 0x8136 until this is done.
  - Make the unknown-stepping default for 0x8136 the pre-8168G sequence, or refuse to attach.
  - RTL8106E-US/8107E parts that report 0x50800000/0x54000000 already match existing entries.

#### R4. Unbounded RX drain inside the ISR can livelock the CPU
- **Location:** `r8168.c:212`. Upheld 3/3, two reports merged.
- **What is wrong:** the `for (;;)` loop exits only on an OWN descriptor. Each descriptor is handed back at once, and the full input stack (including replies through xmit) runs with IF=0 before the EOI.
- **Correct hardware behaviour:** the RX DMA engine refills a descriptor as soon as OWN is returned, so the work done in one interrupt must be bounded. Ring exhaustion is safe: RDU latches and reception resumes.
- **Failure scenario:** a broadcast or small-frame flood on gigabit keeps the loop running, so the timer and scheduler starve and the machine appears hung until the flood stops.
- **Fix sketch:** at most 64 descriptors per call. Then mask the RX bits in IMR and defer the rest, or return and let the latched ROK re-raise the interrupt.

#### R5. No TX re-kick on TOK/TDU, so queued frames strand when a doorbell is ignored
- **Location:** `r8168.c:285` (TOK/TER ignored), `r8168.c:476` (the only TPPOLL NPQ write). Upheld 3/3, two reports merged.
- **What is wrong:** the normal-priority poll is written once per frame. TDU is not in IMR, and the ISR never rings the doorbell again.
- **Correct hardware behaviour:**
  - On PCIe RTL8168/8111 parts, a TPPOLL NPQ write that arrives while the transmitter is busy can be ignored.
  - Write NPQ (0x40 to 0x38) again whenever ISR shows TX OK or TX descriptor unavailable.
  - Optionally re-arm a TimerInt/TCTR backstop while descriptors remain owned.
- **Failure scenario:** the last frame of a burst sits with OWN set until some unrelated later xmit. The visible result is ARP/DHCP retries and TCP retransmit-timeout stalls. It heals on the next send.
- **Fix sketch:** add `INT_TDU` to IMR. In the ISR, `if (isr & (INT_TOK|INT_TDU)) rt_w8(R_TPPOLL, TPPOLL_NPQ);`, ordered with respect to `rt_tx_lock`.

#### R6. Multicast hash left at zero, so IPv6 neighbor discovery and all-hosts traffic are dropped
- **Location:** `r8168.c:623-624` (MAR = 0), `r8168.c:749` (AM set). Upheld 3/3.
- **What is wrong:**
  - With AM set, multicast passes only if the CRC-derived 6-bit index hits a set bit in the 64-bit MAR hash.
  - The hash is opened only for IPv4 joins, and closed again when the last group is left.
- **Correct hardware behaviour:**
  - The hash must cover every group the host listens to: ff02::1, each solicited-node group, and 224.0.0.1. Alternatively open it fully (both words 0xFFFFFFFF).
  - On PCIe parts the two hash words are stored swapped and byte-reversed.
- **Failure scenario:** on an RTL8168H with IPv6 and no IPv4 group joined, Neighbor Solicitations and Router Advertisements are filtered out, so inbound IPv6 fails. IGMP general queries are lost the same way.
- **Fix sketch:** write `0xFFFFFFFF` to both MAR words at setup and leave them open (the stack filters by membership), or compute per-group hashes with the PCIe word and byte swap.

#### R7. SERR unmasked but never acted on; no reinit or watchdog path
- **Location:** `r8168.c:87-88` (INT_SERR in IMR), `r8168.c:285`. Upheld 3/3.
- **What is wrong:** SERR is acknowledged and ignored, and the only soft reset is at attach.
- **Correct hardware behaviour:**
  - ISR bit 15 means a fatal bus error during DMA. Respond with a full reinitialisation:
    1. Stop TE/RE.
    2. CR.RST.
    3. Reprogram CPCR, TNPDS/RDSAR, TCR/RCR, the RX filter and IMR.
    4. Rebuild both rings and reclaim every TX descriptor.
  - A transmit watchdog (descriptors stuck for about 5 s) should take the same path.
  - TER alone is a per-frame error, and treating it as a completion is fine.
- **Failure scenario:** after a PCIe error the TX and RX engines halt and eth0 is dead until reboot. Each send spins with IRQs off before returning -EIO.
- **Fix sketch:** factor the ring and register programming out of `r8168_setup` into `r8168_reinit()`. Call it from deferred context on SERR and from a periodic TX watchdog.

### Low

#### R8. Firmware-enabled PCIe ASPM L0s/L1 and CLKREQ left active
- **Location:** `r8168.c:584-587`. Upheld 3/3.
- **What is wrong:** Link Control is never inspected, so firmware's ASPM and Enable Clock PM settings stay on.
- **Correct hardware behaviour:** without chip-specific ASPM and CLKREQ tuning, find the PCIe capability (0x10). If Link Capabilities bits 11:10 are non-zero, clear Link Control bits 1:0 and bit 8.
- **Failure scenario:** intermittent RX stalls or TX descriptors stuck with OWN set on the Whiskey Lake laptop.
- **Fix sketch:** add the config-space writes before the datapath is enabled.

#### R9. DMA descriptors accessed without volatile or barriers
- **Location:** `r8168.c:169-183` (plain ring pointers), `r8168.c:439-449` (OWN poll), `r8168.c:467-476` (descriptor stores and the doorbell). Upheld 2-3/3, two reports merged.
- **What is wrong:** at -O2 GCC removes the OWN wait loop, so a ring-full condition becomes an immediate drop. Store ordering against TPPOLL is not guaranteed by the language.
- **Correct hardware behaviour:**
  - Re-read OWN from memory on every poll.
  - Write address, opts2 and length before OWN, and OWN before the doorbell.
  - On x86 compiler barriers are sufficient.
- **Failure scenario:** latent today because the kernel builds at -O0.
- **Fix sketch:** volatile status reads, plus compiler barriers before the OWN store and before the TPPOLL write.

#### R10. The handler ownership test and the interrupt self-test are too weak
- **Location:** `r8168.c:264-266` (any non-zero ISR counted), `r8168.c:357-370` (probe). Upheld 2/3, four reports merged.
- **What is wrong:**
  - 0xFFFF (device gone) is claimed and acted on.
  - Latched-but-masked bits count as ours.
  - SWInt latches in ISR whatever route the interrupt takes, so any foreign invocation on a wrongly routed vector during the 50 ms window "verifies" it.
  - There is no quiet-window baseline and no post-ack quiet check.
- **Correct hardware behaviour:**
  - 0xFFFF means the device is not responding: not ours, no writes.
  - Claim only `isr & enabled_mask`.
  - A probe must show the line quiet with IMR=0, an invocation that sees SWInt after the force, and the line quiet again after the ack and IMR=0.
- **Failure scenario:** a wrong routed GSI shared with a device that is asserting is reported as "verified", leaving eth0 on a line the NIC never drives. This is rare and only happens after MSI has failed.
- **Fix sketch:** early return on 0xFFFF and on `!(isr & rt_r16(R_IMR))`, plus the three-phase probe.

#### R11. Station address taken from IDR0-5 with no validity check
- **Location:** `r8168.c:619-620`. Upheld 3/3.
- **What is wrong:** an all-zero, all-ones or group-bit address read from IDR is registered as-is.
- **Correct hardware behaviour:** a blank EEPROM or eFuse autoload yields zeros in IDR. Replace an invalid address with a random locally administered unicast address and program it back into IDR with CFG9346 unlocked.
- **Failure scenario:** on a board with a blank autoload area, eth0 has MAC 00:00:00:00:00:00 and DHCP fails.
- **Fix sketch:** validate the address, generate a replacement, and call `r8168_set_hwaddr()`.

#### R12. Setup failure paths leak DMA memory and leave the NIC bus-mastering
- **Location:** `r8168.c:634-656`, `r8168.c:757-758`. Upheld 2/3.
- **What is wrong:** none of the early returns free anything. After IRQ setup fails, TE/RE, the accept bits and BME stay on with an armed RX ring.
- **Correct hardware behaviour:** set IMR=0, clear CR TE|RE (or CR.RST), clear BME, then free the rings.
- **Failure scenario:** up to 64 frames are DMA'd into leaked memory after a rare total interrupt failure.
- **Fix sketch:** add an unwind label, and defer TE/RE until the interrupt is settled.

#### R13. The firmware-line INTx path never clears the PCI Command INTx Disable bit
- **Location:** `r8168.c:306-307`, `r8168.c:586-587`. Upheld 2/3.
- **What is wrong:** bit 10 is inherited. The routed-path teardown sets it, and the fallback loop can then install the line with INTx disabled.
- **Correct hardware behaviour:** clear Command bit 10 before using or probing legacy INTx.
- **Failure scenario:** a working firmware line is rejected, or installed dead. This is unreachable on the UEFI target, which reports Interrupt Line 0xFF.
- **Fix sketch:** clear bit 10 in the LINE branch.

#### R14. Function never moved to D0 before MMIO bring-up
- **Location:** `r8168.c:584-603`. Upheld 2/3.
- **What is wrong:** PMCSR is not checked.
- **Correct hardware behaviour:** find the PM capability, write D0 if needed, wait 10 ms, then re-check the BARs and Command register.
- **Failure scenario:** in D3hot the CR_RST write is dropped and CR reads 0xFF, so attach fails with "reset timed out".
- **Fix sketch:** handle this generically in the PCI core (shared with E34).

#### R15. The full-ring wait spins up to a million polls with interrupts disabled and never recovers
- **Location:** `r8168.c:439-448`. Upheld 3/3.
- **What is wrong:**
  - Inside the IRQ-off lock the driver polls OWN up to 1,000,000 times.
  - On timeout the slot is left owned and `tx_cur` is not advanced.
  - There is no reclaim on TOK and no watchdog.
- **Correct hardware behaviour:** a full ring is back-pressure, so return busy at once. A stalled engine needs a watchdog-driven reinit (R7).
- **Failure scenario:** with the transmitter stalled, every send masks IRQs for 1-5 ms and fails, indefinitely.
- **Fix sketch:** return `-ENOBUFS` immediately, and rely on R5 and R7 for progress and recovery.

#### R16. IntrMitigate (0xE2) and TimerInt (0x58) never initialised
- **Location:** `r8168.c:699-702`. Upheld 2/3.
- **What is wrong:** whatever firmware or a previous driver left in these registers stays in effect.
- **Correct hardware behaviour:** write IntrMitigate = 0x0000 next to the C+ command write, and TimerInt = 0.
- **Failure scenario:** leftover mitigation thresholds delay ROK on low-rate request/response traffic.
- **Fix sketch:** `rt_w16(0xE2, 0); rt_w32(0x58, 0);`

#### R17. The posted ISR acknowledge is not flushed before the EOI
- **Location:** `r8168.c:270`. Upheld 2/3.
- **What is wrong:** no device read follows the write-1-to-clear. On level-triggered routed INTx, the EOI can reach the I/O APIC before the deassert does.
- **Correct hardware behaviour:** read a device register after acknowledging so the posted write lands first.
- **Failure scenario:** a spurious second entry per interrupt on the INTx fallback path. It costs performance only.
- **Fix sketch:** covered by the ISR loop and IMR rewrite in R2.

---

## Areas examined and found correct

**e1000e**
- The RX tail is kept one behind the cursor (RDT = 31 of 32), so the RX ring never reads as full or empty by mistake.
- RX frames are validated (EOP, errors == 0, 14 ≤ length ≤ 1518) before going up the stack, and the descriptor is always recycled. A corrupted write-back cannot overflow a buffer.
- TX bounce buffers are 2048 bytes and the length check prevents overrun. DMA addresses use the correct virtual-to-physical (−0xC0000000) conversion.
- The I219 descriptor-flush trigger (config 0xE4, FLUSH_DESC_REQUIRED) and FEXTNVM11.DISABLE_MULR_FIX are present. Ordering and stale-ring issues are covered in E4 and E5.
- PCH MAC-only reset under the software flag, with FWSM.RSPCIPHY checked; CTRL_EXT bit 22, TXDCTL bit 22 and KABGTXD.BGSQLBIAS are set; CTRL.SLU/ASDE are set with FRCSPD/FRCDPLX cleared.
- RAR0 falls back to the pre-reset address and is written with Address Valid.
- The interrupt ladder tries MSI first, the INT_ASSERTED test applies on INTx paths, and IMS is unmasked only after the netdev exists.

**r8168**
- The 8168H stepping (0x54000000) is identified through the TCR hwrev mask and gets the correct quirk set: RXDV-gate clear, late TE/RE enable, MACSTAT_DIS|TXENB in the C+ command word.
- C+ command is written first, and descriptor base registers are written high half first.
- RX error bits are tested after the one-bit shift. RX acceptance requires FS|LS, no error summary and a sane length, and the trailing FCS is stripped. Descriptors are always recycled, and EOR is set on the last descriptor of both rings.
- TX writes OWN last and pads short frames to 60 bytes in software over zeroed memory.
- The station address is written with CFG9346 unlocked, high half first. RX accept bits are ORed in after RE. ISR is cleared before interrupt setup, and the soft-reset poll is bounded.
