#include "elf.h"
#include "../../drivers/video/vga.h"
#include "../../exec/perso/personality.h"
#include "../../kern/sched.h"

int elf_check_file(Elf32_Ehdr *hdr) {
    if (!hdr) return 0;
    if (hdr->e_ident[0] != ELFMAG0) return 0;
    if (hdr->e_ident[1] != ELFMAG1) return 0;
    if (hdr->e_ident[2] != ELFMAG2) return 0;
    if (hdr->e_ident[3] != ELFMAG3) return 0;
    return 1;
}

int elf_load_file(void *file, uint32_t size) {
    (void)size;
    Elf32_Ehdr *hdr = (Elf32_Ehdr*)file;
    if (!elf_check_file(hdr)) {
        vga_write("Not a valid ELF file.\n", 22);
        return -1;
    }

    vga_write("Loading ELF file...\n", 20);

    // Branding / Personality Check
    if (hdr->e_ident[EI_OSABI] == ELFOSABI_FREEBSD) {
        vga_write("Detected FreeBSD ELF.\n", 22);
        if (current_process) current_process->pers = &personality_freebsd;
    } else if (hdr->e_ident[EI_OSABI] == ELFOSABI_LINUX) {
        vga_write("Detected Linux ELF.\n", 20);
        if (current_process) current_process->pers = &personality_linux;
    } else if (hdr->e_ident[EI_OSABI] == ELFOSABI_TESTUNIX || hdr->e_ident[EI_OSABI] == ELFOSABI_SYSV) {
        vga_write("Detected TestUnix ELF.\n", 23);
        if (current_process) current_process->pers = &personality_native;
    } else if (hdr->e_ident[EI_OSABI] == ELFOSABI_ATT_UNIX || hdr->e_ident[EI_OSABI] == ELFOSABI_MODESTO) {
        vga_write("Detected SVR4 ELF.\n", 19);
        if (current_process) current_process->pers = &personality_svr4;
    } else {
        vga_write("Unknown ELF OSABI.\n", 19);
        // Default to native?
        if (current_process) current_process->pers = &personality_native;
    }
    
    // Parsing Program Headers and loading segments would go here.
    
    return 0;
}

