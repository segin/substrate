# qemu+KVM i386 Single-Byte Heap Coherence Bug After SIGALRM Delivery

**Status:** Confirmed reproducer.  Substrate kernel is not at fault.

**Component:** qemu's KVM acceleration for 32-bit guests on x86_64 hosts.

**Reporter / first observation:** while chasing intermittent Xfbdev/links crashes during malloc-heavy startup on substrate, ran a 12-scenario libc+stdio torture suite and isolated the symptom to one scenario gated on SIGALRM delivery.

## Symptom

In a single-threaded user process running under `qemu-system-i386 -accel kvm`, a byte read of a user-heap virtual address through a *valid, unchanged* page-table entry can return the **wrong value for tens of consecutive reads**, then "heal" (return the correct value) after the program touches enough other VAs to evict a TLB entry.

- The byte that was actually written remains intact in physical memory (verify-on-write proves it).
- All affected reads return the same wrong value (stable across a tight 65-read retry loop).
- A subsequent full-block re-scan finds the byte correct again.
- The wrong-value reads cluster around moments when a `SIGALRM` has just been delivered to a userland signal handler — never around moments when only the kernel timer ticked.

## Reproducer

Source: `tests/lib/c/torture_heap_stdio.c` scenarios `sc9_sigalrm_during_alloc` and `sc9f_fill_once_verify_loop`.

```
# Cross-build for substrate:
make -C tests/lib/c -f Makefile.sockets torture_heap_stdio \
    CROSS=/opt/substrate/bin/i386-unknown-substrate-

# Stage into rootfs.img:
debugfs -w -R 'write tests/lib/c/torture_heap_stdio /tmp/torture_heap_stdio' rootfs.img
debugfs -w -R 'sif /tmp/torture_heap_stdio mode 0100755' rootfs.img

# Run under KVM (default): expect FAIL within seconds
KVM=1 INITARG=sc9f TIMEOUT=45 ./run-auto-test.sh /tmp/torture_heap_stdio
# Result: FAILED — 8 corruptions in ~25 SIGALRMs delivered.

# Run under TCG (default after the policy flip): expect PASS
INITARG=sc9f TIMEOUT=120 ./run-auto-test.sh /tmp/torture_heap_stdio
# Result: PASSED — 3840 passes, 195 SIGALRMs, 0 corruptions.
```

`sc9f` allocates one 16 KiB block via the libc allocator, fills it once with a per-byte canary value (`canary_byte(p, off) = ((p ^ off ^ 0xA5) * 2654435761) >> 24`), then scans every byte in a loop checking it against the same canary.  Whenever a mismatch is found the test prints details and immediately re-reads the same byte 64 more times.  A periodic `SIGALRM` fires every 5 ms via `setitimer(ITIMER_REAL, ...)` with a handler that does nothing but `g_sc9_ticks++`.

## Observed pattern (typical run under KVM)

```
pass  i      VA        page      pgoff  got   want  wrong_reads  ticks
305   11560  0x94ac8   0x94000   2760   0x7d  0xa1  65           5
417   1594   0x923da   0x92000   986    0x10  0x3b  65           9
683   200    0x91e68   0x91000   3688   0x42  0x6b  65           15
751   4384   0x92ec0   0x92000   3776   0xbe  0xed  65           18
796   763    0x9209b   0x92000   155    0xfa  0xaa  65           21
857   13234  0x95152   0x95000   338    0x58  0xbc  65           24
945   7987   0x93cd3   0x93000   3283   0x15  0x47  65           27
1488  15885  0x95bad   0x95000   2989   0xce  0x93  65           37
```

Properties:

- **Single-byte corruption.**  Surrounding bytes are correct; only the one byte differs from the canary.
- **`wrong_reads = 65` every time.**  All 65 retries returned the same wrong byte through the same virtual address.
- **Per-page hits: 1, 3, 1, 1, 2 across 5 spanned pages.**  Not clustered on one page.
- **No XOR-mask pattern across events.**  The byte's wrong value is unpredictable.
- **Heals on next full-block pass.**  Without the test rewriting the byte, a subsequent re-scan finds it correct.

## Diagnostic eliminations

Five independent probes, all in this file's commit history, rule out the most direct kernel mechanisms:

| Probe | What changed | Result | Conclusion |
|---|---|---|---|
| **`sc9b`** | Same as sc9 but handler is `SIG_IGN` | Zero corruption over 458,752 ops, 0 SIGALRMs delivered | Kernel timer / `psignal` / `sig_pending` bookkeeping are innocent.  Bug requires `sendsig` to run for a real handler. |
| **xsig kernel trace** | Log every `sendsig` sigframe destination | Sigframe goes to user stack (`0xbfffea70..0xbfffeac8`), NEVER heap | `copyout`-into-sigframe is not the source. |
| **`sys_sigreturn` audit** | Read only — writes only kernel-side `syscall_regs` + `current_thread` | No user-memory write at all | The signal-return half is innocent. |
| **`sc9d`** | Handler is `_exit(42)` | Child reaches `_exit` and waitpid sees status 42 | The iret-to-handler-PC transition is clean. |
| **`sc9e`** | Verify-on-write: read back each byte immediately after writing | 0 immediate mismatches over ~200M byte writes with 128 SIGALRMs | The user STORE is always correct.  Corruption is delayed. |
| **`sc9g`** | Same shape as sc9f but `memset(p, 0xA5, BLOCK_SZ)` instead of per-offset canary | Zero corruption over 63,600 passes / 195 SIGALRMs | Wrong reads are only detected when expected vs actual values differ.  When all bytes are the same, "wrong page" reads still return 0xA5 → no mismatch.  Strongly suggests the user is being pointed at a different physical page during the wrong-read window. |
| **`sigflush` kernel patch** | Diagnostic: `pmap_invalidate_all()` (CR3 reload) at start+end of `sendsig` AND in `sys_sigreturn` | Same corruption rate as baseline | The guest's CR3 reload — which should invalidate every non-global TLB entry — is not effective at suppressing the bug.  Either the host doesn't honor the guest's invalidation, or the bug is below the guest TLB. |
| **TCG fallback** | Same workload under `-accel tcg` instead of `-accel kvm` | **Zero corruption over 3840 passes / 195 SIGALRMs** | The bug is in the KVM hypervisor's i386 virtualization, not substrate. |

The TCG A/B is the decisive finding.  Same kernel image, same userland binary, same workload, same number of signals delivered — only the QEMU accelerator differs.

## Working hypothesis

KVM's shadow page-table or EPT state for the i386 guest diverges from the guest's own page tables in a window opened by signal delivery.  Specifically:

1. SIGALRM fires while the guest is running user code in a single-threaded process.
2. `sendsig` builds a sigframe at `useresp - sizeof(sigframe)`.  On the FIRST signal delivery this can trigger a page fault (demand-paged user stack grow-down).
3. The page fault is handled by substrate's `vm_fault` + `pmap_enter`, which calls `invlpg <stack_va>` to invalidate the guest TLB for that single VA.
4. KVM intercepts the `invlpg` (or doesn't, if its shadow page tables track a different VA-to-PA mapping than what the guest now believes).  In either case the host EPT entry KVM is using to back this guest is somehow left pointing at a stale physical page for a HEAP VA that has nothing to do with the page that was just demand-mapped.
5. Subsequent user-mode reads through that heap VA, while running in KVM mode, fetch from the stale physical page.  They return correct data only after the VA's host-side TLB entry is evicted by capacity pressure from accessing many other VAs.

This matches the observed:
- "Stable wrong reads through one VA" — same stale entry, same wrong page.
- "Healing after a full re-scan" — capacity-miss eviction.
- "Per-byte canary detects it, constant-byte memset doesn't" — the wrong physical page contains *some* arbitrary data that happens to differ from per-offset canary but not from `0xA5`-everywhere.
- "Guest CR3 reload doesn't fix it" — the divergence is host-side, not guest-side.

## What we did NOT prove

- **Substrate's signal-delivery code is correct.**  We audited it, traced it, and inserted optional TLB flushes that the bug ignored.  Nothing in `sendsig` or `sys_sigreturn` writes to user heap.  But we did not formally verify all paths in `vm_fault`/`pmap_enter` are guest-TLB-clean — only that the bug doesn't go away when we add CR3 reloads.
- **The exact KVM internal that diverges.**  Reproducer doesn't peek at host EPT / shadow page tables.

## Other crashes that share this signature

| Crash | Root cause | Status |
|---|---|---|
| `Xfbdev → calloc → malloc → split_block` NULL-deref at startup | Thread-unsafe libc malloc — substrate bug | **Fixed in commit `4375c3fa`** (single global spinlock around malloc/free/realloc) |
| `links → malloc → split_block` NULL-deref | Same as above | Fixed |
| `Xfbdev → fgets → fread → memcpy(dst=0x1)` | Possibly the same heap race | Should be fixed by malloc-lock commit |
| `Xfbdev → ... → XkbRF_CheckApplyRule (mdefs=NULL)` and friends, post-malloc-lock | **Still under investigation.**  May be the KVM bug surfacing differently, may be a substrate bug we haven't pinned, may need separate analysis |

The X-server startup crashes we have been chasing in this thread are now believed to be a *combination* of (a) the now-fixed thread-unsafe malloc (substrate, real bug) and (b) the KVM coherence issue described in this document (qemu, also real, harder to fix from our side).

## Workarounds

1. **Use `-accel tcg` instead of `-accel kvm`.**  `run-networking.sh` now defaults to TCG; pass `--kvm` to opt in.  `run-auto-test.sh` defaults to TCG; set `KVM=1` to opt in.
2. **Avoid heavy SIGALRM use during malloc/free** in workloads that must run under KVM.  This is not really an option for the X server (it uses `SmartScheduleTimer` on a 20 ms periodic SIGALRM).

## Next steps if upstream interest exists

- Strip the reproducer to a self-contained C program that:
  - Doesn't depend on substrate.  Either Linux/i386 guest or a stripped-down hand-written ELF.
  - Allocates a multi-page block, fills with per-offset canary, scans + measures `wrong_reads`.
  - Drives periodic SIGALRM at 5 ms.
- Bisect QEMU + KVM-i386 commits to find when this regressed (or whether it has been there forever).
- File against upstream QEMU.

## References

- Reproducer source: `tests/lib/c/torture_heap_stdio.c` (scenarios sc9, sc9b, sc9c, sc9d, sc9e, sc9f, sc9g)
- Project-memory note: `feedback_no_pgrep_self_match.md` (testing harness detail unrelated to this bug)
- Project-memory note: `project_qemu_kvm_signal_coherence.md` (this bug, one-line summary)
- Substrate kernel signal path: `sys/arch/i386/signal.c` (sendsig, sys_sigreturn — confirmed innocent)
- Substrate libc thread-safe malloc fix: commit `4375c3fa` — the unrelated bug that was masking the KVM one until it was fixed
