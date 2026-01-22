#include "mouse.h"
#include "ps2.h"
#include <arch/x86-common/include/io.h>
#include <kern/console.h>
#include <sys/input.h>
#include <sys/random.h>

static uint8_t mouse_buttons = 0;

#define MOUSE_QUEUE_SIZE 64
static mouse_event_t mouse_queue[MOUSE_QUEUE_SIZE];
static int mouse_q_head = 0;
static int mouse_q_tail = 0;

static void mouse_q_push(int32_t dx, int32_t dy, uint8_t buttons) {
    int next = (mouse_q_head + 1) % MOUSE_QUEUE_SIZE;
    if (next != mouse_q_tail) {
        mouse_queue[mouse_q_head].dx = dx;
        mouse_queue[mouse_q_head].dy = dy;
        mouse_queue[mouse_q_head].buttons = buttons;
        mouse_q_head = next;
    }
}

int mouse_get_event(mouse_event_t *ev) {
    if (mouse_q_head == mouse_q_tail) return 0;
    if (ev) *ev = mouse_queue[mouse_q_tail];
    mouse_q_tail = (mouse_q_tail + 1) % MOUSE_QUEUE_SIZE;
    return 1;
}

static input_dev_t mouse_dev = {
    .name = "PS/2 Mouse",
    .caps = (1 << EV_REL) | (1 << EV_KEY),
};

void mouse_init(void) {
    if (input_register_device(&mouse_dev) == 0) {
        kprint("Mouse: Driver loaded (initialized via PS/2 controller).\n");
    }
}

static uint8_t mouse_cycle = 0;
static int8_t  mouse_byte[3];
static int32_t mouse_x = 0;
static int32_t mouse_y = 0;

void mouse_handler(registers_t *regs) {
    uint8_t status = inb(PS2_STATUS_PORT);
    if (status & 1) {
        uint8_t data = inb(PS2_DATA_PORT);
        
        /* Harvest entropy from mouse event timing */
        uint32_t entropy_data[2];
        __asm__ volatile("rdtsc" : "=a"(entropy_data[0]), "=d"(entropy_data[1]));
        entropy_data[1] ^= data; /* Mix in data byte */
        random_harvest_fast(entropy_data, sizeof(entropy_data));
        
        switch (mouse_cycle) {
            case 0:
                mouse_byte[0] = data;
                // Bit 3 should always be 1 for the first byte
                if (data & 0x08) mouse_cycle++;
                break;
            case 1:
                mouse_byte[1] = data;
                mouse_cycle++;
                break;
            case 2:
                mouse_byte[2] = data;
                mouse_cycle = 0;

                // Decode
                mouse_buttons = mouse_byte[0] & 0x07; // Left, Right, Middle bits
                
                int32_t dx = (int32_t)mouse_byte[1];
                int32_t dy = (int32_t)mouse_byte[2];

                // Handle sign bits
                if (mouse_byte[0] & 0x10) dx -= 256;
                if (mouse_byte[0] & 0x20) dy -= 256;

                mouse_x += dx;
                mouse_y -= dy; // PS/2 Y-axis is inverted relative to screen coords

                mouse_q_push(dx, -dy, mouse_buttons);
                
                input_report_rel(&mouse_dev, REL_X, dx);
                input_report_rel(&mouse_dev, REL_Y, -dy);
                input_report_key(&mouse_dev, 0x110, mouse_buttons & 1); // BTN_LEFT
                input_report_key(&mouse_dev, 0x111, mouse_buttons & 2); // BTN_RIGHT
                input_report_key(&mouse_dev, 0x112, mouse_buttons & 4); // BTN_MIDDLE
                input_sync(&mouse_dev);

                // Log movement (optional)
                // char buf[64];
                // snprintf(buf, 64, "Mouse: %d, %d buttons=%x\n", mouse_x, mouse_y, mouse_buttons);
                // vga_write(buf, strlen(buf));
                break;
        }
    }
    
    // EOI handled by IDT dispatcher
    (void)regs;
}

void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons) {
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (buttons) *buttons = mouse_buttons;
}
