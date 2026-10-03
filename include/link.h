#ifndef _LINK_H
#define _LINK_H

/*
 * <link.h> — dynamic-linker introspection.
 *
 * Substrate provides the dl_iterate_phdr(3) subset that libgcc's DWARF
 * unwinder needs (USE_PT_GNU_EH_FRAME) to locate each loaded object's
 * PT_GNU_EH_FRAME / .eh_frame_hdr.  This is what lets a C++ exception
 * unwind across shared-library boundaries.  The struct layout matches
 * the GNU/glibc `struct dl_phdr_info` ABI.
 */

#include <elf.h>
#include <stddef.h>

/* ElfW(type): the ELF type of the native class - Elf32_ on i386, Elf64_
 * on amd64, whose objects /sbin/ld64.so loads. */
#ifndef ElfW
#if defined(__x86_64__)
#define ElfW(type) Elf64_##type
#else
#define ElfW(type) Elf32_##type
#endif
#endif

struct dl_phdr_info {
	ElfW(Addr)		dlpi_addr;	/* module load bias */
	const char	       *dlpi_name;	/* module name / path */
	const ElfW(Phdr)       *dlpi_phdr;	/* program header table */
	ElfW(Half)		dlpi_phnum;	/* number of program headers */
	/*
	 * glibc ABI extension.  Substrate does not maintain the load-event
	 * counters, and dl_iterate_phdr() reports the size of just the four
	 * members above, so conforming callers (e.g. libgcc's unwinder)
	 * never read these.
	 */
	unsigned long long	dlpi_adds;
	unsigned long long	dlpi_subs;
	size_t			dlpi_tls_modid;
	void		       *dlpi_tls_data;
};

#ifdef __cplusplus
extern "C" {
#endif

int dl_iterate_phdr(int (*__callback)(struct dl_phdr_info *__info,
				      size_t __size, void *__data),
		    void *__data);

#ifdef __cplusplus
}
#endif

#endif /* _LINK_H */
