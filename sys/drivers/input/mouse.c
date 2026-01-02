#include "mouse.h"
#include "ps2.h"
#include "../../arch/i386/io.h"
#include "../../drivers/video/vga.h"
#include "../../sys/input.h"

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

void mouse_init(void) {
    vga_write("Mouse: Initializing...\n", 23);

    // 1. Enable auxiliary device
    ps2_write_command(PS2_CMD_ENABLE_P2);

    // 2. Enable interrupts in configuration byte
    ps2_write_command(PS2_CMD_READ_CONFIG);
    uint8_t config = ps2_read_data();
    config |= 2; // Enable IRQ12
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);

    // 3. Set default settings
    ps2_write_aux(0xF6);
    ps2_read_data(); // ACK (0xFA)

    // 4. Enable packet streaming
    ps2_write_aux(0xF4);
    ps2_read_data(); // ACK (0xFA)

    vga_write("Mouse Driver Initialized.\n", 26);
}

static uint8_t mouse_cycle = 0;
static int8_t  mouse_byte[3];
static int32_t mouse_x = 0;
static int32_t mouse_y = 0;

void mouse_handler(registers_t *regs) {
    uint8_t status = inb(PS2_STATUS_PORT);
    if (status & 1) {
        uint8_t data = inb(PS2_DATA_PORT);
        
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
                input_enqueue(EV_REL, 0, dx);
                input_enqueue(EV_REL, 1, -dy);
                input_enqueue(EV_KEY, 0x110, mouse_buttons & 1); // BTN_LEFT

                // Log movement (optional)
                // char buf[64];
                // snprintf(buf, 64, "Mouse: %d, %d buttons=%x\n", mouse_x, mouse_y, mouse_buttons);
                // vga_write(buf, strlen(buf));
                break;
        }
    }
    
    // Send EOI to slave PIC
    outb(0xA0, 0x20);
    // Send EOI to master PIC
    outb(0x20, 0x20);
    (void)regs;
}

void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons) {
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (buttons) *buttons = mouse_buttons;
}
