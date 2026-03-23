/*
 * sys/boot/stage2.h - Stage2 bootloader type definitions
 *
 * Structures for ext2 filesystem, ELF parsing, E820 memory map,
 * and bootloader constants.
 */

#ifndef _STAGE2_H_
#define _STAGE2_H_

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;

/* ext2 constants */
#define BLOCK_SIZE          1024
#define SECTORS_PER_BLOCK   (BLOCK_SIZE / 512)
#define INODE_SIZE          128
#define EXT2_ROOT_INO       2

struct ext2_superblock {
	uint32_t s_inodes_count;        /* 0x00 */
	uint32_t s_blocks_count;        /* 0x04 */
	uint32_t s_r_blocks_count;      /* 0x08 */
	uint32_t s_free_blocks_count;   /* 0x0C */
	uint32_t s_free_inodes_count;   /* 0x10 */
	uint32_t s_first_data_block;    /* 0x14 */
	uint32_t s_log_block_size;      /* 0x18 */
	uint32_t s_log_frag_size;       /* 0x1C */
	uint32_t s_blocks_per_group;    /* 0x20 */
	uint32_t s_frags_per_group;     /* 0x24 */
	uint32_t s_inodes_per_group;    /* 0x28 */
	uint32_t s_mtime;               /* 0x2C */
	uint32_t s_wtime;               /* 0x30 */
	uint16_t s_mnt_count;           /* 0x34 */
	uint16_t s_max_mnt_count;       /* 0x36 */
	uint16_t s_magic;               /* 0x38 */
	/* ... more fields, but we only need these */
} __attribute__((packed));

struct ext2_bgd {
	uint32_t bg_block_bitmap;
	uint32_t bg_inode_bitmap;
	uint32_t bg_inode_table;
	uint16_t bg_free_blocks_count;
	uint16_t bg_free_inodes_count;
	uint16_t bg_used_dirs_count;
	uint16_t bg_pad;
	uint8_t  bg_reserved[12];
} __attribute__((packed));

struct ext2_inode {
	uint16_t i_mode;
	uint16_t i_uid;
	uint32_t i_size;
	uint32_t i_atime;
	uint32_t i_ctime;
	uint32_t i_mtime;
	uint32_t i_dtime;
	uint16_t i_gid;
	uint16_t i_links_count;
	uint32_t i_blocks;
	uint32_t i_flags;
	uint32_t i_osd1;
	uint32_t i_block[15];
	uint32_t i_generation;
	uint32_t i_file_acl;
	uint32_t i_dir_acl;
	uint32_t i_faddr;
	uint8_t  i_osd2[12];
} __attribute__((packed));

struct ext2_dirent {
	uint32_t inode;
	uint16_t rec_len;
	uint8_t  name_len;
	uint8_t  file_type;
	char     name[];
} __attribute__((packed));

/* E820 memory map entry (as filled by BIOS INT 15h, E820h) */
struct e820_entry {
	uint64_t base;
	uint64_t len;
	uint32_t type;
	uint32_t acpi_ext;          /* ACPI 3.0 extended attributes, if present */
} __attribute__((packed));

/* ELF32 structures */
struct elf32_ehdr {
	uint8_t  e_ident[16];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint32_t e_entry;
	uint32_t e_phoff;
	uint32_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
} __attribute__((packed));

struct elf32_phdr {
	uint32_t p_type;
	uint32_t p_offset;
	uint32_t p_vaddr;
	uint32_t p_paddr;
	uint32_t p_filesz;
	uint32_t p_memsz;
	uint32_t p_flags;
	uint32_t p_align;
} __attribute__((packed));

#endif /* _STAGE2_H_ */
