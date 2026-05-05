#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kern/isapnp.h>
#include <kern/resource.h>

void *kmalloc(size_t size) { return calloc(1, size); }
void kfree(void *ptr, size_t size) { (void)size; free(ptr); }

#include "../../sys/kern/resource.c"
#include "../../sys/kern/isapnp.c"

static void append_u32le(uint8_t *buf, size_t *off, uint32_t value)
{
    buf[(*off)++] = (uint8_t)(value & 0xFFU);
    buf[(*off)++] = (uint8_t)((value >> 8) & 0xFFU);
    buf[(*off)++] = (uint8_t)((value >> 16) & 0xFFU);
    buf[(*off)++] = (uint8_t)((value >> 24) & 0xFFU);
}

static size_t build_multiio_resources(uint8_t *buf, size_t cap)
{
    size_t off = 0;
    uint32_t serial_id = ISAPNP_EISA_ID('P', 'N', 'P', 0x0501);
    uint32_t lpt_id = ISAPNP_EISA_ID('P', 'N', 'P', 0x0400);
    const char uart_name[] = "PnP UART";
    const char lpt_name[] = "PnP LPT";

    (void)cap;
    buf[off++] = 0x0A;
    buf[off++] = 0x10;
    buf[off++] = 0x01;

    buf[off++] = 0x15;
    append_u32le(buf, &off, serial_id);
    buf[off++] = 0x00;

    buf[off++] = 0x82;
    buf[off++] = sizeof(uart_name) - 1;
    buf[off++] = 0x00;
    memcpy(buf + off, uart_name, sizeof(uart_name) - 1);
    off += sizeof(uart_name) - 1;

    buf[off++] = 0x47;
    buf[off++] = 0x00;
    buf[off++] = 0xE8;
    buf[off++] = 0x02;
    buf[off++] = 0xE8;
    buf[off++] = 0x02;
    buf[off++] = 0x08;
    buf[off++] = 0x08;

    buf[off++] = 0x23;
    buf[off++] = 0x08;
    buf[off++] = 0x00;
    buf[off++] = 0x00;

    buf[off++] = 0x15;
    append_u32le(buf, &off, lpt_id);
    buf[off++] = 0x00;

    buf[off++] = 0x82;
    buf[off++] = sizeof(lpt_name) - 1;
    buf[off++] = 0x00;
    memcpy(buf + off, lpt_name, sizeof(lpt_name) - 1);
    off += sizeof(lpt_name) - 1;

    buf[off++] = 0x4B;
    buf[off++] = 0x78;
    buf[off++] = 0x02;
    buf[off++] = 0x08;

    buf[off++] = 0x23;
    buf[off++] = 0x80;
    buf[off++] = 0x00;
    buf[off++] = 0x00;

    buf[off++] = 0x79;
    buf[off++] = 0x00;
    return off;
}

static void reset_fixture(void)
{
    resource_init();
    isapnp_test_reset();
    isapnp_init();
}

static void test_isolate_assigns_csns(void)
{
    uint8_t raw[64];
    isapnp_test_card_t card;

    reset_fixture();
    memset(raw, 0, sizeof(raw));
    raw[0] = 0x79;
    raw[1] = 0x00;

    card.card_id = ISAPNP_EISA_ID('P', 'N', 'P', 0x0A03);
    card.serial = 0x10203040U;
    card.resource_data = raw;
    card.resource_len = 2;
    assert(isapnp_test_add_card(&card) == 0);
    card.serial = 0x55667788U;
    assert(isapnp_test_add_card(&card) == 0);

    assert(isapnp_isolate() == 2);
}

static void test_read_resources_parses_logical_devices(void)
{
    uint8_t raw[128];
    isapnp_test_card_t card_desc;
    isapnp_device_t card;

    reset_fixture();
    memset(raw, 0, sizeof(raw));
    card_desc.card_id = ISAPNP_EISA_ID('P', 'N', 'P', 0x0A03);
    card_desc.serial = 0x11223344U;
    card_desc.resource_len = build_multiio_resources(raw, sizeof(raw));
    card_desc.resource_data = raw;
    assert(isapnp_test_add_card(&card_desc) == 0);

    assert(isapnp_isolate() == 1);
    assert(isapnp_read_resources(1, &card) == 0);
    assert(card.logical_count == 2);
    assert(strcmp(card.logical[0].id, "PNP0501") == 0);
    assert(strcmp(card.logical[1].id, "PNP0400") == 0);
    assert(card.logical[0].io_count == 1);
    assert(card.logical[0].irq_count == 1);
    assert(card.logical[0].io[0].min_base == 0x02E8);
    assert(card.logical[1].io[0].base == 0x0278);
}

static void test_activate_assigns_and_reserves_resources(void)
{
    uint8_t raw[128];
    isapnp_test_card_t card_desc;
    isapnp_device_t card;

    reset_fixture();
    memset(raw, 0, sizeof(raw));
    card_desc.card_id = ISAPNP_EISA_ID('P', 'N', 'P', 0x0A03);
    card_desc.serial = 0xAABBCCDDU;
    card_desc.resource_len = build_multiio_resources(raw, sizeof(raw));
    card_desc.resource_data = raw;
    assert(isapnp_test_add_card(&card_desc) == 0);

    assert(isapnp_isolate() == 1);
    assert(isapnp_read_resources(1, &card) == 0);
    assert(isapnp_activate(&card) == 0);
    assert(isapnp_test_logical_active(1, 0));
    assert(isapnp_test_logical_active(1, 1));
    assert(isapnp_test_logical_port(1, 0, 0) == 0x02E8);
    assert(isapnp_test_logical_port(1, 1, 0) == 0x0278);
    assert(isapnp_test_logical_irq(1, 0, 0) == 3);
    assert(isapnp_test_logical_irq(1, 1, 0) == 7);
    assert(resource_find(RES_IO, 0x02E8, 8) != NULL);
    assert(resource_find(RES_IO, 0x0278, 8) != NULL);
}

int main(void)
{
    test_isolate_assigns_csns();
    test_read_resources_parses_logical_devices();
    test_activate_assigns_and_reserves_resources();
    puts("host_test_isapnp: PASS");
    return 0;
}