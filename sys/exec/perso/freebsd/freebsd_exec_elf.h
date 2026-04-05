#ifndef _FREEBSD_EXEC_ELF_H
#define _FREEBSD_EXEC_ELF_H

/* FreeBSD ELF identification */
#define ELFOSABI_FREEBSD       9
#define FREEBSD_ELF_BRAND      "FreeBSD"

/* Traditional FreeBSD sigcode locations */
#define FREEBSD_USRSTACK       0xbfc00000
#define FREEBSD_PS_STRINGS     (FREEBSD_USRSTACK - sizeof(struct freebsd_ps_strings))

#endif /* _FREEBSD_EXEC_ELF_H */
