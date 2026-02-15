# a.out Loader & Personality Manager Specification

This document provides a precise, OS-centric breakdown for implementing an `exec_aout` loader capable of distinguishing and handling various `a.out` formats (Linux, FreeBSD, NetBSD, OpenBSD, SunOS).

## 1. Loader Behavior -> Kernel Expectation Mapping

| Behavior                | Linux a.out    | FreeBSD a.out (old)        | NetBSD a.out | OpenBSD a.out |
| ----------------------- | -------------- | -------------------------- | ------------ | ------------- |
| Separate I/D            | Optional       | **Strict (NMAGIC/ZMAGIC)** | Strict       | Strict        |
| Writable text (OMAGIC)  | Allowed        | Allowed                    | Allowed      | Allowed       |
| Demand-paged text       | ZMAGIC, QMAGIC | **ZMAGIC only**            | ZMAGIC       | ZMAGIC        |
| QMAGIC support          | **Yes**        | No                         | No           | No            |
| Header mapped into VM   | No (QMAGIC)    | Yes (ZMAGIC page)          | Yes          | Yes           |
| Text VA alignment       | Loose          | **Page-aligned**           | Page-aligned | Page-aligned  |
| Data follows text in VA | Not guaranteed | **Always**                 | Always       | Always        |
| First page unmapped     | Linux QMAGIC   | No                         | No           | No            |

**Loader Implications:**
- **QMAGIC Semantics:** If text is at file offset 0 and header is not mapped -> **NOT** BSD.
- **MID Invalid:** If MID is invalid but binary is otherwise BSD-clean -> **FreeBSD**.
- **Relocation Flags:** If flags beyond Linux's minimal set (e.g., `r_baserel`, `r_jmptable`, `r_copy`) -> **BSD-family**.
- **ABI Compliance:** Syscall table must match the chosen personality.

## 2. Classification Heuristic (Sample C)

This routine provides a deterministic way to classify `a.out` binaries based on structural invariants.

```c
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <arpa/inet.h>

#define OMAGIC 0407
#define NMAGIC 0410
#define ZMAGIC 0413
#define QMAGIC 0314

enum aout_flavor {
    AOUT_LINUX,
    AOUT_FREEBSD,
    AOUT_NETBSD,
    AOUT_OPENBSD,
    AOUT_UNKNOWN
};

struct exec {
    uint32_t a_midmag;
    uint32_t a_text;
    uint32_t a_data;
    uint32_t a_bss;
    uint32_t a_syms;
    uint32_t a_entry;
    uint32_t a_trsize; // Text relocation size
    uint32_t a_drsize; // Data relocation size
};

static int get_magic(uint32_t midmag) {
    return midmag & 0xFFFF;
}

static int get_mid(uint32_t midmag) {
    return (midmag >> 16) & 0x3FF;
}

enum aout_flavor classify_aout(int fd) {
    struct exec e;
    lseek(fd, 0, SEEK_SET);
    if (read(fd, &e, sizeof(e)) != sizeof(e))
        return AOUT_UNKNOWN;

    /* Try host-endian first */
    uint32_t midmag = e.a_midmag;
    int magic = get_magic(midmag);

    int host_ok = (magic == OMAGIC || magic == NMAGIC ||
                   magic == ZMAGIC || magic == QMAGIC);

    /* Try network-endian */
    uint32_t nmidmag = ntohl(e.a_midmag);
    int nmagic = get_magic(nmidmag);

    int net_ok = (nmagic == OMAGIC || nmagic == NMAGIC ||
                  nmagic == ZMAGIC || nmagic == QMAGIC);

    /* Linux-only shortcut: QMAGIC is unique to Linux */
    if (magic == QMAGIC || nmagic == QMAGIC)
        return AOUT_LINUX;

    /* Prefer BSD interpretation if net-endian valid */
    if (net_ok) {
        int mid = get_mid(nmidmag);
        if (mid != 0) {
            /* NetBSD/OpenBSD enforce MID. */
            return AOUT_NETBSD; /* Discriminate via syscalls/etc later */
        }
    }

    /* BSD relocation presence test */
    if (e.a_trsize || e.a_drsize) {
        /* BSD-style relocations indicate BSD lineage */
        if (!net_ok && host_ok)
            return AOUT_FREEBSD; /* Old FreeBSD tolerated host-endian */
        if (net_ok)
            return AOUT_NETBSD;
    }

    /* Fallback to Linux */
    if (host_ok)
        return AOUT_LINUX;

    return AOUT_UNKNOWN;
}
```

## 3. Personality Manager Architecture

### High-Level Design

```
           execve()
               |
        +------v-------+
        | Format Probe |
        +------+-------+
               |
        +------v-------+
        | a.out Loader | maps sections, applies relocs
        +------+-------+
               |
      +--------+---------+
      | Personality Select|
      +---+---------+----+
          |         |
   +------v--+  +---v-----+
   | FreeBSD |  | NetBSD  |
   | Persona |  | Persona |
   +----+----+  +----+----+
        |            |
   syscall tbl   syscall tbl
```

### Responsibilities

**Loader:**
- Parse header & map segments (Text RX, Data RW, BSS Zero).
- Apply relocations.
- Set initial registers & entry point.
- **Invariant:** Never mix loader semantics with personality semantics.

**Personality:**
- **Syscall Table:** Unique numbering per OS.
- **Signal ABI:** `struct` layouts differ.
- **Errno Mapping:** Subtle differences.
- **Sigreturn:** Critical ABI component (trampoline).
- **Auxv:** Must be **absent** (unlike ELF).
- **ps_strings:** Expected by BSD.

### FreeBSD Specifics (Old a.out)
- Ignores MID.
- Accepts host-endian headers.
- Uses BSD `sigcontext`.
- Kernel-installed signal trampoline.
- `brk()` grows from end-of-data.

### Safety Rules
1. **Never auto-fallback Linux -> BSD.**
2. **BSD personalities must never execute QMAGIC.**
3. **Relocations define personality, not magic.**
4. **Syscall mismatch = SIGSYS** (no silent remapping).

### Minimal Decision Matrix

| Detection Result        | Personality     |
| ----------------------- | --------------- |
| QMAGIC                  | Linux           |
| BSD reloc + MID ignored | FreeBSD         |
| BSD reloc + strict MID  | NetBSD/OpenBSD  |
| Unknown                 | Reject          |

## 4. Key Takeaways
- **FreeBSD a.out** is defined by **tolerance** (ignored MID, host-endian).
- **Relocations and VM assumptions** are the true ABI discriminators.
- **Personality selection** must be conservative.
- **Loader correctness** is paramount; do not guess for compat.
