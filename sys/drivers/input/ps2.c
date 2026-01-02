#include "ps2.h"
#include "../../arch/i386/io.h"
#include "../../drivers/video/vga.h"

static void ps2_wait_write(void) {
    while (inb(PS2_STATUS_PORT) & 2);
}

static void ps2_wait_read(void) {
    while (!(inb(PS2_STATUS_PORT) & 1));
}

void ps2_write_command(uint8_t cmd) {
    ps2_wait_write();
    outb(PS2_COMMAND_PORT, cmd);
}

void ps2_write_data(uint8_t data) {
    ps2_wait_write();
    outb(PS2_DATA_PORT, data);
}

uint8_t ps2_read_data(void) {
    ps2_wait_read();
    return inb(PS2_DATA_PORT);
}

void ps2_init(void) {
    vga_write("PS/2: Initializing controller...\n", 33);

    // 1. Disable devices
    ps2_write_command(PS2_CMD_DISABLE_P1);
    ps2_write_command(PS2_CMD_DISABLE_P2);

    // 2. Flush buffer
    while (inb(PS2_STATUS_PORT) & 1) inb(PS2_DATA_PORT);

    // 3. Set config byte (disable interrupts for now)
    ps2_write_command(PS2_CMD_READ_CONFIG);
    uint8_t config = ps2_read_data();
    config &= ~(1 | 2 | 0x40); // Disable IRQ1, IRQ12, and Translation
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);

    // 4. Controller self-test
    ps2_write_command(PS2_CMD_SELF_TEST);
    if (ps2_read_data() != 0x55) {
        vga_write("PS/2: Controller self-test failed!\n", 35);
        return;
    }

    // 5. Enable first port
    ps2_write_command(PS2_CMD_ENABLE_P1);
    
    // Enable IRQ1 in config
    ps2_write_command(PS2_CMD_READ_CONFIG);
    config = ps2_read_data();
    config |= 1;
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);

    vga_write("PS/2: Controller initialized.\n", 30);
}
