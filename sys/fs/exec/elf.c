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
    int detected_os = -1;

    // First check explicit OSABI
    if (hdr->e_ident[EI_OSABI] == ELFOSABI_FREEBSD) {
        detected_os = ELFOSABI_FREEBSD;
    } else if (hdr->e_ident[EI_OSABI] == ELFOSABI_LINUX) {
        detected_os = ELFOSABI_LINUX;
    } else if (hdr->e_ident[EI_OSABI] == ELFOSABI_TESTUNIX) {
        detected_os = ELFOSABI_TESTUNIX;
    } else if (hdr->e_ident[EI_OSABI] == ELFOSABI_ATT_UNIX || hdr->e_ident[EI_OSABI] == ELFOSABI_MODESTO) {
        detected_os = ELFOSABI_ATT_UNIX; // SVR4
    } else if (hdr->e_ident[EI_OSABI] == ELFOSABI_SYSV) {
        // Fallback: Check for GNU Note to detect Linux
        Elf32_Phdr *ph = (Elf32_Phdr *)((uint8_t *)file + hdr->e_phoff);
        for (int i = 0; i < hdr->e_phnum; i++) {
            if (ph[i].p_type == PT_INTERP) {
                // Heuristic: Check for ld-linux.so.2
                 char *interp = (char *)((uint8_t *)file + ph[i].p_offset);
                 // We need to be careful about bounds, but file is trusted for now
                 // or at least "size" covers it. 
                 // Assuming interp is null terminated string in file.
                 // Just check if it contains "ld-linux.so.2"
                 // Simple strstr or exact match check?
                 // Let's check prefix or suffix?
                 // "/lib/ld-linux.so.2"
                 if (ph[i].p_filesz > 0) {
                     // Check for common Linux dynamic linkers
                     char *s = interp;
                     uint32_t len = 0;
                     while(len < ph[i].p_filesz && s[len]) len++;
                                          uint32_t k;
                     // Simple scan for "ld-linux" substring
                     for (k = 0; k < len - 7; k++) {
                         if (s[k] == 'l' && s[k+1] == 'd' && s[k+2] == '-' && 
                             s[k+3] == 'l' && s[k+4] == 'i' && s[k+5] == 'n' && 
                             s[k+6] == 'u' && s[k+7] == 'x') {
                             detected_os = ELFOSABI_LINUX;
                             break;
                         }
                     }
                 }
            } else if (ph[i].p_type == PT_NOTE) {
                uint8_t *note_ptr = (uint8_t *)file + ph[i].p_offset;
                uint8_t *note_end = note_ptr + ph[i].p_filesz;
                
                while (note_ptr < note_end) {
                    Elf32_Nhdr *nh = (Elf32_Nhdr *)note_ptr;
                    char *name = (char *)(note_ptr + sizeof(Elf32_Nhdr));
                    // name is padded to 4 bytes
                    // desc is padded to 4 bytes
                    
                    // Check for "GNU" name and type NT_GNU_ABI_TAG
                    if (nh->n_namesz == 4 && name[0] == 'G' && name[1] == 'N' && name[2] == 'U' && name[3] == 0) {
                        if (nh->n_type == NT_GNU_ABI_TAG) {
                           detected_os = ELFOSABI_LINUX; 
                           break;
                        }
                    }
                    
                    // Advance
                    int name_align = (nh->n_namesz + 3) & ~3;
                    int desc_align = (nh->n_descsz + 3) & ~3;
                    note_ptr += sizeof(Elf32_Nhdr) + name_align + desc_align;
                }
            }
            if (detected_os != -1) break;
        }
        
        // If still not detected, assume Native/SYSV
        if (detected_os == -1) detected_os = ELFOSABI_TESTUNIX;
    }

    if (detected_os == ELFOSABI_FREEBSD) {
        vga_write("Detected FreeBSD ELF.\n", 22);
        if (current_process) current_process->pers = &personality_freebsd;
    } else if (detected_os == ELFOSABI_LINUX) {
        vga_write("Detected Linux ELF.\n", 20);
        if (current_process) current_process->pers = &personality_linux;
    } else if (detected_os == ELFOSABI_TESTUNIX) {
        vga_write("Detected TestUnix ELF.\n", 23);
        if (current_process) current_process->pers = &personality_native;
    } else if (detected_os == ELFOSABI_ATT_UNIX) {
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

