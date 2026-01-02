#ifndef _SYS_FREEBSD_BOOT_H
#define _SYS_FREEBSD_BOOT_H

#include <stdint.h>

/*
 * FreeBSD bootinfo structure (i386)
 */
struct bootinfo {
	uint32_t bi_version;
	uint32_t bi_kernelname;		/* GVA of kernel name */
	uint32_t bi_nfs_diskless;	/* GVA of nfs_diskless struct */
	uint32_t bi_directory_handle;	/* GVA of directory handle */
	uint32_t bi_m_addr;		/* GVA of m_addr */
	uint32_t bi_m_size;		/* GVA of m_size */
	uint32_t bi_m_flags;		/* GVA of m_flags */
	uint32_t bi_m_tag;		/* GVA of m_tag */
	uint32_t bi_m_data;		/* GVA of m_data */
	uint32_t bi_m_len;		/* GVA of m_len */
	uint32_t bi_m_next;		/* GVA of m_next */
	uint32_t bi_m_nextpkt;		/* GVA of m_nextpkt */
	uint32_t bi_m_type;		/* GVA of m_type */
	uint32_t bi_m_flags2;		/* GVA of m_flags2 */
	uint32_t bi_m_pkthdr_len;	/* GVA of m_pkthdr_len */
	uint32_t bi_m_pkthdr_rcvif;	/* GVA of m_pkthdr_rcvif */
	/* ... many more fields ... */
};

#define BOOTINFO_VERSION	1
#define BOOTINFO_MAGIC		0x12345678

#endif
