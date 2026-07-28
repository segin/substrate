/*
 * ps2.c - i8042 PS/2 Controller Driver
 *
 * Implements initialization and I/O for the 8042-compatible PS/2 controller.
 * Supports dual-channel operation (keyboard on port 1, mouse on port 2).
 */

#include <stdio.h>

#include <arch/x86-common/io.h>
#include <drivers/input/mouse.h>
#include <drivers/input/ps2.h>
#include <kern/console.h>

/* Global state */
static int ps2_dual_channel = 0;

/*
 * Detected mouse generation.  Visible to mouse.c via
 * ps2_mouse_get_generation() so the IRQ handler knows packet length
 * and byte-3 layout.
 *
 *   0 — standard 3-byte PS/2 mouse (no wheel, 3 buttons max)
 *   3 — Microsoft IntelliMouse (4-byte packet, signed Z-axis in byte 3)
 *   4 — Microsoft IntelliMouse Explorer (4-byte packet, Z in low nibble,
 *       buttons 4/5 in bits 4/5 of byte 3)
 */
static int ps2_mouse_generation = 0;
int ps2_mouse_get_generation(void) { return ps2_mouse_generation; }

/*
 * ps2_wait_write - Wait until input buffer is empty (ready to receive command/data)
 * Returns 0 on success, -1 on timeout.
 */
int ps2_wait_write(void) {
    uint32_t timeout = PS2_TIMEOUT_LOOPS;
    while (timeout--) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_BUFFER_FULL)) {
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

/*
 * ps2_wait_read - Wait until output buffer has data (ready to be read)
 * Returns 0 on success, -1 on timeout.
 */
int ps2_wait_read(void) {
    uint32_t timeout = PS2_TIMEOUT_LOOPS;
    while (timeout--) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER_FULL) {
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

int ps2_write_command(uint8_t cmd) {
    if (ps2_wait_write()) {
        return -1;
    }
    outb(PS2_COMMAND_PORT, cmd);
    return 0;
}

int ps2_write_data(uint8_t data) {
    if (ps2_wait_write()) {
        return -1;
    }
    outb(PS2_DATA_PORT, data);
    return 0;
}

int ps2_write_aux(uint8_t data) {
    if (ps2_write_command(PS2_CMD_WRITE_P2_INPUT)) return -1;
    return ps2_write_data(data);
}

uint8_t ps2_read_data(void) {
    if (ps2_wait_read()) {
        return 0xFF;
    }
    return inb(PS2_DATA_PORT);
}

/*
 * ps2_read_data_timeout - Read with explicit loop count timeout
 * Returns 0 on success (value stored in *data), -1 on timeout.
 */
int ps2_read_data_timeout(uint8_t *data, uint32_t loop_count) {
    while (loop_count--) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER_FULL) {
            *data = inb(PS2_DATA_PORT);
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

/* 
 * ps2_flush - Drain the output buffer 
 */
static void ps2_flush(void) {
    /* Read up to 16 bytes to prevent infinite loops on broken hardware */
    for (int i = 0; i < 16 && (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_BUFFER_FULL); i++) {
        (void)inb(PS2_DATA_PORT);
        /* Small delay */
        for (volatile int j = 0; j < 100; j++);
    }
}

/*
 * ps2_send_command_with_response - Send command and wait for response byte
 * Returns 0 on success (response in *response), -1 on failure.
 */
static int ps2_send_command_with_response(uint8_t cmd, uint8_t *response) {
    if (ps2_write_command(cmd)) return -1;
    return ps2_read_data_timeout(response, PS2_TIMEOUT_LOOPS);
}

static void ps2_log_port_test_failure(const char *port, uint8_t response) {
    char buf[48];

    snprintf(buf, sizeof(buf), "PS/2: %s test failed (0x%02x)\n", port, response);
    kprint(buf);
}

int ps2_init(void) {
    uint8_t config, response;
    
    kprint("PS/2: Initializing controller...\n");
    
    /* Step 1: Disable devices to prevent interference */
    ps2_write_command(PS2_CMD_DISABLE_P1);
    ps2_write_command(PS2_CMD_DISABLE_P2);

    /* Step 2: Flush output buffer */
    ps2_flush();

    /* Step 3: Read and modify configuration byte */
    if (ps2_send_command_with_response(PS2_CMD_READ_CONFIG, &config)) {
        kprint("PS/2: Failed to read config\n");
        return -1;
    }
    
    /* 
     * Disable IRQs during init (will enable at end).
     * Enable translation for Set 1 scancode compatibility.
     * Set system flag.
     */
    config &= ~(PS2_CFG_FIRST_PORT_INT | PS2_CFG_SECOND_PORT_INT);
    config |= PS2_CFG_TRANSLATION | PS2_CFG_SYSTEM_FLAG;

    if (ps2_write_command(PS2_CMD_WRITE_CONFIG) || ps2_write_data(config)) {
        kprint("PS/2: Failed to write config\n");
        return -1;
    }

    /* Step 4: Controller self-test */
    if (ps2_send_command_with_response(PS2_CMD_SELF_TEST, &response) != 0 ||
        response != PS2_TEST_PASSED) {
        if (response != PS2_TEST_PASSED) {
            char buf[32];
            snprintf(buf, sizeof(buf), "PS/2: Self-test retry (0x%02x)\n", response);
            kprint(buf);
        } else {
            kprint("PS/2: Self-test retry after timeout\n");
        }

        if (ps2_send_command_with_response(PS2_CMD_SELF_TEST, &response) != 0) {
            kprint("PS/2: Self-test timeout\n");
            return -1;
        }

        if (response != PS2_TEST_PASSED) {
            char buf[32];
            snprintf(buf, sizeof(buf), "PS/2: Self-test failed (0x%02x)\n", response);
            kprint(buf);
            return -1;
        }
    }
    
    /* 
     * Note: Self-test may reset the controller, so we must restore config.
     * Re-write the configuration byte.
     */
    if (ps2_write_command(PS2_CMD_WRITE_CONFIG) || ps2_write_data(config)) {
        kprint("PS/2: Failed to restore config after self-test\n");
        return -1;
    }
    
    /* Step 5: Check for dual channel support */
    ps2_dual_channel = 0;
    
    /* Enable port 2 temporarily */
    ps2_write_command(PS2_CMD_ENABLE_P2);
    
    /* Read config - if bit 5 (P2 clock disable) is clear, port 2 exists */
    if (ps2_send_command_with_response(PS2_CMD_READ_CONFIG, &config) == 0) {
        if (!(config & PS2_CFG_SECOND_PORT_CLOCK)) {
            ps2_dual_channel = 1;
        }
    }
    
    /* Disable port 2 again for now */
    ps2_write_command(PS2_CMD_DISABLE_P2);
    
    /* Step 6: Interface tests */
    if (ps2_send_command_with_response(PS2_CMD_TEST_P1, &response)) {
        kprint("PS/2: Port 1 test timeout\n");
        return -1;
    }
    
    if (response != PS2_PORT_TEST_PASSED) {
        ps2_log_port_test_failure("Port 1", response);
        return -1;
    }
    
    if (ps2_dual_channel) {
        if (ps2_send_command_with_response(PS2_CMD_TEST_P2, &response) == 0) {
            if (response != PS2_PORT_TEST_PASSED) {
                ps2_log_port_test_failure("Port 2", response);
                ps2_dual_channel = 0;
            }
        } else {
            kprint("PS/2: Port 2 test timeout\n");
            ps2_dual_channel = 0;
        }
    }
    
    if (ps2_dual_channel) {
        kprint("PS/2: Dual channel detected\n");
    }

    /* Step 7: Enable devices */
    ps2_write_command(PS2_CMD_ENABLE_P1);
    
    if (ps2_dual_channel) {
        ps2_write_command(PS2_CMD_ENABLE_P2);
    }
    
    /* Step 8: Enable interrupts */
    if (ps2_send_command_with_response(PS2_CMD_READ_CONFIG, &config)) {
        kprint("PS/2: Failed to read config for IRQ enable\n");
        return -1;
    }
    
    config |= PS2_CFG_FIRST_PORT_INT;
    if (ps2_dual_channel) {
        config |= PS2_CFG_SECOND_PORT_INT;
    }
    
    if (ps2_write_command(PS2_CMD_WRITE_CONFIG) || ps2_write_data(config)) {
        kprint("PS/2: Failed to enable IRQs\n");
        return -1;
    }
    
    /* Step 9: Initialize mouse on port 2 if present */
    if (ps2_dual_channel) {
        ps2_mouse_setup();
    }

    kprint("PS/2: Controller initialized\n");
    return 0;
}

/*
 * Send a single byte to the mouse and wait for the standard ACK (0xFA).
 * Returns 0 on success, -1 on any timeout/NAK.
 */
static int ps2_aux_cmd(uint8_t cmd) {
    uint8_t ack;
    if (ps2_write_aux(cmd) != 0) return -1;
    if (ps2_read_data_timeout(&ack, PS2_MOUSE_TIMEOUT_LOOPS) != 0) return -1;
    return (ack == PS2_DEV_ACK) ? 0 : -1;
}

/*
 * Microsoft's "knock sequence" for entering an extended IntelliMouse mode.
 * The sequence is three consecutive Set Sample Rate (0xF3) commands with
 * specific values; the mouse interprets that triplet as a request to
 * switch personality.  After the knock, Read Device Type (0xF2) returns
 * the new ID byte.
 *
 *   200, 100, 80  → 0x03 IntelliMouse                       (3-button + wheel)
 *   200, 200, 80  → 0x04 IntelliMouse Explorer              (5-button + wheel)
 *
 * Explorer's knock is only honoured AFTER the IntelliMouse knock has
 * landed; we issue them in sequence.  A non-Microsoft mouse that
 * doesn't recognise either sequence simply stays in standard mode
 * and returns 0x00 from 0xF2 — which is exactly what we want.
 */
static int ps2_mouse_knock(uint8_t a, uint8_t b, uint8_t c) {
    if (ps2_aux_cmd(0xF3) != 0) return -1;
    if (ps2_aux_cmd(a)    != 0) return -1;
    if (ps2_aux_cmd(0xF3) != 0) return -1;
    if (ps2_aux_cmd(b)    != 0) return -1;
    if (ps2_aux_cmd(0xF3) != 0) return -1;
    if (ps2_aux_cmd(c)    != 0) return -1;
    return 0;
}

/* Send 0xF2 (Read Device Type), return the ID byte on success, -1 on failure. */
static int ps2_mouse_read_id(void) {
    uint8_t ack, id;
    if (ps2_write_aux(0xF2) != 0) return -1;
    if (ps2_read_data_timeout(&ack, PS2_MOUSE_TIMEOUT_LOOPS) != 0) return -1;
    if (ack != PS2_DEV_ACK) return -1;
    if (ps2_read_data_timeout(&id, PS2_MOUSE_TIMEOUT_LOOPS) != 0) return -1;
    return (int)id;
}

/*
 * Probe an attached PS/2 mouse, escalating through:
 *   plain → IntelliMouse (200/100/80) → Explorer (200/200/80)
 * and finally turn on data reporting.  Stores the resulting generation
 * in ps2_mouse_generation.
 */
void ps2_mouse_setup(void) {
    uint8_t byte;

    /* Reset.  Expect ACK + 0xAA self-test + initial ID. */
    if (ps2_write_aux(PS2_DEV_RESET) != 0) return;
    if (ps2_read_data_timeout(&byte, PS2_MOUSE_TIMEOUT_LOOPS) != 0 ||
        byte != PS2_DEV_ACK) return;
    if (ps2_read_data_timeout(&byte, PS2_MOUSE_TIMEOUT_LOOPS) != 0 ||
        byte != 0xAA) return;
    /* Initial device ID (post-reset).  Plain mice return 0x00 — note
     * that we will re-read it below after the knock. */
    ps2_read_data_timeout(&byte, PS2_TIMEOUT_LOOPS);

    ps2_mouse_generation = 0;

    /* Knock for IntelliMouse (wheel). */
    if (ps2_mouse_knock(200, 100, 80) == 0) {
        int id = ps2_mouse_read_id();
        if (id == 0x03) {
            ps2_mouse_generation = 3;
            kprint("PS/2: IntelliMouse (wheel) detected\n");

            /* Knock for IntelliMouse Explorer (5-button).  Only meaningful
             * once the mouse has already gone into IntelliMouse mode. */
            if (ps2_mouse_knock(200, 200, 80) == 0) {
                id = ps2_mouse_read_id();
                if (id == 0x04) {
                    ps2_mouse_generation = 4;
                    kprint("PS/2: IntelliMouse Explorer (5-button) detected\n");
                }
            }
        }
    }

    /* Enable data reporting. */
    if (ps2_aux_cmd(PS2_DEV_SCAN_ON) == 0) {
        kprint("PS/2: Mouse enabled\n");
    }
}
