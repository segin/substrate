#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct elks_exec {
    uint32_t type;
    uint8_t  hlen;
    uint8_t  reserved1;
    uint16_t version;
    uint16_t tseg;
    uint16_t reserved2;
    uint16_t dseg;
    uint16_t reserved3;
    uint16_t bseg;
    uint16_t reserved4;
    uint32_t entry;
    uint16_t chmem;
    uint16_t minstack;
    uint32_t syms;
};

int main() {
    struct elks_exec hdr;
    memset(&hdr, 0, sizeof(hdr));
    
    hdr.type = 0x04100301u;
    hdr.hlen = sizeof(hdr);
    hdr.version = 1;
    
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
    
    hdr.tseg = sizeof(code);
    hdr.dseg = 0;
    hdr.bseg = 0;
    hdr.entry = 0;
    hdr.chmem = 0;
    hdr.minstack = 4096;
    
    FILE *f = fopen("hello_elks", "wb");
    fwrite(&hdr, 1, sizeof(hdr), f);
    fwrite(code, 1, sizeof(code), f);
    fclose(f);
    
    printf("Created hello_elks test binary\n");
    return 0;
}
