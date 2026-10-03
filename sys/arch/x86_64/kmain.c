/*
 * kmain.c - x86_64 kernel entry (bring-up milestone 0)
 *
 * Reached from boot.S in 64-bit mode at the higher half.  This brings up
 * the CPU tables the rest of the port builds on -- GDT with the IA-32
 * compatibility segments, TSS, IDT -- reports what the loader passed, and
 * proves the exception path round trip.  Physical memory management, the
 * MI kernel and the compat32 path to the existing i386 userland are the
 * next milestones (docs/specs/arch_x86_64_core.md).
 */
#include <stddef.h>
#include <stdint.h>

#include <arch/x86-common/multiboot.h>
#include <arch/x86_64/boot.h>
#include <arch/x86_64/cons.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/idt.h>
#include <arch/x86_64/layout.h>
#include <arch/x86_64/trap.h>
#include <sys/smp.h>

/* One CPU until the SMP bring-up is ported. */
int smp_get_cpu_id(void)
{
	return 0;
}

static void cpuid(uint32_t leaf, uint32_t r[4])
{
	__asm__ volatile("cpuid"
	                 : "=a"(r[0]), "=b"(r[1]), "=c"(r[2]), "=d"(r[3])
	                 : "a"(leaf), "c"(0));
}

static void print_cpu(void)
{
	uint32_t r[4];
	char brand[49];
	int i;

	cpuid(0x80000000, r);
	if (r[0] < 0x80000004) {
		return;
	}
	for (i = 0; i < 3; i++) {
		cpuid(0x80000002 + (uint32_t)i, (uint32_t *)(void *)&brand[16 * i]);
	}
	brand[48] = '\0';
	econs_printf("cpu: %s\n", brand);
}

static const char *mem_type(uint32_t type)
{
	switch (type) {
	case MULTIBOOT_MEMORY_AVAILABLE:        return "available";
	case MULTIBOOT_MEMORY_ACPI_RECLAIMABLE: return "ACPI";
	case MULTIBOOT_MEMORY_NVS:              return "ACPI NVS";
	case MULTIBOOT_MEMORY_BADRAM:           return "bad";
	default:                                return "reserved";
	}
}

/* The loader's structures sit below 4 GiB: reach them through the
 * direct map, which the boot page tables already cover. */
static uint64_t print_memory_map(uint32_t magic, uint32_t info_phys)
{
	uint64_t usable = 0;

	if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
		const multiboot_info_t *mbi = phys_to_dmap(info_phys);
		uint64_t p, end;

		econs_puts("boot: multiboot\n");
		if (!(mbi->flags & MULTIBOOT_INFO_MEM_MAP)) {
			econs_puts("boot: no memory map\n");
			return 0;
		}
		p = mbi->mmap_addr;
		end = p + mbi->mmap_length;
		while (p < end) {
			const multiboot_mmap_entry_t *e = phys_to_dmap(p);

			econs_printf("  %016lx-%016lx %s\n", e->addr,
			             e->addr + e->len - 1, mem_type(e->type));
			if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
				usable += e->len;
			}
			p += e->size + 4;
		}
	} else if (magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
		const uint8_t *base = phys_to_dmap(info_phys);
		uint32_t total = *(const uint32_t *)base;
		uint32_t off = 8;

		econs_puts("boot: multiboot2\n");
		while (off + 8 <= total) {
			const multiboot2_tag_t *t = (const void *)(base + off);

			if (t->type == MULTIBOOT2_TAG_TYPE_END) {
				break;
			}
			if (t->type == MULTIBOOT2_TAG_TYPE_MMAP) {
				const multiboot2_tag_mmap_t *m = (const void *)t;
				uint32_t eo;

				for (eo = 16; eo + m->entry_size <= m->size;
				     eo += m->entry_size) {
					const multiboot2_mmap_entry_t *e =
					    (const void *)((const uint8_t *)m + eo);

					econs_printf("  %016lx-%016lx %s\n", e->addr,
					             e->addr + e->len - 1,
					             mem_type(e->type));
					if (e->type == MULTIBOOT_MEMORY_AVAILABLE) {
						usable += e->len;
					}
				}
			}
			off += (t->size + 7) & ~7u;
		}
	} else {
		econs_printf("boot: unknown loader magic %08x\n", magic);
	}
	return usable;
}

void kmain64(uint32_t magic, uint32_t info_phys)
{
	uint64_t usable;
	int failures = 0;

	econs_init();
	econs_puts("\nSubstrate x86_64 kernel (bring-up)\n");
	econs_printf("kernel: %p-%p, loaded at %lx\n", (void *)_kernel_start,
	             (void *)_kernel_end, kva_to_phys(_kernel_start));
	print_cpu();
	usable = print_memory_map(magic, info_phys);
	econs_printf("memory: %lu MiB usable\n", usable >> 20);

	/* Own GDT and TSS before the identity window goes: the loader's GDT
	 * is only reachable through it. */
	gdt_init();
	econs_printf("gdt: kcode %02x kdata %02x ucode32 %02x udata %02x "
	             "ucode %02x tss %02x\n", SEL_KCODE, SEL_KDATA, SEL_UCODE32,
	             SEL_UDATA, SEL_UCODE, SEL_TSS);
	idt_init();

	boot_pml4[0] = 0;
	__asm__ volatile("movq %%cr3, %%rax; movq %%rax, %%cr3"
	                 ::: "rax", "memory");
	econs_puts("vm: identity window dropped\n");

	/* The exception path, there and back. */
	__asm__ volatile("int3");
	if (trap_breakpoints == 1) {
		econs_puts("trap: breakpoint taken and returned\n");
	} else {
		econs_printf("trap: breakpoint count %lu, expected 1\n",
		             (uint64_t)trap_breakpoints);
		failures++;
	}

	econs_printf("Result: %s (x86_64 milestone 0)\n",
	             failures ? "FAIL" : "PASS");
	for (;;) {
		__asm__ volatile("cli; hlt");
	}
}
