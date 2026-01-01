#include "efi.h"

// Compiler needs to emit MS ABI for EFI entry? 
// GCC handles this with __attribute__((ms_abi)) on x86_64.

__attribute__((ms_abi)) 
uint64_t efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    (void)ImageHandle;

    // Simple "Hello"
    // Strings in EFI are UCS-2 (char16_t).
    
    // We can't easily write L"String" with -fshort-wchar if it's not set, 
    // but standard C strings are not UCS-2. 
    // We manually construct a short string for test.
    
    short str[] = {'H', 'e', 'l', 'l', 'o', ' ', 'E', 'F', 'I', '\r', '\n', 0};

    // OutputString is the 2nd function pointer in SimpleTextOutput protocol
    // typedef EFI_STATUS (EFIAPI *EFI_TEXT_STRING) (IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN CHAR16 *String);
    
    // Calling convention hell: EFI uses MS ABI. 
    // ConOut->OutputString(ConOut, str);
    
    typedef uint64_t (__attribute__((ms_abi)) *OutputString_t)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, short*);
    OutputString_t out = (OutputString_t)SystemTable->ConOut->OutputString;
    
    out(SystemTable->ConOut, str);

    // Infinite loop to see output
    while(1);

    return EFI_SUCCESS;
}
