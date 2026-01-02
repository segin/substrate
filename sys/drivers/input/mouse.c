#include "mouse.h"
#include "ps2.h"
#include "../../arch/i386/io.h"
#include "../../drivers/video/vga.h"

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

void mouse_handler(registers_t *regs) {
    uint8_t status = inb(PS2_STATUS_PORT);
    if (status & 1) {
        uint8_t data = inb(PS2_DATA_PORT);
        // Process mouse data packet here
        (void)data;
    }
    
    // Send EOI to slave PIC
    outb(0xA0, 0x20);
    // Send EOI to master PIC
    outb(0x20, 0x20);
    (void)regs;
}
