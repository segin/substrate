/*
 * <a.out.h> — legacy a.out object-file header.
 *
 * Substrate doesn't support the SVR3/BSD a.out object format
 * (we run ELF natively), but ported tools — most notably
 * gnulib's getloadavg.c — still include this header for the
 * struct nlist definition under #ifndef NLIST_STRUCT.  A
 * minimal stub keeps those tools compiling; the resulting
 * nlist()-based code paths return -1 at runtime (no /vmlinux),
 * and callers fall back to /proc-based or fail-soft behaviour.
 */
#ifndef _A_OUT_H
#define _A_OUT_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nlist {
	union {
		char *n_name;
		long n_strx;
	} n_un;
	unsigned char n_type;
	char n_other;
	short n_desc;
	unsigned long n_value;
};

#ifdef __cplusplus
}
#endif

#endif /* _A_OUT_H */
