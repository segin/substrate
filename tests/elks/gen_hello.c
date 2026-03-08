#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct elks_exec {
    uint8_t  a_magic[2];
    uint8_t  a_flags;
    uint8_t  a_cpu;
    uint8_t  a_hdrlen;
    uint8_t  a_unused;
    uint16_t a_version;
    uint32_t a_text;
    uint32_t a_data;
    uint32_t a_bss;
    uint32_t a_entry;
    uint32_t a_total;
    uint32_t a_syms;
};

int main() {
    struct elks_exec hdr;
    memset(&hdr, 0, sizeof(hdr));
    
    hdr.a_magic[0] = 0x01;
    hdr.a_magic[1] = 0x03;
    hdr.a_flags = 0x10; // Executable
    hdr.a_cpu = 0x10;   // 8086
    hdr.a_hdrlen = sizeof(hdr);
    
    // 16-bit code:
    // B8 04 00    mov ax, 4
    // BB 01 00    mov bx, 1
    // B9 14 00    mov cx, 0x0014 (offset to msg in text segment)
    // BA 0D 00    mov dx, 13
    // CD 80       int 0x80
    // B8 01 00    mov ax, 1
    // BB 00 00    mov bx, 0
    // CD 80       int 0x80
    // msg: "Hello, ELKS!\n" (13 bytes)
    
    uint8_t code[] = {
        0xB8, 0x04, 0x00,
        0xBB, 0x01, 0x00,
        0xB9, 0x14, 0x00,
        0xBA, 0x0D, 0x00,
        0xCD, 0x80,
        0xB8, 0x01, 0x00,
        0xBB, 0x00, 0x00,
        0xCD, 0x80,
        'H', 'e', 'l', 'l', 'o', ',', ' ', 'E', 'L', 'K', 'S', '!', '\n'
    };
    
    hdr.a_text = sizeof(code);
    hdr.a_total = 0x10000; // 64KB total
    
    FILE *f = fopen("hello_elks", "wb");
    fwrite(&hdr, 1, sizeof(hdr), f);
    fwrite(code, 1, sizeof(code), f);
    fclose(f);
    
    printf("Created hello_elks test binary\n");
    return 0;
}
