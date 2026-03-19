/*
 * sys/boot/stage2.c - Substrate second-stage bootloader
 *
 * Loaded by stage1 at physical 0x5000.  Runs in 32-bit protected mode
 * with a flat memory model.  Reads the ext2 filesystem to find /vmunix,
 * displays a boot prompt with timeout, constructs a Multiboot info
 * structure, loads the ELF kernel at its physical addresses, and jumps
 * to the entry point with EAX=0x2BADB002 and EBX=multiboot_info pointer.
 */

#include "stage2.h"

/* ---- Global state ---- */
static uint8_t g_drive;
static uint32_t g_e820_count;
static struct e820_entry *g_e820_map;
static char cmdline_buf[256];

/* Simple heap (bump allocator starting at 0x100000 - 256KB .. we use 64KB) */
static uint32_t heap_ptr = 0x00060000;  /* above stage2, below stack */

static void *salloc(uint32_t size)
{
	void *p = (void *)heap_ptr;
	heap_ptr = (heap_ptr + size + 3) & ~3;  /* 4-byte align */
	return p;
}

/* ==== Low-level I/O ==== */

static inline void outb(uint16_t port, uint8_t val)
{
	__asm__ volatile ("outb %0, %1" : : "a"(val), "dN"(port));
}

static inline uint8_t inb(uint16_t port)
{
	uint8_t val;
	__asm__ volatile ("inb %1, %0" : "=a"(val) : "dN"(port));
	return val;
}

static inline void insl(uint16_t port, void *buf, uint32_t cnt)
{
	__asm__ volatile ("cld; rep insl"
	    : "=D"(buf), "=c"(cnt)
	    : "d"(port), "0"(buf), "1"(cnt)
	    : "memory", "cc");
}

/* ==== VGA text output ==== */

#define VGA_BASE 0xB8000
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTR 0x07           /* light grey on black */
#define VGA_ATTR_HL 0x0F        /* white on black */

static uint16_t *const vga = (uint16_t *)VGA_BASE;
static int vga_x, vga_y;

static void vga_scroll(void)
{
	int i;
	for (i = 0; i < VGA_COLS * (VGA_ROWS - 1); i++)
		vga[i] = vga[i + VGA_COLS];
	for (i = VGA_COLS * (VGA_ROWS - 1); i < VGA_COLS * VGA_ROWS; i++)
		vga[i] = (VGA_ATTR << 8) | ' ';
	vga_y = VGA_ROWS - 1;
}

static void vga_putc(char c)
{
	if (c == '\n') {
		vga_x = 0;
		vga_y++;
	} else if (c == '\r') {
		vga_x = 0;
	} else if (c == '\b') {
		if (vga_x > 0) {
			vga_x--;
			vga[vga_y * VGA_COLS + vga_x] = (VGA_ATTR << 8) | ' ';
		}
	} else {
		vga[vga_y * VGA_COLS + vga_x] = (VGA_ATTR << 8) | c;
		vga_x++;
		if (vga_x >= VGA_COLS) {
			vga_x = 0;
			vga_y++;
		}
	}
	if (vga_y >= VGA_ROWS)
		vga_scroll();
}

static void vga_puts(const char *s)
{
	while (*s)
		vga_putc(*s++);
}


static void vga_clear(void)
{
	int i;
	for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
		vga[i] = (VGA_ATTR << 8) | ' ';
	vga_x = vga_y = 0;
}

static void vga_puthex(uint32_t val)
{
	static const char hex[] = "0123456789abcdef";
	char buf[11];
	buf[0] = '0';
	buf[1] = 'x';
	int i;
	for (i = 0; i < 8; i++)
		buf[2 + i] = hex[(val >> (28 - i * 4)) & 0xF];
	buf[10] = 0;
	vga_puts(buf);
}

static void vga_putdec(uint32_t val)
{
	char buf[12];
	int i = 0;
	if (val == 0) {
		vga_putc('0');
		return;
	}
	while (val) {
		buf[i++] = '0' + (val % 10);
		val /= 10;
	}
	while (i > 0)
		vga_putc(buf[--i]);
}

/* ==== Serial output (COM1) ==== */
#define COM1 0x3F8

static void serial_init(void)
{
	outb(COM1 + 1, 0x00);   /* disable interrupts */
	outb(COM1 + 3, 0x80);   /* set DLAB */
	outb(COM1 + 0, 0x01);   /* baud 115200 (divisor 1) */
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x03);   /* 8N1 */
	outb(COM1 + 2, 0xC7);   /* FIFO */
	outb(COM1 + 4, 0x0B);   /* RTS/DSR set */
}

static void serial_putc(char c)
{
	while (!(inb(COM1 + 5) & 0x20))
		;
	outb(COM1, c);
}

static void serial_puts(const char *s)
{
	while (*s) {
		if (*s == '\n')
			serial_putc('\r');
		serial_putc(*s++);
	}
}

static void serial_puthex(uint32_t val)
{
	static const char hex[] = "0123456789abcdef";
	char buf[11];
	buf[0] = '0';
	buf[1] = 'x';
	int i;
	for (i = 0; i < 8; i++)
		buf[2 + i] = hex[(val >> (28 - i * 4)) & 0xF];
	buf[10] = 0;
	serial_puts(buf);
}

static void serial_putdec(uint32_t val)
{
	char buf[12];
	int i = 0;
	if (val == 0) {
		serial_putc('0');
		return;
	}
	while (val) {
		buf[i++] = '0' + (val % 10);
		val /= 10;
	}
	while (i > 0)
		serial_putc(buf[--i]);
}

static void puthex(uint32_t val)
{
	vga_puthex(val);
	serial_puthex(val);
}

static void putdec(uint32_t val)
{
	vga_putdec(val);
	serial_putdec(val);
}

static void puts(const char *s)
{
	vga_puts(s);
	serial_puts(s);
}

static void putc_both(char c)
{
	vga_putc(c);
	if (c == '\n')
		serial_putc('\r');
	serial_putc(c);
}

/* ==== Keyboard input (PS/2 polling) ==== */

static int kbd_ready(void)
{
	return inb(0x64) & 1;
}

static uint8_t kbd_read_raw(void)
{
	return inb(0x60);
}

/* Simple scancode set 1 -> ASCII (US layout, lowercase only) */
static const char scancode_ascii[128] = {
	0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
	'\t','q','w','e','r','t','y','u','i','o','p','[',']', '\n',
	0, 'a','s','d','f','g','h','j','k','l',';','\'', '`',
	0, '\\','z','x','c','v','b','n','m', ',','.','/', 0,
	'*', 0, ' '
};

static char kbd_getc(void)
{
	uint8_t sc;
	do {
		while (!kbd_ready())
			;
		sc = kbd_read_raw();
	} while (sc & 0x80);  /* skip key-up events */
	if (sc < sizeof(scancode_ascii))
		return scancode_ascii[sc];
	return 0;
}

/* Non-blocking: returns char or 0 if no key */
static char kbd_trygetc(void)
{
	if (!kbd_ready())
		return 0;
	uint8_t sc = kbd_read_raw();
	if (sc & 0x80)
		return 0;  /* key-up */
	if (sc < sizeof(scancode_ascii))
		return scancode_ascii[sc];
	return 0;
}

/* ==== Timer ==== */

/* Use PIT channel 0 at ~18.2 Hz (default BIOS rate) via port 0x40
 * In protected mode, we can't use INT 8h, so we poll the PIT directly. */
static uint32_t pit_read_count(void)
{
	outb(0x43, 0x00);  /* latch channel 0 */
	uint8_t lo = inb(0x40);
	uint8_t hi = inb(0x40);
	return (hi << 8) | lo;
}

/* Busy-wait for approximately `ticks` PIT rollovers (~55ms each).
 * Returns early (1) if a key is pressed. Returns 0 on timeout. */
static int wait_for_key(int seconds)
{
	/* PIT default reload = 65536, frequency ~18.2 Hz. 
	 * We count rollovers by detecting the counter wrapping. */
	int ticks = seconds * 18;
	uint16_t prev = pit_read_count();
	int counted = 0;

	while (counted < ticks) {
		if (kbd_ready())
			return 1;

		uint16_t cur = pit_read_count();
		if (cur > prev)  /* counter wrapped (counts down) */
			counted++;
		prev = cur;
	}
	return 0;
}

/* ==== IDE PIO (primary master, LBA28) ==== */

#define IDE_IO   0x1F0
#define IDE_DATA 0
#define IDE_ERR  1
#define IDE_SECN 2
#define IDE_LOW  3
#define IDE_MID  4
#define IDE_HIGH 5
#define IDE_HEAD 6
#define IDE_CMD  7
#define IDE_ALT  0x3F6

#define IDE_BSY  0x80
#define IDE_RDY  0x40
#define IDE_DRQ  0x08

static void ide_wait(void)
{
	while (inb(IDE_IO + IDE_CMD) & IDE_BSY)
		;
	while (!(inb(IDE_IO + IDE_CMD) & IDE_RDY))
		;
}

static void ide_read_sectors(void *buf, uint32_t lba, uint8_t count)
{
	ide_wait();
	outb(IDE_IO + IDE_SECN, count);
	outb(IDE_IO + IDE_LOW,  lba & 0xFF);
	outb(IDE_IO + IDE_MID,  (lba >> 8) & 0xFF);
	outb(IDE_IO + IDE_HIGH, (lba >> 16) & 0xFF);
	outb(IDE_IO + IDE_HEAD, 0xE0 | ((lba >> 24) & 0x0F));  /* master, LBA */
	outb(IDE_IO + IDE_CMD,  0x20);          /* READ SECTORS */

	uint8_t *p = (uint8_t *)buf;
	int i;
	for (i = 0; i < count; i++) {
		ide_wait();
		insl(IDE_IO + IDE_DATA, p, 128);  /* 128 dwords = 512 bytes */
		p += 512;
	}
}

/* Read one ext2 block (1024 bytes = 2 sectors) */
static void read_block(void *buf, uint32_t block)
{
	ide_read_sectors(buf, block * SECTORS_PER_BLOCK, SECTORS_PER_BLOCK);
}

/* ==== ext2 filesystem ==== */

static struct ext2_superblock *sb;
static struct ext2_bgd *bgd;

static int ext2_init(void)
{
	/* Read superblock (block 1) */
	sb = salloc(BLOCK_SIZE);
	read_block(sb, 1);
	if (sb->s_magic != 0xEF53)
		return -1;

	/* Read block group descriptor table (block 2) */
	bgd = salloc(BLOCK_SIZE);
	read_block(bgd, 2);

	return 0;
}

static struct ext2_inode *ext2_read_inode(uint32_t ino)
{
	uint32_t bg = (ino - 1) / sb->s_inodes_per_group;
	uint32_t idx = (ino - 1) % sb->s_inodes_per_group;
	uint32_t blk = bgd[bg].bg_inode_table + (idx * INODE_SIZE) / BLOCK_SIZE;
	uint32_t off = (idx * INODE_SIZE) % BLOCK_SIZE;

	uint8_t *buf = salloc(BLOCK_SIZE);
	read_block(buf, blk);
	struct ext2_inode *inode = salloc(sizeof(struct ext2_inode));
	__builtin_memcpy(inode, buf + off, sizeof(struct ext2_inode));
	return inode;
}

static uint32_t ext2_read_indirect(uint32_t block_ptr, uint32_t index)
{
	uint32_t buf[BLOCK_SIZE / 4];
	read_block(buf, block_ptr);
	return buf[index];
}

static void ext2_read_file(struct ext2_inode *inode, void *dest)
{
	uint32_t num_blocks = (inode->i_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
	uint32_t i;
	uint8_t *p = dest;

	for (i = 0; i < num_blocks; i++) {
		uint32_t blk;
		if (i < 12) {
			blk = inode->i_block[i];
		} else if (i < 12 + BLOCK_SIZE / 4) {
			blk = ext2_read_indirect(inode->i_block[12], i - 12);
		} else {
			/* Doubly-indirect: not expected for vmunix location, but handle */
			uint32_t idx = i - 12 - BLOCK_SIZE / 4;
			uint32_t ind = ext2_read_indirect(inode->i_block[13],
			    idx / (BLOCK_SIZE / 4));
			blk = ext2_read_indirect(ind, idx % (BLOCK_SIZE / 4));
		}
		if (blk == 0) {
			/* Sparse block (hole) – zero-fill */
			uint32_t j;
			for (j = 0; j < BLOCK_SIZE; j++)
				p[j] = 0;
		} else {
			read_block(p, blk);
		}
		p += BLOCK_SIZE;
	}
}

/* Find a directory entry by name in an inode */
static uint32_t ext2_find_entry(uint32_t dir_ino, const char *name)
{
	struct ext2_inode *dir = ext2_read_inode(dir_ino);
	uint32_t size = dir->i_size;
	uint8_t *buf = salloc(size);
	ext2_read_file(dir, buf);

	uint32_t off = 0;
	int name_len = 0;
	const char *p;
	for (p = name; *p; p++)
		name_len++;

	while (off < size) {
		struct ext2_dirent *de = (struct ext2_dirent *)(buf + off);
		if (de->inode && de->name_len == name_len) {
			int match = 1;
			int j;
			for (j = 0; j < name_len; j++) {
				if (de->name[j] != name[j]) {
					match = 0;
					break;
				}
			}
			if (match)
				return de->inode;
		}
		if (de->rec_len == 0)
			break;
		off += de->rec_len;
	}
	return 0;
}

/* Resolve a path like "vmunix" (relative to root) */
static uint32_t ext2_lookup(const char *path)
{
	/* Always start from root inode (2) */
	return ext2_find_entry(2, path);
}

/* ==== ELF loader ==== */

static uint32_t load_elf(void *data)
{
	struct elf32_ehdr *ehdr = data;

	/* Validate */
	if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' ||
	    ehdr->e_ident[2] != 'L'  || ehdr->e_ident[3] != 'F') {
		puts("Not ELF\n");
		return 0;
	}
	if (ehdr->e_type != 2 || ehdr->e_machine != 3) {
		puts("Not i386 executable\n");
		return 0;
	}

	struct elf32_phdr *phdr = (void *)((uint32_t)data + ehdr->e_phoff);
	uint32_t entry_vaddr = ehdr->e_entry;
	uint32_t virt_to_phys = 0;
	int found_load = 0;
	int i;

	for (i = 0; i < ehdr->e_phnum; i++, phdr++) {
		if (phdr->p_type != 1)  /* PT_LOAD */
			continue;

		/* Compute virt-to-phys offset from first loadable segment */
		if (!found_load) {
			virt_to_phys = phdr->p_vaddr - phdr->p_paddr;
			found_load = 1;
		}

		uint8_t *src = (uint8_t *)data + phdr->p_offset;
		uint8_t *dst = (uint8_t *)phdr->p_paddr;

		/* Copy file data */
		uint32_t j;
		for (j = 0; j < phdr->p_filesz; j++)
			dst[j] = src[j];
		/* Zero BSS */
		for (; j < phdr->p_memsz; j++)
			dst[j] = 0;
	}

	/* Convert virtual entry to physical */
	return entry_vaddr - virt_to_phys;
}

/* ==== Multiboot info construction ==== */

struct multiboot_info {
	uint32_t flags;
	uint32_t mem_lower;
	uint32_t mem_upper;
	uint32_t boot_device;
	uint32_t cmdline;
	uint32_t mods_count;
	uint32_t mods_addr;
	uint32_t syms[4];
	uint32_t mmap_length;
	uint32_t mmap_addr;
	uint32_t drives_length;
	uint32_t drives_addr;
	uint32_t config_table;
	uint32_t boot_loader_name;
	uint32_t apm_table;
} __attribute__((packed));

struct multiboot_mmap_entry {
	uint32_t size;
	uint64_t addr;
	uint64_t len;
	uint32_t type;
} __attribute__((packed));

/* Place multiboot structures at a known safe physical address
 * that won't be overwritten by the kernel load (kernel loads at 1MB+) */
#define MBOOT_INFO_ADDR   0x00010000   /* 64KB */
#define MBOOT_MMAP_ADDR   0x00011000   /* 68KB */
#define MBOOT_CMDLINE_ADDR 0x00012000  /* 72KB */
#define MBOOT_NAME_ADDR   0x00012100   /* right after cmdline */

#define MULTIBOOT_MAGIC   0x2BADB002

#define MBOOT_INFO_MEMORY   0x001
#define MBOOT_INFO_BOOTDEV  0x002
#define MBOOT_INFO_CMDLINE  0x004
#define MBOOT_INFO_MEM_MAP  0x040
#define MBOOT_INFO_NAME     0x200

static void build_multiboot_info(const char *cmdline)
{
	struct multiboot_info *mbi = (void *)MBOOT_INFO_ADDR;
	int i;

	/* Zero it */
	uint8_t *p = (uint8_t *)mbi;
	for (i = 0; i < (int)sizeof(*mbi); i++)
		p[i] = 0;

	mbi->flags = MBOOT_INFO_MEMORY | MBOOT_INFO_CMDLINE |
	             MBOOT_INFO_BOOTDEV | MBOOT_INFO_NAME;

	/* Memory sizes from E820 map */
	uint32_t mem_lower = 640;   /* conventional memory, KB */
	uint32_t mem_upper = 0;
	for (i = 0; i < (int)g_e820_count; i++) {
		uint64_t base = g_e820_map[i].base;
		uint64_t len  = g_e820_map[i].len;
		uint32_t type = g_e820_map[i].type;
		if (type == 1 && base <= 0x100000 && base + len > 0x100000) {
			mem_upper = (uint32_t)((base + len - 0x100000) / 1024);
		} else if (type == 1 && base == 0x100000) {
			mem_upper = (uint32_t)(len / 1024);
		} else if (type == 1 && base >= 0x100000 && mem_upper == 0) {
			mem_upper = (uint32_t)((base + len - 0x100000) / 1024);
		}
	}
	mbi->mem_lower = mem_lower;
	mbi->mem_upper = mem_upper;

	/* Boot device: 0x80 = first hard disk, partition 0 */
	mbi->boot_device = (g_drive << 24) | 0xFFFFFF;

	/* Command line */
	char *dst = (char *)MBOOT_CMDLINE_ADDR;
	const char *src = cmdline;
	i = 0;
	while (*src && i < 255) {
		dst[i++] = *src++;
	}
	dst[i] = '\0';
	mbi->cmdline = MBOOT_CMDLINE_ADDR;

	/* Boot loader name */
	static const char loader_name[] = "Substrate boot";
	char *name = (char *)MBOOT_NAME_ADDR;
	for (i = 0; loader_name[i]; i++)
		name[i] = loader_name[i];
	name[i] = '\0';
	mbi->boot_loader_name = MBOOT_NAME_ADDR;

	/* E820 memory map -> multiboot mmap format */
	if (g_e820_count > 0) {
		mbi->flags |= MBOOT_INFO_MEM_MAP;
		struct multiboot_mmap_entry *mmap =
		    (struct multiboot_mmap_entry *)MBOOT_MMAP_ADDR;
		for (i = 0; i < (int)g_e820_count; i++) {
			mmap[i].size = 20;  /* size of entry excluding this field */
			mmap[i].addr = g_e820_map[i].base;
			mmap[i].len  = g_e820_map[i].len;
			mmap[i].type = g_e820_map[i].type;
		}
		mbi->mmap_addr = MBOOT_MMAP_ADDR;
		mbi->mmap_length = g_e820_count * sizeof(struct multiboot_mmap_entry);
	}
}

static void __attribute__((noreturn))
jump_to_kernel(uint32_t entry)
{
	__asm__ volatile (
		"mov %[mbi], %%ebx\n\t"
		"mov %[magic], %%eax\n\t"
		"jmp *%[ep]\n\t"
		:
		: [mbi] "i"(MBOOT_INFO_ADDR),
		  [magic] "i"(MULTIBOOT_MAGIC),
		  [ep] "c"(entry)   /* force ECX to avoid EAX/EBX conflict */
		: "eax", "ebx", "memory"
	);
	__builtin_unreachable();
}

/* ==== Main ==== */

#define DEFAULT_KERNEL "vmunix"
#define BOOT_TIMEOUT   5            /* seconds to wait for input */

__attribute__((section(".text.entry")))
void stage2_main(uint32_t e820_count, struct e820_entry *e820_map,
                 uint32_t drive_num)
{
	g_e820_count = e820_count;
	g_e820_map = e820_map;
	g_drive = drive_num;

	serial_init();
	vga_clear();

	puts("Substrate boot\n\n");

	/* Initialize ext2 */
	if (ext2_init() < 0) {
		puts("Error: not an ext2 filesystem\n");
		goto hang;
	}

	puts("boot: ");

	/* Wait for keypress or timeout */
	int got_key = wait_for_key(BOOT_TIMEOUT);

	const char *kernel_name = DEFAULT_KERNEL;
	cmdline_buf[0] = '\0';

	if (got_key) {
		/* User wants to type a command line */
		int pos = 0;
		char c;

		/* Read what was already pressed */
		c = kbd_trygetc();
		if (c == '\n' || c == '\r') {
			putc_both('\n');
			goto do_boot;
		}
		if (c > 0 && c != '\b') {
			cmdline_buf[pos++] = c;
			putc_both(c);
		}

		/* Read rest of line */
		while (pos < (int)sizeof(cmdline_buf) - 1) {
			c = kbd_getc();
			if (c == '\n' || c == '\r') {
				putc_both('\n');
				break;
			}
			if (c == '\b') {
				if (pos > 0) {
					pos--;
					putc_both('\b');
				}
				continue;
			}
			if (c == 0)
				continue;
			cmdline_buf[pos++] = c;
			putc_both(c);
		}
		cmdline_buf[pos] = '\0';

		/* If user typed something, use it as the command line.
		 * The first word (up to space) may be a kernel path.
		 * If it starts with '/', strip the leading slash. */
		if (pos > 0) {
			/* Check if first word looks like a kernel name */
			const char *p = cmdline_buf;
			if (*p == '/')
				p++;
			/* Find end of first word */
			const char *word_end = p;
			while (*word_end && *word_end != ' ')
				word_end++;
			/* Use the whole input as the command line */
		}
	} else {
		puts(DEFAULT_KERNEL);
		putc_both('\n');
	}

do_boot:
	/* Determine kernel filename from cmdline (first word) */
	{
		const char *p = cmdline_buf;
		if (*p == '/')
			p++;
		/* If cmdline is empty, use default */
		if (*p == '\0') {
			kernel_name = DEFAULT_KERNEL;
			/* Set default command line */
			const char *def = DEFAULT_KERNEL;
			int i = 0;
			while (def[i]) {
				cmdline_buf[i] = def[i];
				i++;
			}
			cmdline_buf[i] = '\0';
		} else {
			/* First word of cmdline is the kernel name */
			static char kname[64];
			int i = 0;
			while (*p && *p != ' ' && i < 63) {
				kname[i++] = *p++;
			}
			kname[i] = '\0';
			kernel_name = kname;
		}
	}

	/* Look up the kernel in the root directory */
	puts("Loading /");
	puts(kernel_name);
	puts("...\n");

	uint32_t ino = ext2_lookup(kernel_name);
	if (ino == 0) {
		puts("Error: /");
		puts(kernel_name);
		puts(" not found\n");
		goto hang;
	}

	struct ext2_inode *kernel_inode = ext2_read_inode(ino);
	uint32_t ksize = kernel_inode->i_size;

	puts("  size: ");
	putdec(ksize);
	putc_both('\n');

	/* Read entire kernel into a temp buffer above 0x200000 (2MB) */
	uint8_t *kbuf = (uint8_t *)0x00200000;
	ext2_read_file(kernel_inode, kbuf);

	/* Load ELF segments to their physical addresses */
	uint32_t entry = load_elf(kbuf);
	if (entry == 0) {
		puts("Error: could not load ELF kernel\n");
		goto hang;
	}

	puts("  entry: ");
	puthex(entry);
	putc_both('\n');

	/* Build multiboot info */
	build_multiboot_info(cmdline_buf);

	puts("Booting...\n\n");

	/* Jump to kernel */
	jump_to_kernel(entry);

hang:
	puts("\n-- System halted --\n");
	for (;;)
		__asm__ volatile ("cli; hlt");
}
