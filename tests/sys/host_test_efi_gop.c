/*
 * host_test_efi_gop.c - Host-side tests for EFI GOP mode selection and
 *                       EFI runtime services kernel interface.
 *
 * Verifies:
 *   1. efi_select_best_gop_mode picks the highest-resolution 32-bit mode
 *      that fits within the 1920x1200 cap.
 *   2. Modes larger than 1920x1200 are skipped.
 *   3. PixelBltOnly modes are skipped.
 *   4. efi_runtime wrappers report unavailable when pointer is NULL.
 *   5. efi_get_time / efi_set_time round-trip correctly with mock RT.
 *   6. efi_reset_system dispatches the correct reset type.
 *
 * Build:
 *   make -C tests/sys host_test_efi_gop
 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ==================== Minimal EFI type stubs ==================== */

typedef unsigned long EFI_STATUS;
#define EFI_SUCCESS 0
#define EFI_LOAD_ERROR 1

typedef struct { uint32_t Data1; uint16_t Data2; uint16_t Data3; uint8_t Data4[8]; } EFI_GUID;

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef enum {
    PixelRedGreenBlueReserved8BitPerColor,
    PixelBlueGreenRedReserved8BitPerColor,
    PixelBitMask,
    PixelBltOnly,
    PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;

typedef struct {
    uint32_t RedMask;
    uint32_t GreenMask;
    uint32_t BlueMask;
    uint32_t ReservedMask;
} EFI_PIXEL_BITMASK;

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
    EFI_PIXEL_BITMASK PixelInformation;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
    unsigned long SizeOfInfo;
    uint64_t FrameBufferBase;
    unsigned long FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef EFI_STATUS (*EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    uint32_t ModeNumber,
    unsigned long *SizeOfInfo,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);

typedef EFI_STATUS (*EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE)(
    EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    uint32_t ModeNumber);

struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_QUERY_MODE QueryMode;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_SET_MODE SetMode;
    void *Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};

typedef struct {
    uint16_t Year;
    uint8_t  Month;
    uint8_t  Day;
    uint8_t  Hour;
    uint8_t  Minute;
    uint8_t  Second;
    uint8_t  Pad1;
    uint32_t Nanosecond;
    int16_t  TimeZone;
    uint8_t  Daylight;
    uint8_t  Pad2;
} EFI_TIME;

typedef struct {
    uint32_t Resolution;
    uint32_t Accuracy;
    uint8_t  SetsToZero;
} EFI_TIME_CAPABILITIES;

typedef enum {
    EfiResetCold,
    EfiResetWarm,
    EfiResetShutdown,
    EfiResetPlatformSpecific
} EFI_RESET_TYPE;

typedef struct EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;

struct EFI_RUNTIME_SERVICES {
    EFI_TABLE_HEADER Hdr;
    EFI_STATUS (*GetTime)(EFI_TIME *Time, EFI_TIME_CAPABILITIES *Capabilities);
    EFI_STATUS (*SetTime)(EFI_TIME *Time);
    void *GetWakeupTime;
    void *SetWakeupTime;
    void *SetVirtualAddressMap;
    void *ConvertPointer;
    EFI_STATUS (*GetVariable)(int16_t *VariableName, EFI_GUID *VendorGuid,
        uint32_t *Attributes, unsigned long *DataSize, void *Data);
    EFI_STATUS (*GetNextVariableName)(unsigned long *VariableNameSize,
        int16_t *VariableName, EFI_GUID *VendorGuid);
    EFI_STATUS (*SetVariable)(int16_t *VariableName, EFI_GUID *VendorGuid,
        uint32_t Attributes, unsigned long DataSize, void *Data);
    void *GetNextHighMonotonicCount;
    void (*ResetSystem)(EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus,
        unsigned long DataSize, void *ResetData);
    void *UpdateCapsule;
    void *QueryCapsuleCapabilities;
    void *QueryVariableInfo;
};

/* Stub EFI system table for efi_select_best_gop_mode's print calls */
struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;
typedef struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    void *Reset;
    EFI_STATUS (*OutputString)(struct _EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, int16_t *String);
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_T;

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void *FirmwareVendor;
    uint32_t FirmwareRevision;
    void *ConsoleInHandle;
    void *ConIn;
    void *ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_T *ConOut;
} EFI_SYSTEM_TABLE;

/* ==================== Mock infrastructure ==================== */

#define MAX_MOCK_MODES 8
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION mock_modes[MAX_MOCK_MODES];
static int mock_mode_count = 0;
static uint32_t last_set_mode = 0xFFFFFFFF;
static int set_mode_called = 0;

static EFI_STATUS mock_query_mode(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    uint32_t ModeNumber, unsigned long *SizeOfInfo,
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info) {
    (void)This;
    if (ModeNumber >= (uint32_t)mock_mode_count) return EFI_LOAD_ERROR;
    *SizeOfInfo = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
    *Info = &mock_modes[ModeNumber];
    return EFI_SUCCESS;
}

static EFI_STATUS mock_set_mode(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
    uint32_t ModeNumber) {
    (void)This;
    if (ModeNumber >= (uint32_t)mock_mode_count) return EFI_LOAD_ERROR;
    last_set_mode = ModeNumber;
    set_mode_called = 1;
    /* Update the mode's Info to point to the selected mode */
    This->Mode->Mode = ModeNumber;
    This->Mode->Info = &mock_modes[ModeNumber];
    return EFI_SUCCESS;
}

static EFI_STATUS mock_output_string(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_T *This,
    int16_t *String) {
    (void)This;
    (void)String;
    return EFI_SUCCESS;
}

/* ---- Mock for efi_runtime (kprint) ---- */
void kprint(const char *s) { (void)s; }

/* ==================== Bring in efi_select_best_gop_mode ==================== */

/*
 * We cannot #include efi_boot.c (it has inline asm and EFI entry points).
 * Instead, replicate the function under test exactly as it appears in
 * efi_boot.c.  If the implementation changes, this copy must be updated.
 */

#define RELOC(x) (x)

static void efi_print(EFI_SYSTEM_TABLE *st, const char *s) {
    int16_t buf[128];
    int i = 0;
    while (*s && i < 127) { buf[i++] = (int16_t)(*s++); }
    buf[i] = 0;
    st->ConOut->OutputString(st->ConOut, buf);
}

static void efi_print_hex(EFI_SYSTEM_TABLE *st, uint32_t val) {
    char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) { buf[9-i] = hex[val & 0xF]; val >>= 4; }
    buf[10] = 0;
    efi_print(st, buf);
}

static void efi_select_best_gop_mode(EFI_SYSTEM_TABLE *st,
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;
    uint32_t best_mode;
    uint64_t best_pixels;
    uint32_t i;

    if (gop == NULL || gop->Mode == NULL || gop->QueryMode == NULL ||
        gop->SetMode == NULL) {
        return;
    }

    mode = gop->Mode;
    best_mode = mode->Mode;
    best_pixels = 0;

    for (i = 0; i < mode->MaxMode; i++) {
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = NULL;
        unsigned long info_size = 0;
        EFI_STATUS qs;

        qs = gop->QueryMode(gop, i, &info_size, &info);
        if (qs != EFI_SUCCESS || info == NULL) continue;
        if (info->PixelFormat == PixelBltOnly) continue;
        if (info->HorizontalResolution > 1920 ||
            info->VerticalResolution > 1200) continue;
        if (info->PixelFormat == PixelRedGreenBlueReserved8BitPerColor ||
            info->PixelFormat == PixelBlueGreenRedReserved8BitPerColor ||
            info->PixelFormat == PixelBitMask) {
            uint64_t pixels = (uint64_t)info->HorizontalResolution *
                              info->VerticalResolution;
            if (pixels > best_pixels) {
                best_pixels = pixels;
                best_mode = i;
            }
        }
    }

    if (best_mode != mode->Mode) {
        EFI_STATUS ss = gop->SetMode(gop, best_mode);
        if (ss == EFI_SUCCESS) {
            efi_print(st, RELOC("GOP: Switched to mode "));
            efi_print_hex(st, best_mode);
            efi_print(st, RELOC("\r\n"));
        }
    }
}

/* ==================== Bring in efi_runtime wrappers ==================== */

/* We block the real arch header since we defined EFI types above. */
#define _ARCH_I386_EFI_H
#define _KERN_CONSOLE_STUB_H

/* Provide the extern that efi_runtime.c references */
EFI_RUNTIME_SERVICES *efi_saved_runtime_services = NULL;

#include "../../sys/kern/efi_runtime.c"

/* ==================== Tests ==================== */

static void setup_mock_gop(EFI_GRAPHICS_OUTPUT_PROTOCOL *gop,
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *pmode) {
    memset(mock_modes, 0, sizeof(mock_modes));
    mock_mode_count = 0;
    last_set_mode = 0xFFFFFFFF;
    set_mode_called = 0;

    gop->QueryMode = mock_query_mode;
    gop->SetMode = mock_set_mode;
    gop->Blt = NULL;
    gop->Mode = pmode;

    pmode->Mode = 0;
    pmode->MaxMode = 0;
    pmode->Info = NULL;
    pmode->SizeOfInfo = sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);
    pmode->FrameBufferBase = 0xE0000000;
    pmode->FrameBufferSize = 0;
}

static void add_mock_mode(uint32_t w, uint32_t h,
    EFI_GRAPHICS_PIXEL_FORMAT fmt) {
    assert(mock_mode_count < MAX_MOCK_MODES);
    mock_modes[mock_mode_count].HorizontalResolution = w;
    mock_modes[mock_mode_count].VerticalResolution = h;
    mock_modes[mock_mode_count].PixelFormat = fmt;
    mock_modes[mock_mode_count].PixelsPerScanLine = w;
    mock_mode_count++;
}

static EFI_SYSTEM_TABLE *make_fake_st(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_T *con) {
    static EFI_SYSTEM_TABLE st;
    memset(&st, 0, sizeof(st));
    con->OutputString = mock_output_string;
    st.ConOut = con;
    return &st;
}

static void test_selects_highest_resolution(void) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL gop;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE pmode;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_T con;
    EFI_SYSTEM_TABLE *st = make_fake_st(&con);

    setup_mock_gop(&gop, &pmode);
    add_mock_mode(640, 480, PixelBlueGreenRedReserved8BitPerColor);   /* 0 */
    add_mock_mode(1024, 768, PixelRedGreenBlueReserved8BitPerColor);  /* 1 */
    add_mock_mode(1280, 1024, PixelBlueGreenRedReserved8BitPerColor); /* 2 */
    pmode.MaxMode = 3;
    pmode.Mode = 0;
    pmode.Info = &mock_modes[0];

    efi_select_best_gop_mode(st, &gop);

    assert(set_mode_called == 1);
    assert(last_set_mode == 2);  /* 1280x1024 is largest */
    printf("  PASS: selects_highest_resolution\n");
}

static void test_skips_oversized_modes(void) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL gop;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE pmode;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_T con;
    EFI_SYSTEM_TABLE *st = make_fake_st(&con);

    setup_mock_gop(&gop, &pmode);
    add_mock_mode(640, 480, PixelBlueGreenRedReserved8BitPerColor);   /* 0 */
    add_mock_mode(2560, 1440, PixelRedGreenBlueReserved8BitPerColor); /* 1 - too big */
    add_mock_mode(1920, 1080, PixelBlueGreenRedReserved8BitPerColor); /* 2 */
    pmode.MaxMode = 3;
    pmode.Mode = 0;
    pmode.Info = &mock_modes[0];

    efi_select_best_gop_mode(st, &gop);

    assert(set_mode_called == 1);
    assert(last_set_mode == 2);  /* 1920x1080, not the 2560x1440 */
    printf("  PASS: skips_oversized_modes\n");
}

static void test_skips_bltonly(void) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL gop;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE pmode;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_T con;
    EFI_SYSTEM_TABLE *st = make_fake_st(&con);

    setup_mock_gop(&gop, &pmode);
    add_mock_mode(1920, 1080, PixelBltOnly);                          /* 0 - no LFB */
    add_mock_mode(800, 600, PixelRedGreenBlueReserved8BitPerColor);   /* 1 */
    pmode.MaxMode = 2;
    pmode.Mode = 0;
    pmode.Info = &mock_modes[0];

    efi_select_best_gop_mode(st, &gop);

    assert(set_mode_called == 1);
    assert(last_set_mode == 1);  /* 800x600, the only usable mode */
    printf("  PASS: skips_bltonly\n");
}

static void test_keeps_current_when_best(void) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL gop;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE pmode;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_T con;
    EFI_SYSTEM_TABLE *st = make_fake_st(&con);

    setup_mock_gop(&gop, &pmode);
    add_mock_mode(1920, 1080, PixelBlueGreenRedReserved8BitPerColor); /* 0 */
    add_mock_mode(640, 480, PixelRedGreenBlueReserved8BitPerColor);   /* 1 */
    pmode.MaxMode = 2;
    pmode.Mode = 0;
    pmode.Info = &mock_modes[0];

    efi_select_best_gop_mode(st, &gop);

    /* Current mode (0) is already the best; SetMode should NOT be called */
    assert(set_mode_called == 0);
    printf("  PASS: keeps_current_when_best\n");
}

static void test_bitmask_mode_accepted(void) {
    EFI_GRAPHICS_OUTPUT_PROTOCOL gop;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE pmode;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_T con;
    EFI_SYSTEM_TABLE *st = make_fake_st(&con);

    setup_mock_gop(&gop, &pmode);
    add_mock_mode(640, 480, PixelBlueGreenRedReserved8BitPerColor);   /* 0 */
    mock_modes[0].PixelsPerScanLine = 640;
    /* Mode 1: PixelBitMask with custom masks */
    add_mock_mode(1024, 768, PixelBitMask);                           /* 1 */
    mock_modes[1].PixelInformation.RedMask   = 0x00FF0000;
    mock_modes[1].PixelInformation.GreenMask = 0x0000FF00;
    mock_modes[1].PixelInformation.BlueMask  = 0x000000FF;
    pmode.MaxMode = 2;
    pmode.Mode = 0;
    pmode.Info = &mock_modes[0];

    efi_select_best_gop_mode(st, &gop);

    assert(set_mode_called == 1);
    assert(last_set_mode == 1);  /* PixelBitMask mode is accepted */
    printf("  PASS: bitmask_mode_accepted\n");
}

/* ---- Runtime services tests ---- */

static EFI_TIME mock_efi_time;
static int mock_get_time_called = 0;
static int mock_set_time_called = 0;
static EFI_RESET_TYPE mock_reset_type = (EFI_RESET_TYPE)-1;
static int mock_reset_called = 0;

static EFI_STATUS mock_get_time(EFI_TIME *Time, EFI_TIME_CAPABILITIES *Caps) {
    (void)Caps;
    mock_get_time_called++;
    *Time = mock_efi_time;
    return EFI_SUCCESS;
}

static EFI_STATUS mock_set_time_fn(EFI_TIME *Time) {
    mock_set_time_called++;
    mock_efi_time = *Time;
    return EFI_SUCCESS;
}

static void mock_reset_system_fn(EFI_RESET_TYPE ResetType, EFI_STATUS Status,
    unsigned long DataSize, void *Data) {
    (void)Status;
    (void)DataSize;
    (void)Data;
    mock_reset_type = ResetType;
    mock_reset_called++;
}

static void test_runtime_unavailable(void) {
    /* rt should be NULL initially (no efi_runtime_init called) */
    rt = NULL;
    efi_saved_runtime_services = NULL;

    assert(efi_runtime_available() == 0);

    struct efi_time t;
    assert(efi_get_time(&t) == -1);
    assert(efi_set_time(&t) == -1);

    printf("  PASS: runtime_unavailable\n");
}

static void test_runtime_get_set_time(void) {
    static struct EFI_RUNTIME_SERVICES mock_rt;
    memset(&mock_rt, 0, sizeof(mock_rt));
    mock_rt.GetTime = mock_get_time;
    mock_rt.SetTime = mock_set_time_fn;
    mock_rt.ResetSystem = mock_reset_system_fn;

    /* Directly set the static rt pointer used by efi_runtime.c */
    rt = &mock_rt;

    assert(efi_runtime_available() == 1);

    /* Set up the mock EFI time */
    memset(&mock_efi_time, 0, sizeof(mock_efi_time));
    mock_efi_time.Year = 2026;
    mock_efi_time.Month = 3;
    mock_efi_time.Day = 16;
    mock_efi_time.Hour = 14;
    mock_efi_time.Minute = 30;
    mock_efi_time.Second = 45;

    struct efi_time t;
    memset(&t, 0, sizeof(t));
    assert(efi_get_time(&t) == 0);
    assert(t.year == 2026);
    assert(t.month == 3);
    assert(t.day == 16);
    assert(t.hour == 14);
    assert(t.minute == 30);
    assert(t.second == 45);

    /* Modify and set back */
    t.year = 2030;
    t.hour = 8;
    assert(efi_set_time(&t) == 0);
    assert(mock_efi_time.Year == 2030);
    assert(mock_efi_time.Hour == 8);

    printf("  PASS: runtime_get_set_time\n");
}

static void test_runtime_reset(void) {
    static struct EFI_RUNTIME_SERVICES mock_rt;
    memset(&mock_rt, 0, sizeof(mock_rt));
    mock_rt.ResetSystem = mock_reset_system_fn;
    rt = &mock_rt;

    mock_reset_called = 0;
    efi_reset_system(EFI_RESET_COLD);
    assert(mock_reset_called == 1);
    assert(mock_reset_type == EfiResetCold);

    efi_reset_system(EFI_RESET_WARM);
    assert(mock_reset_called == 2);
    assert(mock_reset_type == EfiResetWarm);

    efi_reset_system(EFI_RESET_SHUTDOWN);
    assert(mock_reset_called == 3);
    assert(mock_reset_type == EfiResetShutdown);

    printf("  PASS: runtime_reset\n");

    rt = NULL;
}

int main(void) {
    printf("host_test_efi_gop:\n");

    /* GOP mode selection tests */
    test_selects_highest_resolution();
    test_skips_oversized_modes();
    test_skips_bltonly();
    test_keeps_current_when_best();
    test_bitmask_mode_accepted();

    /* EFI runtime tests */
    test_runtime_unavailable();
    test_runtime_get_set_time();
    test_runtime_reset();

    puts("host_test_efi_gop: PASS");
    return 0;
}
