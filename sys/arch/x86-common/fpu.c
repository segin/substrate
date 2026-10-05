#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/proc.h>
#include <sys/signal.h>
#include <sys/smp.h>
#include <kern/console.h>
#include <arch/x86-common/cpu.h>
#include <arch/x86-common/fpu.h>
#include <arch/x86-common/intr.h>
#include <arch/x86-common/io.h>
#include <stdio.h>
#include <string.h>
#include <machine/idt.h>
#include <machine/percpu.h>
#include <vm/vm_kmem.h>

/*
 * fpu.c - lazy x87/SSE/AVX context switching, shared by the i386 and x86_64
 * kernels.  The control registers go through unsigned long, which is the
 * register width either kernel can move to and from %cr0/%cr4.  FXSAVE and
 * XSAVE without REX.W write the 32-bit format, which is the one a 32-bit
 * process on the x86_64 kernel needs.
 *
 * The state belongs to a THREAD.  It used to be kept per process, with the
 * trap re-armed only when the process changed, so two threads of one
 * process ran on each other's live registers: preempt one in the middle of
 * a computation and it resumed with whatever the other had left in %xmm0.
 * On x86-64, where every double and most block copies live in the SSE
 * registers, that corrupted three rounds in four of a four-thread test.
 *
 * Three save formats, the best the CPU has:
 *
 *   XSAVE   x87, SSE and whatever else XCR0 enables -- AVX, and AVX-512
 *           where present.  Setting CR4.OSXSAVE is also what tells user
 *           space (CPUID.OSXSAVE) that the system preserves the YMM
 *           registers, so this is what makes AVX usable at all.
 *   FXSAVE  x87 and SSE, 512 bytes.
 *   FNSAVE  x87 only, 108 bytes.
 */
static int fpu_use_fxsave = 0;
static int fpu_use_xsave = 0;

/* XSAVE: the components enabled in XCR0, and the size of the area that
 * holds them (CPUID.0xD.0:EBX).  The legacy region, the first 512 bytes,
 * has the FXSAVE layout; a 64-byte header follows it. */
static uint64_t fpu_xcr0 = 0;
static uint32_t fpu_area_size = 512;

#define FPU_LEGACY_SIZE     512U
#define FPU_XSAVE_HDR_SIZE  64U
#define FPU_AREA_ALIGN      64U         /* XSAVE's; FXSAVE needs only 16 */
#define FPU_AREA_MAX        4096U       /* above this, AVX-512 is left off */

#define XCR0_X87            0x01ULL
#define XCR0_SSE            0x02ULL
#define XCR0_AVX            0x04ULL
#define XCR0_AVX512         0xE0ULL     /* opmask, ZMM_Hi256, Hi16_ZMM */

#define CR4_OSFXSR          0x00000200UL
#define CR4_OSXMMEXCPT      0x00000400UL
#define CR4_OSXSAVE         0x00040000UL

#define FPU_FCW_DEFAULT     0x037FU     /* what FNINIT sets */
#define FPU_MXCSR_DEFAULT   0x1F80U     /* all exceptions masked */
#define FPU_MXCSR_OFFSET    24U
/* MXCSR comes back from user memory in a signal frame; these are the bits
 * every SSE CPU accepts.  FXRSTOR and XRSTOR fault on a reserved one. */
#define FPU_MXCSR_VALID     0xFFBFU

/*
 * Lazy-FPU owner: the thread whose register state is currently live in *a
 * given CPU's* hardware FPU.  NULL means the registers belong to no live
 * thread (fresh boot, or the owner exited).  The invariant that makes lazy
 * save/restore correct is: whenever a thread other than the owner is
 * scheduled on a CPU, that CPU's CR0.TS is set (fpu_switch below) so it
 * traps (#NM) before it can touch the FPU; the handler then saves the
 * owner's still-live registers and loads the faulting thread's.  No thread
 * other than the owner can dirty the registers between ownership changes,
 * so the save in the handler always captures the owner's state.
 *
 * CR0.TS and the physical FPU registers are per-CPU hardware, so ownership
 * is tracked per CPU.  (Only one CPU runs threads today.  When more do, a
 * thread that migrates while it owns another CPU's registers has to have
 * them saved there first.)
 */
static thread_t *fpu_owner[MAX_CPUS] = { NULL };

/* Which CPU's FPU ownership slot to use.  Clamp defensively so an unexpected id
 * (or a pre-percpu-init call) can never index out of bounds. */
static inline int fpu_cpu(void) {
#ifdef HOST_TEST
    return 0;
#else
    int c = CPU_ID();
    return (c >= 0 && c < MAX_CPUS) ? c : 0;
#endif
}

static inline void fpu_set_ts(void) {
#ifndef HOST_TEST
    unsigned long cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x08;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
#endif
}

static inline void fpu_clear_ts(void) {
#ifndef HOST_TEST
    __asm__ volatile("clts");
#endif
}

/*
 * A thread's save area, allocated at its first use of the FPU: most kernel
 * threads never have one.  kmalloc promises no alignment, so the area is
 * carved out of a larger allocation.
 */
static void *fpu_thread_area(thread_t *t) {
    if (!t->fpu_area) {
        void *raw = kmalloc(fpu_area_size + FPU_AREA_ALIGN - 1);
        if (!raw) return NULL;
        memset(raw, 0, fpu_area_size + FPU_AREA_ALIGN - 1);
        t->fpu_raw = raw;
        t->fpu_area = (void *)(((uintptr_t)raw + FPU_AREA_ALIGN - 1) &
                               ~(uintptr_t)(FPU_AREA_ALIGN - 1));
    }
    return t->fpu_area;
}

/* Save the live registers into T's area, which must exist. */
static void fpu_save(thread_t *t) {
#ifndef HOST_TEST
    void *area = t->fpu_area;
    if (fpu_use_xsave) {
        __asm__ volatile("xsave (%0)"
                         : : "r"(area), "a"((uint32_t)fpu_xcr0),
                             "d"((uint32_t)(fpu_xcr0 >> 32))
                         : "memory");
    } else if (fpu_use_fxsave) {
        __asm__ volatile("fxsave (%0)" : : "r"(area) : "memory");
    } else {
        __asm__ volatile("fnsave (%0)" : : "r"(area) : "memory");
    }
#else
    (void)t;
#endif
}

/* Load the registers from T's area. */
static void fpu_restore(thread_t *t) {
#ifndef HOST_TEST
    void *area = t->fpu_area;
    if (fpu_use_xsave) {
        __asm__ volatile("xrstor (%0)"
                         : : "r"(area), "a"((uint32_t)fpu_xcr0),
                             "d"((uint32_t)(fpu_xcr0 >> 32)),
                             "m"(*(const char (*)[FPU_LEGACY_SIZE])area));
    } else if (fpu_use_fxsave) {
        __asm__ volatile("fxrstor (%0)"
                         : : "r"(area),
                             "m"(*(const char (*)[FPU_LEGACY_SIZE])area));
    } else {
        __asm__ volatile("frstor (%0)"
                         : : "r"(area), "m"(*(const char (*)[108])area));
    }
#else
    (void)t;
#endif
}

/*
 * Make AREA the state a thread starts with: the x87 as FNINIT leaves it,
 * MXCSR at its default, every register zero.  Loading it, rather than
 * running FNINIT on whatever the previous owner left, is what keeps one
 * thread's XMM and YMM contents from reaching another.  With XSAVE the
 * zeroed header (XSTATE_BV = 0) puts every component in its initial
 * configuration.  FNSAVE's format has no such image; that path uses FNINIT.
 */
static void fpu_init_area(void *area) {
    uint16_t fcw = FPU_FCW_DEFAULT;
    uint32_t mxcsr = FPU_MXCSR_DEFAULT;

    memset(area, 0, fpu_area_size);
    memcpy(area, &fcw, sizeof(fcw));
    memcpy((uint8_t *)area + FPU_MXCSR_OFFSET, &mxcsr, sizeof(mxcsr));
}

/* If T's registers are live on this CPU, write them back to its area, so
 * that the area is T's state.  Interrupts must be off. */
static void fpu_sync(thread_t *t) {
    if (fpu_owner[fpu_cpu()] == t) {
        fpu_clear_ts();
        fpu_save(t);
    }
}

/* T's registers are no longer the live ones: its next use reloads its
 * area.  Interrupts must be off. */
static void fpu_disown(thread_t *t) {
    int cpu = fpu_cpu();

    if (fpu_owner[cpu] == t) {
        fpu_owner[cpu] = NULL;
        fpu_set_ts();
    }
}

/*
 * A thread is being scheduled on this CPU.  It may use the FPU freely only
 * if the live registers are already its own; anyone else has to trap (#NM)
 * first, so that fpu_handler saves the owner's registers and loads theirs.
 * Called from arch_switch_to on every thread change.
 */
void fpu_switch(thread_t *next) {
    if (next && fpu_owner[fpu_cpu()] == next) {
        fpu_clear_ts();
    } else {
        fpu_set_ts();
    }
}

/*
 * A process is being torn down: none of its threads will run user code
 * again, so drop any ownership instead of saving registers nobody will
 * read.  The threads' areas are freed as each is reaped (fpu_thread_free).
 */
void fpu_forget_process(struct process *p) {
    for (int i = 0; i < MAX_CPUS; i++) {
        if (fpu_owner[i] && fpu_owner[i]->proc == p) {
            fpu_owner[i] = NULL;
            if (i == fpu_cpu()) fpu_set_ts();
        }
    }
}

/* A thread's storage is about to be freed. */
void fpu_thread_free(thread_t *t) {
    if (!t) return;
    unsigned long flags = intr_disable();
    for (int i = 0; i < MAX_CPUS; i++) {
        if (fpu_owner[i] == t) {
            fpu_owner[i] = NULL;
            if (i == fpu_cpu()) fpu_set_ts();
        }
    }
    intr_restore(flags);
    if (t->fpu_raw) {
        kfree(t->fpu_raw, fpu_area_size + FPU_AREA_ALIGN - 1);
    }
    t->fpu_raw = NULL;
    t->fpu_area = NULL;
    t->fpu_used = 0;
}

/*
 * A new thread starts with a copy of its creator's state: fork(2) gives
 * the child the parent's registers, and POSIX has a thread inherit the
 * floating-point environment of the one that created it.  Called in the
 * creator's context.  A creator that has never used the FPU, or a child
 * there is no memory for, leaves the child to start from the initial state.
 */
void fpu_thread_inherit(thread_t *parent, thread_t *child) {
    if (!parent || !child || !parent->fpu_used || !parent->fpu_area)
        return;
    if (!fpu_thread_area(child))
        return;
    unsigned long flags = intr_disable();
    fpu_sync(parent);
    memcpy(child->fpu_area, parent->fpu_area, fpu_area_size);
    intr_restore(flags);
    child->fpu_used = 1;
}

/*
 * Signal delivery and return.  The amd64 signal frame carries the state,
 * since 64-bit code keeps live values in the SSE and AVX registers across
 * any instruction a signal can interrupt, and a handler is free to use
 * them.  The frame has room for an FXSAVE image (512 bytes); what XSAVE
 * adds beyond that -- its header and the extended components, the upper
 * halves of the YMM registers among them -- goes into a separate block on
 * the user stack that the frame points to (mc_xfpustate, as on FreeBSD).
 *
 * fpu_signal_save() copies the legacy image to `image` and returns 1, or
 * returns 0 if the thread has never used the FPU.  fpu_signal_extra_len()
 * is the size of the extended block, 0 without XSAVE, and
 * fpu_signal_copyout_extra() writes it to user memory; it must follow a
 * fpu_signal_save() that returned 1, with no return to user mode between.
 * fpu_signal_restore() makes the frame's image, and the extended block if
 * it is given one of the right size, the thread's state.
 */
int fpu_signal_save(void *image) {
    thread_t *t = current_thread;

    if (!t || !fpu_use_fxsave || !t->fpu_used || !t->fpu_area)
        return 0;
    unsigned long flags = intr_disable();
    fpu_sync(t);
    memcpy(image, t->fpu_area, FPU_LEGACY_SIZE);
    intr_restore(flags);
    return 1;
}

size_t fpu_signal_extra_len(void) {
    return fpu_use_xsave ? (size_t)(fpu_area_size - FPU_LEGACY_SIZE) : 0;
}

int fpu_signal_copyout_extra(void *uaddr) {
    thread_t *t = current_thread;
    size_t len = fpu_signal_extra_len();

    if (!t || !t->fpu_area || len == 0)
        return -EINVAL;
    /* The area is current: fpu_signal_save() wrote the registers back, and
     * the thread has not been in user mode since to change them. */
    return copyout((const uint8_t *)t->fpu_area + FPU_LEGACY_SIZE, uaddr,
                   len) != 0 ? -EFAULT : 0;
}

int fpu_signal_restore(const void *image, const void *uextra,
                       size_t extra_len) {
    thread_t *t = current_thread;

    if (!t || !fpu_use_fxsave)
        return 0;
    if (!fpu_thread_area(t))
        return -ENOMEM;

    /* From here the area, not the registers, is the thread's state; it is
     * loaded at the thread's next use, which cannot come before this
     * returns to user mode. */
    unsigned long flags = intr_disable();
    fpu_disown(t);
    intr_restore(flags);

    uint8_t *area = t->fpu_area;
    memcpy(area, image, FPU_LEGACY_SIZE);
    uint32_t mxcsr;
    memcpy(&mxcsr, area + FPU_MXCSR_OFFSET, sizeof(mxcsr));
    mxcsr &= FPU_MXCSR_VALID;
    memcpy(area + FPU_MXCSR_OFFSET, &mxcsr, sizeof(mxcsr));

    if (fpu_use_xsave) {
        uint8_t *hdr = area + FPU_LEGACY_SIZE;
        uint64_t bv = 0;

        if (uextra && extra_len == fpu_signal_extra_len()) {
            if (copyin(uextra, hdr, extra_len) != 0) {
                /* Half-written: fall back to the initial extended state. */
                memset(hdr, 0, extra_len);
            }
            memcpy(&bv, hdr, sizeof(bv));
        } else {
            /* A frame without the block: its handler saw, and may have
             * changed, only the legacy state.  The extended components
             * go back to their initial configuration. */
            memset(hdr, 0, fpu_area_size - FPU_LEGACY_SIZE);
        }
        /* The header is user data.  XRSTOR faults on a component XCR0
         * does not enable, on the compacted format, and on any reserved
         * header byte; and the legacy image above must be loaded. */
        bv = (bv & fpu_xcr0) | XCR0_X87 | XCR0_SSE;
        memset(hdr, 0, FPU_XSAVE_HDR_SIZE);
        memcpy(hdr, &bv, sizeof(bv));
    }
    t->fpu_used = 1;
    return 0;
}

// FPU Device Not Available Exception (Interrupt 7)
void fpu_handler(registers_t *regs) {
    (void)regs;
    // Clear TS to allow FPU access for the faulting instruction.
    fpu_clear_ts();

    /* Kernel context with no current thread: just enable the FPU for this
     * transient use and leave ownership untouched (the live registers still
     * belong to this CPU's owner; a kernel path must not clobber user FP
     * state). */
    thread_t *t = current_thread;
    if (!t)
        return;

    int cpu = fpu_cpu();

    /* We are already this CPU's owner: our registers are live and intact (TS
     * kept every other thread out since we last ran), so restoring here would
     * overwrite them with a stale save.  Nothing to do but keep TS clear. */
    if (fpu_owner[cpu] == t)
        return;

    /* Ownership is changing on this CPU.  Save the outgoing owner's still-live
     * registers before we load ours, so a later switch back to it restores
     * correctly. */
    if (fpu_owner[cpu]) {
        fpu_save(fpu_owner[cpu]);
        fpu_owner[cpu] = NULL;
    }

    if (!fpu_thread_area(t)) {
        /* No memory for a save area: the thread cannot be switched away
         * from with its registers intact, so it cannot go on. */
        fpu_set_ts();
        if (t->proc) sigexit(t->proc, SIGKILL);
        return;
    }

    if (!t->fpu_used) {
        // First use of the FPU by this thread - start from a known state.
        if (fpu_use_fxsave) {
            fpu_init_area(t->fpu_area);
            fpu_restore(t);
        } else {
#ifndef HOST_TEST
            __asm__ volatile("fninit");
#endif
        }
        t->fpu_used = 1;
    } else {
        // Restore this thread's previously saved FPU state.
        fpu_restore(t);
    }

    fpu_owner[cpu] = t;

    // Re-executing the faulting instruction will now work.
}

static int fpu_present = 0;

#ifndef HOST_TEST
/*
 * Turn XSAVE on: CR4.OSXSAVE, then the components this kernel keeps in
 * XCR0, then the size of the area they need.  AVX-512 is enabled only as
 * the complete set of its three components, which is the only form the
 * CPU accepts, and only if the area stays within FPU_AREA_MAX.
 */
static void fpu_enable_xsave(void) {
    uint32_t eax, ebx, ecx, edx;
    unsigned long cr4;
    uint64_t supported, want;

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= CR4_OSXSAVE;
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    i386_cpuid(0xD, 0, &eax, &ebx, &ecx, &edx);
    supported = ((uint64_t)edx << 32) | eax;

    want = XCR0_X87 | XCR0_SSE;
    if (supported & XCR0_AVX) {
        want |= XCR0_AVX;
        if ((supported & XCR0_AVX512) == XCR0_AVX512)
            want |= XCR0_AVX512;
    }
    for (;;) {
        __asm__ volatile("xsetbv"
                         : : "a"((uint32_t)want), "d"((uint32_t)(want >> 32)),
                             "c"(0));
        i386_cpuid(0xD, 0, &eax, &ebx, &ecx, &edx);
        if (ebx <= FPU_AREA_MAX || !(want & XCR0_AVX512))
            break;
        want &= ~XCR0_AVX512;
    }
    if (ebx < FPU_LEGACY_SIZE + FPU_XSAVE_HDR_SIZE || ebx > FPU_AREA_MAX) {
        /* Not a size this code can believe; stay with FXSAVE. */
        cr4 &= ~CR4_OSXSAVE;
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
        return;
    }
    fpu_xcr0 = want;
    fpu_area_size = ebx;
    fpu_use_xsave = 1;
}
#endif

void fpu_init(void) {
#ifndef HOST_TEST
    // Detect FPU presence using CPUID or CR0 probing
    unsigned long cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));

    // Clear EM (emulation) bit to test for FPU
    cr0 &= ~0x04;  // Clear EM
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    // Try FNINIT and check status word
    uint16_t status = 0x5A5A;
    __asm__ volatile("fninit");
    __asm__ volatile("fnstsw %0" : "=m"(status));

    if ((status & 0xFF) == 0) {
        // FPU detected!
        fpu_present = 1;
        fpu_use_fxsave = i386_cpu_has_fxsr();
        kprint("FPU: Hardware x87 detected\n");

        // Configure CR0 for native FPU with lazy switching
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 0x02;   // Set MP (Monitor Coprocessor)
        cr0 |= 0x20;   // Set NE (Numeric Error - use internal FPU error handling)
        cr0 &= ~0x04;  // Clear EM (no emulation needed)
        cr0 |= 0x08;   // Set TS (Task Switched) for lazy context switching
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

        // Initialize FPU to known state
        __asm__ volatile("fninit");
        if (fpu_use_fxsave) {
            /* SSE / SSE2 are gated by CR4.OSFXSR (bit 9): without it
             * the CPU raises #UD on any SSE opcode in user mode, even
             * if CPUID reports SSE/SSE2 support.  Also set OSXMMEXCPT
             * (bit 10) so SIMD FP exceptions raise #XF rather than the
             * legacy #UD fallback.  We gate this on FXSR availability
             * because OSFXSR without FXSAVE/FXRSTOR is meaningless. */
            if (i386_cpu_has_cr4()) {
                unsigned long cr4;
                __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
                cr4 |= CR4_OSFXSR;
                cr4 |= CR4_OSXMMEXCPT;
                __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
                kprint("FPU: SSE enabled (CR4.OSFXSR + OSXMMEXCPT)\n");
                if (i386_cpu_has_xsave())
                    fpu_enable_xsave();
            }
            if (fpu_use_xsave) {
                char buf[96];
                snprintf(buf, sizeof(buf),
                         "FPU: Using XSAVE/XRSTOR context format "
                         "(XCR0=0x%x, %u bytes%s)\n",
                         (unsigned)fpu_xcr0, (unsigned)fpu_area_size,
                         (fpu_xcr0 & XCR0_AVX512) ? ", AVX-512"
                         : (fpu_xcr0 & XCR0_AVX) ? ", AVX" : "");
                kprint(buf);
            } else {
                kprint("FPU: Using FXSAVE/FXRSTOR context format\n");
            }
        } else {
            fpu_area_size = 108;
            kprint("FPU: Using FNSAVE/FRSTOR context format\n");
        }
    } else {
        // No FPU - enable emulation
        fpu_present = 0;
        fpu_use_fxsave = 0;
        kprint("FPU: No hardware x87 detected (emulation mode)\n");
        __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
        cr0 |= 0x04;   // Set EM (emulation)
        cr0 &= ~0x02;  // Clear MP
        __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
    }
#endif

    // Register INT 7 handler for #NM (Device Not Available)
    idt_set_gate(7, (uintptr_t)isr7, 0x08, 0x8E);
}
