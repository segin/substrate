/*
 * sysrq.c - Magic SysRq key subsystem
 *
 * Provides emergency keyboard/serial commands for debugging and recovery.
 * Activated by Alt+SysRq+key on PS/2 keyboard, or serial break + key.
 */

#include <sys/reboot.h>
#include <sys/syscall_impl.h>
#include <kern/console.h>
#include <kern/debug.h>
#include <kern/sysrq.h>
#include <machine/cpu.h>
#include <machine/pmm.h>
#include <vm/phys_mem.h>
#include <arch/x86-common/io.h>

void sysrq_init(void)
{
	/* Nothing to initialize yet; table is static. */
}

static void sysrq_reboot(void)
{
	kprint("SysRq: Rebooting...\n");
	/* Keyboard controller reset */
	while (inb(0x64) & 0x02)
		;
	outb(0x64, 0xFE);
	/* Fallback: triple fault */
	x86_triple_fault();
}

static void sysrq_sync(void)
{
	kprint("SysRq: Emergency Sync\n");
	sys_sync();
}

static void sysrq_show_tasks(void)
{
	kprint("SysRq: Show Tasks\n");
	debug_dump_processes();
}

static void sysrq_show_memory(void)
{
	/* Pages * 4 = kB; see gen_meminfo() in fs/procfs.c. */
	uint32_t free_kb = (uint32_t)(vm_phys_get_free() * 4);
	uint32_t total_kb = free_kb + (uint32_t)(vm_phys_get_used() * 4);

	kprintf("SysRq: Memory Info\n");
	kprintf("  Total: %u KB\n", total_kb);
	kprintf("  Free:  %u KB\n", free_kb);
	kprintf("  Used:  %u KB\n", total_kb - free_kb);
}

static void sysrq_help(void)
{
	kprint("SysRq: HELP\n");
	kprint("  b - reboot\n");
	kprint("  h - help\n");
	kprint("  m - memory info\n");
	kprint("  p - process dump\n");
	kprint("  s - sync filesystems\n");
	kprint("  t - show tasks\n");
}

void sysrq_handle(int key)
{
	/* Normalize to lowercase */
	if (key >= 'A' && key <= 'Z')
		key = key - 'A' + 'a';

	switch (key) {
	case 'b':
		sysrq_reboot();
		break;
	case 'h':
		sysrq_help();
		break;
	case 'm':
		sysrq_show_memory();
		break;
	case 'p':
	case 't':
		sysrq_show_tasks();
		break;
	case 's':
		sysrq_sync();
		break;
	default:
		kprintf("SysRq: unknown key '%c' (0x%02x) — try 'h' for help\n",
			(key >= 0x20 && key < 0x7F) ? key : '?', key);
		break;
	}
}
