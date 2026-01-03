#include "ps2.h"
#include "../../arch/i386/io.h"
#include "../../kern/console.h"
#include <stdio.h>

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

void ps2_write_aux(uint8_t data) {
    ps2_write_command(0xD4);
    ps2_write_data(data);
}

uint8_t ps2_read_data(void) {
    ps2_wait_read();
    return inb(PS2_DATA_PORT);
}

uint8_t ps2_read_data_timeout(uint32_t timeout) {
    uint32_t t = timeout;
    while (t--) {
        if (inb(PS2_STATUS_PORT) & 1) return inb(PS2_DATA_PORT);
        // Simple delay loop
        __asm__ volatile("nop; nop; nop; nop");
    }
    return 0xFF; // Timeout/Error
}

void ps2_init(void) {
    kprint("PS/2: Initializing controller...\n");

    // 1. Disable devices
    ps2_write_command(PS2_CMD_DISABLE_P1);
    ps2_write_command(PS2_CMD_DISABLE_P2);

    // 2. Flush buffer
    while (inb(PS2_STATUS_PORT) & 1) inb(PS2_DATA_PORT);

    // 3. Set config byte
    ps2_write_command(PS2_CMD_READ_CONFIG);
    uint8_t config = ps2_read_data();
    // Enable IRQ1 (bit 0) and IRQ12 (bit 1) later.
    // Ensure translation (bit 6) is ENABLED for Set 1 scancodes if needed, 
    // but modern advice says disable translation if you want raw set 2.
    // For now, let's keep translation ON as our keyboard driver likely expects Set 1.
    config &= ~(1 | 2 | 0x40); // Clear IRQs and Translation to start fresh?
                               // Actually, let's KEEP translation (bit 6) if it was on, or force it on.
    config |= 0x40; // Enable Translation (Set 2 -> Set 1)
    
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);

    // 4. Controller self-test
    ps2_write_command(PS2_CMD_SELF_TEST);
    if (ps2_read_data() != 0x55) {
        kprint("PS/2: Controller self-test failed!\n");
        return;
    }
    
    // 5. Check for Dual Channel
    uint8_t dual_channel = 0;
    ps2_write_command(PS2_CMD_ENABLE_P2);
    ps2_write_command(PS2_CMD_READ_CONFIG);
    config = ps2_read_data();
    if (!(config & 0x20)) { // Bit 5 should be 1 if disabled? No, wait. 
        // If ENABLE_P2 cleared the disable bit (5), then dual channel exists.
        // Bit 5 is "Mouse Clock Disable". 0 = Enabled.
        dual_channel = 1;
        ps2_write_command(PS2_CMD_DISABLE_P2); // Disable again for now
    }
    
    // 6. Interface Tests
    ps2_write_command(PS2_CMD_P1_TEST);
    if (ps2_read_data() != 0x00) {
        kprint("PS/2: Port 1 failed test.\n");
        return;
    }
    
    if (dual_channel) {
        ps2_write_command(PS2_CMD_P2_TEST);
        if (ps2_read_data() != 0x00) {
             kprint("PS/2: Port 2 failed test.\n");
             dual_channel = 0;
        } else {
             kprint("PS/2: Dual Channel Detected.\n");
        }
    }

    // 7. Enable devices and interrupts
    ps2_write_command(PS2_CMD_ENABLE_P1);
    config = ps2_read_data(); // Read current config (why? we need to re-read to enable IRQs)
    ps2_write_command(PS2_CMD_READ_CONFIG);
    config = ps2_read_data();
    config |= 1; // IRQ 1
    
    if (dual_channel) {
        ps2_write_command(PS2_CMD_ENABLE_P2);
        config |= 2; // IRQ 12
    }
    
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);
    
    // 8. Initialize Mouse (if present)
    if (dual_channel) {
        ps2_write_aux(PS2_MOUSE_RESET);
        // Mouse sends ACK (0xFA), then self-test (0xAA), then ID (0x00)
        uint8_t ack = ps2_read_data_timeout(100000);
        uint8_t test = ps2_read_data_timeout(100000);
        uint8_t id = ps2_read_data_timeout(100000);
        
        char buf[64];
        sprintf(buf, "PS/2: Mouse Reset. ACK=%x Test=%x ID=%x\n", ack, test, id);
        kprint(buf);
        
        ps2_write_aux(PS2_MOUSE_SET_DEFAULTS);
        ps2_read_data_timeout(100000); // ACK
        
        ps2_write_aux(PS2_MOUSE_ENABLE_DATA);
        ps2_read_data_timeout(100000); // ACK
        kprint("PS/2: Mouse Enabled.\n");
    }

    kprint("PS/2: Controller initialized.\n");
}
