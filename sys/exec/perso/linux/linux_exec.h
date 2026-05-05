#ifndef _LINUX_EXEC_H
#define _LINUX_EXEC_H

/*
 * Linux ELF execution support for the Substrate kernel.
 *
 * Linux ELF binaries are identified by EI_OSABI == ELFOSABI_LINUX (3)
 * or ELFOSABI_NONE (0) with a .note.ABI-tag section containing the
 * GNU ABI tag with OS = GNU/Linux (0).
 *
 * Reference: NetBSD sys/compat/linux/common/linux_exec.h
 */

#include <stdint.h>

/*
 * Linux ELF auxiliary vector types (AT_*)
 *
 * Passed on the stack to the process by the kernel during exec.
 * The dynamic linker (ld-linux.so) reads these to set up the process.
 */
#define LINUX_AT_NULL           0
#define LINUX_AT_IGNORE         1
#define LINUX_AT_EXECFD         2
#define LINUX_AT_PHDR           3
#define LINUX_AT_PHENT          4
#define LINUX_AT_PHNUM          5
#define LINUX_AT_PAGESZ         6
#define LINUX_AT_BASE           7
#define LINUX_AT_FLAGS          8
#define LINUX_AT_ENTRY          9
#define LINUX_AT_NOTELF        10
#define LINUX_AT_UID           11
#define LINUX_AT_EUID          12
#define LINUX_AT_GID           13
#define LINUX_AT_EGID          14
#define LINUX_AT_PLATFORM      15
#define LINUX_AT_HWCAP         16
#define LINUX_AT_CLKTCK        17
/* 18-22 reserved */
#define LINUX_AT_SECURE        23
#define LINUX_AT_BASE_PLATFORM 24
#define LINUX_AT_RANDOM        25
#define LINUX_AT_HWCAP2        26
#define LINUX_AT_EXECFN        31
#define LINUX_AT_SYSINFO       32
#define LINUX_AT_SYSINFO_EHDR  33

#define LINUX_RANDOM_BYTES     16  /* 16 bytes for AT_RANDOM */

/*
 * Linux ELF note descriptor for GNU ABI tag
 *
 * Found in .note.ABI-tag section (or PT_NOTE segment).
 * n_name = "GNU\0", n_type = 1 (NT_GNU_ABI_TAG)
 */
#define LINUX_NT_GNU_ABI_TAG   1
#define LINUX_GNU_ABI_LINUX    0

struct linux_abi_tag {
	uint32_t os;           /* LINUX_GNU_ABI_LINUX = 0 */
	uint32_t major;        /* Minimum kernel major version */
	uint32_t minor;        /* Minimum kernel minor version */
	uint32_t patch;        /* Minimum kernel patch level */
};

/*
 * ELF identification for Linux binaries
 */
#define LINUX_ELFOSABI         3   /* ELFOSABI_LINUX / ELFOSABI_GNU */

/*
 * Linux a.out magic numbers (for historical reference)
 */
#define LINUX_AOUT_OMAGIC      0407
#define LINUX_AOUT_NMAGIC      0410
#define LINUX_AOUT_ZMAGIC      0413
#define LINUX_AOUT_QMAGIC      0314

/* Extract magic and machine type from a.out header */
#define LINUX_N_MAGIC(ep)      ((ep)->a_info & 0xFFFF)
#define LINUX_N_MACHTYPE(ep)   (((ep)->a_info >> 16) & 0xFF)

/* i386 machine ID for Linux a.out */
#define LINUX_MID_I386         100

/*
 * Linux a.out text/data offset calculations
 */
#define LINUX_N_TXTOFF(ep, m) \
	((m) == LINUX_AOUT_ZMAGIC ? 1024 : \
	 ((m) == LINUX_AOUT_QMAGIC ? 0 : sizeof(struct exec)))

#define LINUX__N_TXTENDADDR(ep, m) \
	((ep).a_text + LINUX_N_TXTOFF(ep, m))

#define LINUX__N_SEGMENT_ROUND(x) \
	(((x) + 4095) & ~4095)

#define LINUX_N_DATADDR(ep, m) \
	((m) == LINUX_AOUT_OMAGIC ? LINUX__N_TXTENDADDR(ep, m) : \
	 LINUX__N_SEGMENT_ROUND(LINUX__N_TXTENDADDR(ep, m)))

/*
 * Probe function: check if an ELF binary is a Linux binary.
 * Returns 0 if the binary should be handled by the Linux personality.
 */
int linux_elf32_probe(const char *path, const void *ehdr);

#endif /* _LINUX_EXEC_H */
