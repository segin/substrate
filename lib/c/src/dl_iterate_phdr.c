/*
 * dl_iterate_phdr(3) — bridge to the dynamic linker's loaded-object list.
 *
 * libgcc's DWARF unwinder (built with USE_PT_GNU_EH_FRAME) calls this to
 * find each loaded module's PT_GNU_EH_FRAME, which is what makes a C++
 * exception unwind across shared-library boundaries.  ld.so provides the
 * real iterator as __ldso_dl_iterate_phdr.  A statically linked program
 * with no ld.so has a single module and never needs PT_GNU_EH_FRAME-based
 * unwinding, so the weak fallback simply reports nothing.
 */
#include <link.h>

extern int __ldso_dl_iterate_phdr(
	int (*)(struct dl_phdr_info *, size_t, void *), void *)
	__attribute__((weak));

int dl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *),
		    void *data)
{
	if (__ldso_dl_iterate_phdr)
		return __ldso_dl_iterate_phdr(callback, data);
	return 0;
}
