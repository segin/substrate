#ifndef _EFI_H
#define _EFI_H

#include <stdint.h>

// Minimal EFI definitions
#define EFI_SUCCESS 0

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

typedef struct {
    // ... Text Output Protocol ...
    void *Reset;
    void *OutputString;
    // ...
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    void *FirmwareVendor;
    uint32_t FirmwareRevision;
    void *ConsoleInHandle;
    void *ConIn;
    void *ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *ConOut;
    // ...
};

typedef void *EFI_HANDLE;

// Entry point signature
typedef uint64_t (*EFI_IMAGE_ENTRY_POINT)(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);

#endif
