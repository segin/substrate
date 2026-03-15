#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t mock_status_seq[128];
static size_t mock_status_len;
static size_t mock_status_pos;
static uint8_t mock_data_seq[128];
static size_t mock_data_len;
static size_t mock_data_pos;

typedef struct {
    uint16_t port;
    uint8_t value;
} io_write_t;

static io_write_t mock_writes[128];
static size_t mock_write_count;
static char mock_log[1024];

static void mock_reset(void) {
    memset(mock_status_seq, 0, sizeof(mock_status_seq));
    memset(mock_data_seq, 0, sizeof(mock_data_seq));
    memset(mock_writes, 0, sizeof(mock_writes));
    memset(mock_log, 0, sizeof(mock_log));
    mock_status_len = 0;
    mock_status_pos = 0;
    mock_data_len = 0;
    mock_data_pos = 0;
    mock_write_count = 0;
}

static void push_status(uint8_t value) {
    assert(mock_status_len < sizeof(mock_status_seq));
    mock_status_seq[mock_status_len++] = value;
}

static void push_data(uint8_t value) {
    assert(mock_data_len < sizeof(mock_data_seq));
    mock_data_seq[mock_data_len++] = value;
}

static uint8_t ps2_mock_inb(uint16_t port) {
    if (port == 0x64) {
        assert(mock_status_pos < mock_status_len);
        return mock_status_seq[mock_status_pos++];
    }
    if (port == 0x60) {
        assert(mock_data_pos < mock_data_len);
        return mock_data_seq[mock_data_pos++];
    }
    return 0xFF;
}

static void ps2_mock_outb(uint16_t port, uint8_t value) {
    assert(mock_write_count < (sizeof(mock_writes) / sizeof(mock_writes[0])));
    mock_writes[mock_write_count].port = port;
    mock_writes[mock_write_count].value = value;
    mock_write_count++;
}

void kprint(const char *str) {
    size_t used = strlen(mock_log);
    snprintf(mock_log + used, sizeof(mock_log) - used, "%s", str);
}

#define _IO_H
#define _KERN_CONSOLE_STUB_H
static inline uint8_t inb(uint16_t port) { return ps2_mock_inb(port); }
static inline void outb(uint16_t port, uint8_t value) { ps2_mock_outb(port, value); }
#include "../../sys/drivers/input/ps2.c"

static void assert_write(size_t idx, uint16_t port, uint8_t value) {
    assert(idx < mock_write_count);
    assert(mock_writes[idx].port == port);
    assert(mock_writes[idx].value == value);
}

static void test_io_primitives(void) {
    uint8_t data = 0;

    mock_reset();
    push_status(PS2_STATUS_INPUT_BUFFER_FULL);
    push_status(0);
    assert(ps2_wait_write() == 0);

    mock_reset();
    push_status(0);
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    assert(ps2_wait_read() == 0);

    mock_reset();
    push_status(0);
    assert(ps2_write_command(PS2_CMD_SELF_TEST) == 0);
    assert_write(0, PS2_COMMAND_PORT, PS2_CMD_SELF_TEST);

    mock_reset();
    push_status(0);
    assert(ps2_write_data(0x5A) == 0);
    assert_write(0, PS2_DATA_PORT, 0x5A);

    mock_reset();
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(0x42);
    assert(ps2_read_data() == 0x42);

    mock_reset();
    push_status(0);
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(0x77);
    assert(ps2_read_data_timeout(&data, 2) == 0);
    assert(data == 0x77);

    mock_reset();
    push_status(0);
    push_status(0);
    assert(ps2_write_aux(0xF4) == 0);
    assert_write(0, PS2_COMMAND_PORT, PS2_CMD_WRITE_P2_INPUT);
    assert_write(1, PS2_DATA_PORT, 0xF4);

    mock_reset();
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(0x11);
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(0x22);
    push_status(0);
    ps2_flush();
    assert(mock_data_pos == 2);

    mock_reset();
    push_status(0);
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(0x55);
    assert(ps2_send_command_with_response(PS2_CMD_SELF_TEST, &data) == 0);
    assert(data == 0x55);
    assert_write(0, PS2_COMMAND_PORT, PS2_CMD_SELF_TEST);
}

static void load_dual_channel_init_script(int retry_self_test, uint8_t p2_result) {
    push_status(0); /* disable p1 */
    push_status(0); /* disable p2 */
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL); /* flush */
    push_data(0x99);
    push_status(0); /* flush done */

    push_status(0); /* read config cmd */
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(0x00); /* config */

    push_status(0); /* write config cmd */
    push_status(0); /* write config data */

    push_status(0); /* self-test #1 cmd */
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(retry_self_test ? PS2_TEST_FAILED : PS2_TEST_PASSED);
    if (retry_self_test) {
        push_status(0); /* self-test #2 cmd */
        push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
        push_data(PS2_TEST_PASSED);
    }

    push_status(0); /* rewrite config cmd */
    push_status(0); /* rewrite config data */

    push_status(0); /* enable p2 */
    push_status(0); /* read config cmd */
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(0x00); /* dual channel present */
    push_status(0); /* disable p2 */

    push_status(0); /* test p1 cmd */
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(PS2_PORT_TEST_PASSED);

    push_status(0); /* test p2 cmd */
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(p2_result);

    push_status(0); /* enable p1 */
    if (p2_result == PS2_PORT_TEST_PASSED) {
        push_status(0); /* enable p2 */
    }

    push_status(0); /* read config cmd */
    push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
    push_data(0x00); /* config */
    push_status(0); /* write config cmd */
    push_status(0); /* write config data */

    if (p2_result == PS2_PORT_TEST_PASSED) {
        push_status(0); /* aux reset command */
        push_status(0); /* aux reset data */
        push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
        push_data(PS2_DEV_ACK);
        push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
        push_data(0xAA);
        push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
        push_data(0x00);
        push_status(0); /* aux scan-on command */
        push_status(0); /* aux scan-on data */
        push_status(PS2_STATUS_OUTPUT_BUFFER_FULL);
        push_data(PS2_DEV_ACK);
    }
}

static void test_init_retry_and_dual_channel(void) {
    mock_reset();
    load_dual_channel_init_script(1, PS2_PORT_TEST_PASSED);

    assert(ps2_init() == 0);
    assert(ps2_dual_channel == 1);
    assert(strstr(mock_log, "PS/2: Self-test retry") != NULL);
    assert(strstr(mock_log, "PS/2: Mouse enabled") != NULL);

    assert_write(0, PS2_COMMAND_PORT, PS2_CMD_DISABLE_P1);
    assert_write(1, PS2_COMMAND_PORT, PS2_CMD_DISABLE_P2);
    assert_write(2, PS2_COMMAND_PORT, PS2_CMD_READ_CONFIG);
    assert_write(3, PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG);
    assert_write(4, PS2_DATA_PORT, (uint8_t)(PS2_CFG_TRANSLATION | PS2_CFG_SYSTEM_FLAG));
    assert_write(5, PS2_COMMAND_PORT, PS2_CMD_SELF_TEST);
    assert_write(6, PS2_COMMAND_PORT, PS2_CMD_SELF_TEST);
    assert_write(7, PS2_COMMAND_PORT, PS2_CMD_WRITE_CONFIG);
    assert_write(8, PS2_DATA_PORT, (uint8_t)(PS2_CFG_TRANSLATION | PS2_CFG_SYSTEM_FLAG));
}

static void test_init_port2_failure_falls_back(void) {
    mock_reset();
    load_dual_channel_init_script(0, 0x01);

    assert(ps2_init() == 0);
    assert(ps2_dual_channel == 0);
    assert(strstr(mock_log, "PS/2: Port 2 test failed (0x01)") != NULL);
    assert(strstr(mock_log, "PS/2: Mouse enabled") == NULL);
}

int main(void) {
    test_io_primitives();
    test_init_retry_and_dual_channel();
    test_init_port2_failure_falls_back();
    puts("host_test_ps2: PASS");
    return 0;
}
