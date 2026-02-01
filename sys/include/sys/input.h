#ifndef _SYS_INPUT_H
#define _SYS_INPUT_H

#include <stdint.h>
#include <sys/file.h> // for fs_node_t, off_t

// Event Types
#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define EV_ABS 0x03
#define EV_MSC 0x04

// Key Codes (Subset of Linux input.h)
#define KEY_RESERVED 0
#define KEY_ESC      1
#define KEY_1        2
#define KEY_2        3
// ... extensive list omitted for now, drivers use raw scancodes or defined mappings

// Relative Axes
#define REL_X      0x00
#define REL_Y      0x01
#define REL_WHEEL  0x08

// Input Event Structure (Linux compatible)
typedef struct input_event {
    uint64_t time_sec;
    uint64_t time_usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
} input_event_t;

struct input_dev;
struct input_handle;

// Operations for an input device
typedef struct input_dev_ops {
    void (*set_leds)(struct input_dev *dev, int leds);
    int  (*poll)(struct input_dev *dev); // For polling-mode devices
} input_dev_ops_t;

// Input Device Representation
typedef struct input_dev {
    char name[64];
    uint32_t caps; // Capabilities bitmap
    input_dev_ops_t *ops;
    void *driver_data;
    struct input_dev *next;
} input_dev_t;

// API
void input_init(void);

// Driver API
int input_register_device(input_dev_t *dev);
void input_unregister_device(input_dev_t *dev);
void input_report_event(input_dev_t *dev, uint16_t type, uint16_t code, int32_t value);
static inline void input_report_key(input_dev_t *dev, uint16_t code, int32_t value) {
    input_report_event(dev, EV_KEY, code, value);
}
static inline void input_report_rel(input_dev_t *dev, uint16_t code, int32_t value) {
    input_report_event(dev, EV_REL, code, value);
}
void input_sync(input_dev_t *dev); // Send EV_SYN

// Internal/Legacy Compatibility (to be deprecated or mapped)
void input_enqueue(uint16_t type, uint16_t code, int32_t value); // Legacy wrapper

#endif
