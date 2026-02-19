/*
 * EFI Bootloader Stub for IA32
 *
 * This file is compiled as part of the kernel but acts as the EFI entry point.
 * It sets up the environment (paging, GDT, multiboot info) to match what
 * boot.S provides for legacy multiboot loaders, then jumps to kmain.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Include Multiboot definitions
// The Makefile adds -I../x86-common/include
#include <multiboot.h>

#include <kern/main.h>
#include "efi.h"

// --------------------------------------------------------------------------------
// Globals and Imports
// --------------------------------------------------------------------------------

// kmain is the kernel entry point (defined in kern/main.c)


// Address Translation Macros
// We link strings/globals at 0xC0... but we run at 0x40...
// We must manually relocate strings passed to EFI.
#define LINK_BASE 0xC0000000
#define LOAD_BASE 0x00400000
#define RELOC(x) ((const char*)((uint32_t)(x) - LINK_BASE + LOAD_BASE))

// Helpers for printing (simple UCS-2 conversion)
static void efi_print(EFI_SYSTEM_TABLE *st, const char *s) {
    int16_t buf[128];
    int i = 0;
    while (*s && i < 127) {
        buf[i++] = (int16_t)(*s++);
    }
    buf[i] = 0;
    st->ConOut->OutputString(st->ConOut, buf);
}

static void efi_print_hex(EFI_SYSTEM_TABLE *st, uint32_t val) {
    // hex is on stack, safe
    char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[9-i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[10] = 0;
    efi_print(st, buf);
}

// --------------------------------------------------------------------------------
// Entry Point
// --------------------------------------------------------------------------------

// Place in .setup section to ensure it's near the start if we sort sections
__attribute__((section(".setup")))
uint32_t efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS status;
    EFI_BOOT_SERVICES *bs = SystemTable->BootServices;

    // Define GUID on stack to avoid high-memory global access
    EFI_GUID LoadedImageProtocolGUID =
        { 0x5B1B31A1, 0x9562, 0x11D2, { 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B } };

    SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
    efi_print(SystemTable, RELOC("Starting Kernel (EFI IA32)...\r\n"));

    // 1. Get Loaded Image Protocol to find our ImageBase and Size
    EFI_LOADED_IMAGE_PROTOCOL *loaded_image = NULL;
    status = bs->HandleProtocol(ImageHandle, &LoadedImageProtocolGUID, (void**)&loaded_image);
    if (status != EFI_SUCCESS || !loaded_image) {
        efi_print(SystemTable, RELOC("Error: Could not get Loaded Image Protocol\r\n"));
        return status;
    }

    uint32_t image_base = (uint32_t)(uintptr_t)loaded_image->ImageBase;

    efi_print(SystemTable, RELOC("Image Base: "));
    efi_print_hex(SystemTable, image_base);
    efi_print(SystemTable, RELOC("\r\n"));

    // 2. Allocate Memory for Multiboot Info and Mmap
    // We construct a fake Multiboot info structure
    multiboot_info_t *mbi = NULL;
    uint64_t mbi_phys = 0;
    status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &mbi_phys); // 4KB
    if (status != EFI_SUCCESS) {
        efi_print(SystemTable, RELOC("Error: Allocating MBI failed\r\n"));
        return status;
    }
    mbi = (multiboot_info_t *)(uintptr_t)mbi_phys;

    // Clear MBI
    for (size_t i=0; i<sizeof(multiboot_info_t); i++) ((char*)mbi)[i] = 0;

    // Set MBI fields
    mbi->flags = (1<<6); // MMAP valid

    // Allocate buffer for EFI Memory Map (to convert later)
    uint64_t mmap_phys = 0;
    unsigned long mmap_size = 8192; // 8KB should be enough
    status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 2, &mmap_phys);
    if (status != EFI_SUCCESS) {
        efi_print(SystemTable, RELOC("Error: Allocating Mmap buffer failed\r\n"));
        return status;
    }

    // 3. Setup Page Tables
    // We need to map:
    // - Identity 0..16MB (covers boot.S logic equivalents)
    // - AND cover 0xC0000000..0xC0FFFFFF (mapped to ImageBase..ImageBase+16MB)

    // Allocate Page Directory (1 page)
    uint64_t pd_phys = 0;
    status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &pd_phys);
    if (status != EFI_SUCCESS) return status;
    uint32_t *pd = (uint32_t*)(uintptr_t)pd_phys;

    // Allocate 4 Page Tables for Identity Map (0-16MB) + Kernel High Half
    uint64_t pt_phys = 0;
    status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 4, &pt_phys); // 4 pages
    if (status != EFI_SUCCESS) return status;
    uint32_t *pt = (uint32_t*)(uintptr_t)pt_phys;

    // Allocate LAPIC Page Table
    uint64_t lapic_pt_phys = 0;
    status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 1, &lapic_pt_phys);
    if (status != EFI_SUCCESS) return status;
    uint32_t *lapic_pt = (uint32_t*)(uintptr_t)lapic_pt_phys;

    // Zero out PD
    for (int i=0; i<1024; i++) pd[i] = 0;

    // Initialize Page Tables for ImageBase (mapped to 0xC0000000)
    for (int i=0; i < 4 * 1024; i++) {
        uint32_t phys = image_base + (i * 4096);
        pt[i] = phys | 3; // Present + RW
    }

    // Map these 4 PTs to High Half (0xC0000000 -> PDE 768..771)
    for (int i=0; i<4; i++) {
        pd[768+i] = (pt_phys + i*4096) | 3;
    }

    // ALSO identity map the code we are running NOW (at image_base).
    uint32_t pde_start = image_base >> 22;
    for (int i=0; i<4; i++) {
        // Only if it doesn't conflict with high half
        if ((pde_start + i) < 768) {
            pd[pde_start + i] = (pt_phys + i*4096) | 3;
        }
    }

    // Safety: Alloc 4 more pages for Identity 0-16MB fixed (for MBI etc if low).
    uint64_t id_pt_phys = 0;
    bs->AllocatePages(AllocateAnyPages, EfiLoaderData, 4, &id_pt_phys);
    uint32_t *id_pt = (uint32_t*)(uintptr_t)id_pt_phys;
    for (int i=0; i < 4 * 1024; i++) {
        id_pt[i] = (i * 4096) | 3;
    }
    // Map 0-16MB Identity at PDE 0-3
    for (int i=0; i<4; i++) {
        // Don't overwrite if image_base is also low (e.g. 4MB)
        if (pd[i] == 0) {
             pd[i] = (id_pt_phys + i*4096) | 3;
        }
    }

    // LAPIC Mapping
    for (int i=0; i<1024; i++) {
        uint32_t phys = 0xFEC00000 + i*4096;
        lapic_pt[i] = phys | 0x13; // P + RW + PCD
    }
    pd[1019] = lapic_pt_phys | 3;

    // 4. Get Memory Map
    unsigned long map_key = 0;
    unsigned long desc_size = 0;
    uint32_t desc_ver = 0;

    status = bs->GetMemoryMap(&mmap_size, (EFI_MEMORY_DESCRIPTOR*)(uintptr_t)mmap_phys, &map_key, &desc_size, &desc_ver);
    if (status != EFI_SUCCESS) {
        mmap_size += 4096;
        status = bs->GetMemoryMap(&mmap_size, (EFI_MEMORY_DESCRIPTOR*)(uintptr_t)mmap_phys, &map_key, &desc_size, &desc_ver);
    }

    if (status != EFI_SUCCESS) {
        efi_print(SystemTable, RELOC("Error: GetMemoryMap failed\r\n"));
        return status;
    }

    // 5. Exit Boot Services
    status = bs->ExitBootServices(ImageHandle, map_key);
    if (status != EFI_SUCCESS) {
        status = bs->GetMemoryMap(&mmap_size, (EFI_MEMORY_DESCRIPTOR*)(uintptr_t)mmap_phys, &map_key, &desc_size, &desc_ver);
        if (status == EFI_SUCCESS) {
             status = bs->ExitBootServices(ImageHandle, map_key);
        }
    }

    if (status != EFI_SUCCESS) {
        return status;
    }

    // 6. Convert Memory Map to Multiboot
    struct multiboot_mmap_entry {
        uint32_t size;
        uint64_t addr;
        uint64_t len;
        uint32_t type;
    } __attribute__((packed));

    struct multiboot_mmap_entry *mb_mmap = (struct multiboot_mmap_entry *)((uintptr_t)mbi + sizeof(multiboot_info_t));
    uint32_t mb_mmap_count = 0;

    uint8_t *p = (uint8_t*)(uintptr_t)mmap_phys;
    uint8_t *end = p + mmap_size;

    while (p < end) {
        EFI_MEMORY_DESCRIPTOR *d = (EFI_MEMORY_DESCRIPTOR*)p;

        // Convert type
        uint32_t mb_type = 2; // Reserved
        if (d->Type == EfiConventionalMemory || d->Type == EfiLoaderCode || d->Type == EfiLoaderData || d->Type == EfiBootServicesCode || d->Type == EfiBootServicesData) {
             mb_type = 1; // Available
        }

        mb_mmap[mb_mmap_count].size = 20;
        mb_mmap[mb_mmap_count].addr = d->PhysicalStart;
        mb_mmap[mb_mmap_count].len = d->NumberOfPages * 4096;
        mb_mmap[mb_mmap_count].type = mb_type;

        mb_mmap_count++;
        if (mb_mmap_count > 150) break;

        p += desc_size;
    }

    mbi->mmap_addr = (uint32_t)(uintptr_t)mb_mmap;
    mbi->mmap_length = mb_mmap_count * sizeof(struct multiboot_mmap_entry);

    // 7. Enable Paging
    __asm__ volatile("mov %0, %%cr3" :: "r"((uint32_t)pd_phys));

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    // 8. Jump to Kernel
    extern char stack_top[];

    __asm__ volatile (
        "mov %0, %%esp \n\t"
        "push %1 \n\t"        // addr (mbi)
        "push %2 \n\t"        // magic
        "call kmain \n\t"
        :
        : "r"(stack_top), "r"((uint32_t)(uintptr_t)mbi), "i"(0x2BADB002)
        : "memory"
    );

    while(1);
    return EFI_SUCCESS;
}
