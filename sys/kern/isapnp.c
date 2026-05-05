#include <kern/isapnp.h>

#include <kern/resource.h>
#include <stdio.h>
#include <string.h>
#include <sys/errno.h>
#include <vm/vm_kmem.h>

#ifndef HOST_TEST
#include <arch/x86-common/io.h>
#include <kern/console.h>
#define isapnp_inb(port) inb(port)
#define isapnp_outb(port, value) outb((port), (value))
#else
#define isapnp_inb(port) ((void)(port), 0xffU)
#define isapnp_outb(port, value) ((void)(port), (void)(value))
#endif

#define ISAPNP_INDEX_PORT           0x279U
#define ISAPNP_WRITE_PORT           0xA79U
#define ISAPNP_REG_SET_RDP          0x00U
#define ISAPNP_REG_SERIAL_ISO       0x01U
#define ISAPNP_REG_CONFIG_CONTROL   0x02U
#define ISAPNP_REG_WAKE             0x03U
#define ISAPNP_REG_RESOURCE_DATA    0x04U
#define ISAPNP_REG_STATUS           0x05U
#define ISAPNP_REG_SET_CSN          0x06U
#define ISAPNP_REG_LOGDEV           0x07U

#define ISAPNP_CFG_ACTIVATE         0x30U
#define ISAPNP_CFG_MEM              0x40U
#define ISAPNP_CFG_PORT             0x60U
#define ISAPNP_CFG_IRQ              0x70U
#define ISAPNP_CFG_DMA              0x74U

#define ISAPNP_STAG_PNPVERNO        0x01U
#define ISAPNP_STAG_LOGDEVID        0x02U
#define ISAPNP_STAG_COMPATDEVID     0x03U
#define ISAPNP_STAG_IRQ             0x04U
#define ISAPNP_STAG_DMA             0x05U
#define ISAPNP_STAG_STARTDEP        0x06U
#define ISAPNP_STAG_ENDDEP          0x07U
#define ISAPNP_STAG_IOPORT          0x08U
#define ISAPNP_STAG_FIXEDIO         0x09U
#define ISAPNP_STAG_VENDOR          0x0EU
#define ISAPNP_STAG_END             0x0FU

#define ISAPNP_LTAG_MEMRANGE        0x81U
#define ISAPNP_LTAG_ANSISTR         0x82U
#define ISAPNP_LTAG_UNICODESTR      0x83U
#define ISAPNP_LTAG_VENDOR          0x84U
#define ISAPNP_LTAG_MEM32RANGE      0x85U
#define ISAPNP_LTAG_FIXEDMEM32      0x86U

#define ISAPNP_RDP_START            0x213U
#define ISAPNP_RDP_END              0x3FFU
#define ISAPNP_RDP_STEP             32U
#define ISAPNP_RAW_MAX              512U

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t offset;
} isapnp_stream_t;

static int isapnp_isolated_cards;

#ifdef HOST_TEST
typedef struct {
    int present;
    uint32_t card_id;
    uint32_t serial;
    uint8_t resource_data[ISAPNP_RAW_MAX];
    size_t resource_len;
    uint8_t active_mask;
    uint16_t assigned_port[ISAPNP_MAX_LOGICAL_DEVICES][ISAPNP_MAX_IO];
    uint8_t assigned_irq[ISAPNP_MAX_LOGICAL_DEVICES][ISAPNP_MAX_IRQ];
} isapnp_test_card_state_t;

static isapnp_test_card_state_t isapnp_test_cards[ISAPNP_MAX_CARDS];
static int isapnp_test_card_count;
#else
static int isapnp_rdp_port;
static int isapnp_rdp_reserved;
#endif

static uint16_t isapnp_decode_device(uint16_t encoded)
{
    return (uint16_t)(((encoded & 0x00FFU) << 8) | ((encoded & 0xFF00U) >> 8));
}

static uint16_t isapnp_low16(uint32_t value)
{
    return (uint16_t)(value & 0xFFFFU);
}

static uint16_t isapnp_high16(uint32_t value)
{
    return (uint16_t)((value >> 16) & 0xFFFFU);
}

void isapnp_eisa_id_to_string(uint32_t eisa_id, char out[8])
{
    uint16_t vendor = isapnp_low16(eisa_id);
    uint16_t device = isapnp_decode_device(isapnp_high16(eisa_id));
    unsigned a = (vendor >> 2) & 0x3FU;
    unsigned b = ((vendor & 0x03U) << 3) | ((vendor >> 13) & 0x07U);
    unsigned c = (vendor >> 8) & 0x1FU;

    if (out == NULL) {
        return;
    }

    out[0] = (char)((a ? a : 1U) + 'A' - 1);
    out[1] = (char)((b ? b : 1U) + 'A' - 1);
    out[2] = (char)((c ? c : 1U) + 'A' - 1);
    (void)snprintf(out + 3, 5, "%04X", device);
}

static void isapnp_zero_device(isapnp_device_t *dev)
{
    if (dev == NULL) {
        return;
    }
    memset(dev, 0, sizeof(*dev));
}

static int isapnp_stream_read(isapnp_stream_t *stream, uint8_t *out, size_t count)
{
    if (stream == NULL || out == NULL) {
        return -EINVAL;
    }
    if (stream->offset + count > stream->length) {
        return -EINVAL;
    }
    memcpy(out, stream->data + stream->offset, count);
    stream->offset += count;
    return 0;
}

static int isapnp_stream_skip(isapnp_stream_t *stream, size_t count)
{
    if (stream == NULL) {
        return -EINVAL;
    }
    if (stream->offset + count > stream->length) {
        return -EINVAL;
    }
    stream->offset += count;
    return 0;
}

static int isapnp_parse_tag(isapnp_stream_t *stream, uint8_t *tag, uint16_t *size)
{
    uint8_t head;

    if (stream == NULL || tag == NULL || size == NULL) {
        return -EINVAL;
    }
    if (isapnp_stream_read(stream, &head, 1) != 0) {
        return -EINVAL;
    }

    if (head == 0) {
        return -EINVAL;
    }

    if (head & 0x80U) {
        uint8_t bytes[2];

        if (isapnp_stream_read(stream, bytes, sizeof(bytes)) != 0) {
            return -EINVAL;
        }
        *tag = head;
        *size = (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
    } else {
        *tag = (uint8_t)((head >> 3) & 0x0FU);
        *size = (uint16_t)(head & 0x07U);
    }

    if (stream->offset + *size > stream->length) {
        return -EINVAL;
    }

    return 0;
}

static void isapnp_copy_name(char *dst, size_t dst_size, const uint8_t *src, size_t src_len)
{
    size_t count;

    if (dst == NULL || dst_size == 0 || src == NULL) {
        return;
    }

    count = src_len;
    if (count >= dst_size) {
        count = dst_size - 1;
    }
    memcpy(dst, src, count);
    dst[count] = '\0';
    while (count > 0 && dst[count - 1] == ' ') {
        dst[--count] = '\0';
    }
}

static unsigned isapnp_lowest_set_bit16(uint16_t mask)
{
    unsigned bit;

    for (bit = 0; bit < 16U; bit++) {
        if (mask & (uint16_t)(1U << bit)) {
            return bit;
        }
    }
    return 0xFFU;
}

static unsigned isapnp_lowest_set_bit8(uint8_t mask)
{
    unsigned bit;

    for (bit = 0; bit < 8U; bit++) {
        if (mask & (uint8_t)(1U << bit)) {
            return bit;
        }
    }
    return 0xFFU;
}

static int isapnp_parse_payload(isapnp_device_t *card, const uint8_t *data, size_t length)
{
    isapnp_stream_t stream;
    isapnp_logical_device_t *logical = NULL;
    int skip_dependent = 0;
    int saw_dependent = 0;

    if (card == NULL || data == NULL) {
        return -EINVAL;
    }

    stream.data = data;
    stream.length = length;
    stream.offset = 0;

    while (stream.offset < stream.length) {
        uint8_t tag;
        uint16_t size;
        const uint8_t *payload;

        if (isapnp_parse_tag(&stream, &tag, &size) != 0) {
            return -EINVAL;
        }
        payload = stream.data + stream.offset;

        if (tag == ISAPNP_STAG_LOGDEVID) {
            uint32_t eisa_id;

            if (size < 4 || card->logical_count >= ISAPNP_MAX_LOGICAL_DEVICES) {
                return -EINVAL;
            }
            logical = &card->logical[card->logical_count++];
            memset(logical, 0, sizeof(*logical));
            logical->logical_device = (uint8_t)(card->logical_count - 1U);
            logical->flags = size > 4 ? payload[4] : 0;
            eisa_id = (uint32_t)payload[0]
                    | ((uint32_t)payload[1] << 8)
                    | ((uint32_t)payload[2] << 16)
                    | ((uint32_t)payload[3] << 24);
            logical->eisa_id = eisa_id;
            logical->vendor_id = isapnp_low16(eisa_id);
            logical->device_id = isapnp_decode_device(isapnp_high16(eisa_id));
            isapnp_eisa_id_to_string(eisa_id, logical->id);
            strncpy(logical->name, logical->id, sizeof(logical->name) - 1);
            logical->name[sizeof(logical->name) - 1] = '\0';
            skip_dependent = 0;
            saw_dependent = 0;
            if (isapnp_stream_skip(&stream, size) != 0) {
                return -EINVAL;
            }
            continue;
        }

        if (tag == ISAPNP_STAG_END) {
            if (isapnp_stream_skip(&stream, size) != 0) {
                return -EINVAL;
            }
            break;
        }

        if (tag == ISAPNP_STAG_STARTDEP) {
            if (logical != NULL && saw_dependent) {
                skip_dependent = 1;
            }
            saw_dependent = 1;
            if (isapnp_stream_skip(&stream, size) != 0) {
                return -EINVAL;
            }
            continue;
        }

        if (tag == ISAPNP_STAG_ENDDEP) {
            skip_dependent = 0;
            if (isapnp_stream_skip(&stream, size) != 0) {
                return -EINVAL;
            }
            continue;
        }

        if (tag == ISAPNP_STAG_PNPVERNO) {
            if (size >= 2) {
                card->pnp_version = payload[0];
                card->product_version = payload[1];
            }
            if (isapnp_stream_skip(&stream, size) != 0) {
                return -EINVAL;
            }
            continue;
        }

        if (tag == ISAPNP_LTAG_ANSISTR) {
            if (logical != NULL && logical->name[0] == '\0') {
                isapnp_copy_name(logical->name, sizeof(logical->name), payload, size);
            } else if (logical != NULL && strcmp(logical->name, logical->id) == 0) {
                isapnp_copy_name(logical->name, sizeof(logical->name), payload, size);
            } else if (card->name[0] == '\0') {
                isapnp_copy_name(card->name, sizeof(card->name), payload, size);
            }
            if (isapnp_stream_skip(&stream, size) != 0) {
                return -EINVAL;
            }
            continue;
        }

        if (logical == NULL || skip_dependent) {
            if (isapnp_stream_skip(&stream, size) != 0) {
                return -EINVAL;
            }
            continue;
        }

        switch (tag) {
        case ISAPNP_STAG_COMPATDEVID:
            if (size == 4 && logical->compat_count < ISAPNP_MAX_COMPAT_IDS) {
                logical->compat_ids[logical->compat_count++] = (uint32_t)payload[0]
                                                          | ((uint32_t)payload[1] << 8)
                                                          | ((uint32_t)payload[2] << 16)
                                                          | ((uint32_t)payload[3] << 24);
            }
            break;

        case ISAPNP_STAG_IRQ:
            if ((size == 2 || size == 3) && logical->irq_count < ISAPNP_MAX_IRQ) {
                isapnp_irq_resource_t *irq = &logical->irq[logical->irq_count++];

                irq->mask = (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8));
                irq->flags = size > 2 ? payload[2] : 0;
                irq->irq = 0xFFU;
            }
            break;

        case ISAPNP_STAG_DMA:
            if (size == 2 && logical->dma_count < ISAPNP_MAX_DMA) {
                isapnp_dma_resource_t *dma = &logical->dma[logical->dma_count++];

                dma->mask = payload[0];
                dma->flags = payload[1];
                dma->channel = 0xFFU;
            }
            break;

        case ISAPNP_STAG_IOPORT:
            if (size == 7 && logical->io_count < ISAPNP_MAX_IO) {
                isapnp_io_resource_t *io = &logical->io[logical->io_count++];

                io->flags = payload[0];
                io->min_base = (uint16_t)(payload[1] | ((uint16_t)payload[2] << 8));
                io->max_base = (uint16_t)(payload[3] | ((uint16_t)payload[4] << 8));
                io->align = payload[5];
                io->length = payload[6];
                io->base = 0;
            }
            break;

        case ISAPNP_STAG_FIXEDIO:
            if (size == 3 && logical->io_count < ISAPNP_MAX_IO) {
                isapnp_io_resource_t *io = &logical->io[logical->io_count++];
                uint16_t base = (uint16_t)(payload[0] | ((uint16_t)payload[1] << 8));

                io->flags = 0;
                io->min_base = base;
                io->max_base = base;
                io->base = base;
                io->align = 0;
                io->length = payload[2];
            }
            break;

        case ISAPNP_LTAG_MEMRANGE:
            if (size == 9 && logical->mem_count < ISAPNP_MAX_MEM) {
                isapnp_mem_resource_t *mem = &logical->mem[logical->mem_count++];

                mem->flags = payload[0];
                mem->min_base = (uint32_t)(((uint32_t)payload[1] | ((uint32_t)payload[2] << 8)) << 8);
                mem->max_base = (uint32_t)(((uint32_t)payload[3] | ((uint32_t)payload[4] << 8)) << 8);
                mem->align = (uint32_t)((uint32_t)payload[5] | ((uint32_t)payload[6] << 8));
                mem->length = (uint32_t)(((uint32_t)payload[7] | ((uint32_t)payload[8] << 8)) << 8);
                mem->base = 0;
            }
            break;

        case ISAPNP_LTAG_MEM32RANGE:
            if (size == 17 && logical->mem_count < ISAPNP_MAX_MEM) {
                isapnp_mem_resource_t *mem = &logical->mem[logical->mem_count++];

                mem->flags = payload[0];
                mem->min_base = (uint32_t)payload[1]
                              | ((uint32_t)payload[2] << 8)
                              | ((uint32_t)payload[3] << 16)
                              | ((uint32_t)payload[4] << 24);
                mem->max_base = (uint32_t)payload[5]
                              | ((uint32_t)payload[6] << 8)
                              | ((uint32_t)payload[7] << 16)
                              | ((uint32_t)payload[8] << 24);
                mem->align = (uint32_t)payload[9]
                           | ((uint32_t)payload[10] << 8)
                           | ((uint32_t)payload[11] << 16)
                           | ((uint32_t)payload[12] << 24);
                mem->length = (uint32_t)payload[13]
                            | ((uint32_t)payload[14] << 8)
                            | ((uint32_t)payload[15] << 16)
                            | ((uint32_t)payload[16] << 24);
                mem->base = 0;
            }
            break;

        case ISAPNP_LTAG_FIXEDMEM32:
            if (size == 9 && logical->mem_count < ISAPNP_MAX_MEM) {
                isapnp_mem_resource_t *mem = &logical->mem[logical->mem_count++];

                mem->flags = payload[0];
                mem->base = (uint32_t)payload[1]
                          | ((uint32_t)payload[2] << 8)
                          | ((uint32_t)payload[3] << 16)
                          | ((uint32_t)payload[4] << 24);
                mem->min_base = mem->base;
                mem->max_base = mem->base;
                mem->align = 0;
                mem->length = (uint32_t)payload[5]
                            | ((uint32_t)payload[6] << 8)
                            | ((uint32_t)payload[7] << 16)
                            | ((uint32_t)payload[8] << 24);
            }
            break;

        default:
            break;
        }

        if (isapnp_stream_skip(&stream, size) != 0) {
            return -EINVAL;
        }
    }

    if (card->name[0] == '\0') {
        strncpy(card->name, card->id, sizeof(card->name) - 1);
        card->name[sizeof(card->name) - 1] = '\0';
    }
    return 0;
}

static uint16_t isapnp_choose_io_base(isapnp_io_resource_t *io)
{
    uint32_t base;
    uint32_t end;
    uint32_t step;

    if (io == NULL || io->length == 0) {
        return 0;
    }

    step = io->align ? io->align : 1U;
    if (io->base != 0) {
        if (request_region(io->base, io->length, "isapnp") == NULL) {
            return 0;
        }
        return io->base;
    }

    end = io->max_base;
    for (base = io->min_base; base <= end; base += step) {
        if (request_region(base, io->length, "isapnp") != NULL) {
            return (uint16_t)base;
        }
        if (base + step < base) {
            break;
        }
    }

    return 0;
}

static uint32_t isapnp_choose_mem_base(isapnp_mem_resource_t *mem)
{
    uint64_t base;
    uint64_t end;
    uint64_t step;

    if (mem == NULL || mem->length == 0) {
        return 0;
    }

    step = mem->align ? mem->align : 1U;
    if (mem->base != 0) {
        if (request_mem_region(mem->base, mem->length, "isapnp") == NULL) {
            return 0;
        }
        return mem->base;
    }

    end = mem->max_base;
    for (base = mem->min_base; base <= end; base += step) {
        if (request_mem_region(base, mem->length, "isapnp") != NULL) {
            return (uint32_t)base;
        }
        if (base + step < base) {
            break;
        }
    }

    return 0;
}

static void isapnp_release_logical(isapnp_logical_device_t *logical)
{
    unsigned i;

    if (logical == NULL) {
        return;
    }

    for (i = 0; i < logical->io_count; i++) {
        if (logical->io[i].base != 0 && logical->io[i].length != 0) {
            release_region(logical->io[i].base, logical->io[i].length);
        }
    }
    for (i = 0; i < logical->mem_count; i++) {
        if (logical->mem[i].base != 0 && logical->mem[i].length != 0) {
            release_mem_region(logical->mem[i].base, logical->mem[i].length);
        }
    }
    logical->active = 0;
}

static void isapnp_release_card_prefix(isapnp_device_t *dev, unsigned count)
{
    unsigned i;

    if (dev == NULL) {
        return;
    }

    if (count > dev->logical_count) {
        count = dev->logical_count;
    }
    for (i = 0; i < count; i++) {
        isapnp_release_logical(&dev->logical[i]);
    }
}

#ifndef HOST_TEST
static void isapnp_delay(unsigned iterations)
{
    volatile unsigned i;

    for (i = 0; i < iterations; i++) {
        __asm__ __volatile__("pause");
    }
}

static void isapnp_write_address(uint8_t reg)
{
    isapnp_outb(ISAPNP_INDEX_PORT, reg);
    isapnp_delay(256);
}

static void isapnp_write_data(uint8_t value)
{
    isapnp_outb(ISAPNP_WRITE_PORT, value);
}

static uint8_t isapnp_read_data_port(void)
{
    return isapnp_inb((uint16_t)isapnp_rdp_port);
}

static uint8_t isapnp_read_config(uint8_t reg)
{
    isapnp_write_address(reg);
    return isapnp_read_data_port();
}

static void isapnp_write_config(uint8_t reg, uint8_t value)
{
    isapnp_write_address(reg);
    isapnp_write_data(value);
}

static void isapnp_write_word(uint8_t reg, uint16_t value)
{
    isapnp_write_config(reg, (uint8_t)(value >> 8));
    isapnp_write_config((uint8_t)(reg + 1U), (uint8_t)value);
}

static void isapnp_send_key(void)
{
    uint8_t code = 0x6AU;
    unsigned i;

    isapnp_delay(4096);
    isapnp_write_address(0x00);
    isapnp_write_address(0x00);
    isapnp_write_address(code);
    for (i = 1; i < 32U; i++) {
        uint8_t feedback = (uint8_t)(((code & 0x01U) ^ ((code & 0x02U) >> 1)) << 7);
        code = (uint8_t)((code >> 1) | feedback);
        isapnp_write_address(code);
    }
}

static void isapnp_wait_for_key(void)
{
    isapnp_write_config(ISAPNP_REG_CONFIG_CONTROL, 0x02U);
}

static void isapnp_wake(uint8_t csn)
{
    isapnp_write_config(ISAPNP_REG_WAKE, csn);
}

static void isapnp_select_logical(uint8_t logical_device)
{
    isapnp_write_config(ISAPNP_REG_LOGDEV, logical_device);
}

static int isapnp_next_rdp_port(void)
{
    int port;

    if (isapnp_rdp_reserved != 0) {
        release_region((uint16_t)isapnp_rdp_reserved, 1);
        isapnp_rdp_reserved = 0;
    }

    for (port = isapnp_rdp_port ? isapnp_rdp_port : (int)ISAPNP_RDP_START;
         port <= (int)ISAPNP_RDP_END;
         port += (int)ISAPNP_RDP_STEP) {
        if (port >= 0x280 && port <= 0x380) {
            continue;
        }
        if (request_region((uint16_t)port, 1, "isapnp-rdp") != NULL) {
            isapnp_rdp_port = port;
            isapnp_rdp_reserved = port;
            return 0;
        }
    }

    return -EBUSY;
}

static void isapnp_set_rdp(void)
{
    isapnp_write_config(ISAPNP_REG_SET_RDP, (uint8_t)(isapnp_rdp_port >> 2));
    isapnp_delay(1024);
}

static int isapnp_prepare_isolation(void)
{
    isapnp_wait_for_key();
    isapnp_send_key();
    isapnp_write_config(ISAPNP_REG_CONFIG_CONTROL, 0x05U);
    isapnp_delay(8192);
    isapnp_wait_for_key();
    isapnp_send_key();
    isapnp_wake(0);
    if (isapnp_next_rdp_port() != 0) {
        isapnp_wait_for_key();
        return -EBUSY;
    }
    isapnp_set_rdp();
    isapnp_delay(8192);
    isapnp_write_address(ISAPNP_REG_SERIAL_ISO);
    isapnp_delay(8192);
    return 0;
}

static int isapnp_peek_bytes(uint8_t *buf, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++) {
        unsigned spins;
        uint8_t status = 0;

        for (spins = 0; spins < 20U; spins++) {
            status = isapnp_read_config(ISAPNP_REG_STATUS);
            if (status & 0x01U) {
                break;
            }
            isapnp_delay(1024);
        }
        if ((status & 0x01U) == 0) {
            return -EIO;
        }
        if (buf != NULL) {
            buf[i] = isapnp_read_config(ISAPNP_REG_RESOURCE_DATA);
        } else {
            (void)isapnp_read_config(ISAPNP_REG_RESOURCE_DATA);
        }
    }

    return 0;
}

static int isapnp_fetch_stream(uint8_t csn, uint8_t header[9], uint8_t *payload, size_t payload_size, size_t *payload_len)
{
    size_t off = 0;

    if (header == NULL || payload == NULL || payload_len == NULL) {
        return -EINVAL;
    }

    isapnp_wait_for_key();
    isapnp_send_key();
    isapnp_wake(csn);
    if (isapnp_peek_bytes(header, 9) != 0) {
        isapnp_wait_for_key();
        return -EIO;
    }

    while (off < payload_size) {
        uint8_t tag;

        if (isapnp_peek_bytes(&tag, 1) != 0) {
            isapnp_wait_for_key();
            return -EIO;
        }
        payload[off++] = tag;

        if (tag & 0x80U) {
            uint8_t size_bytes[2];
            uint16_t size;

            if (off + 2 > payload_size) {
                isapnp_wait_for_key();
                return -ENOSPC;
            }
            if (isapnp_peek_bytes(size_bytes, 2) != 0) {
                isapnp_wait_for_key();
                return -EIO;
            }
            payload[off++] = size_bytes[0];
            payload[off++] = size_bytes[1];
            size = (uint16_t)(size_bytes[0] | ((uint16_t)size_bytes[1] << 8));
            if (off + size > payload_size) {
                isapnp_wait_for_key();
                return -ENOSPC;
            }
            if (isapnp_peek_bytes(payload + off, size) != 0) {
                isapnp_wait_for_key();
                return -EIO;
            }
            off += size;
        } else {
            uint16_t size = (uint16_t)(tag & 0x07U);
            if (off + size > payload_size) {
                isapnp_wait_for_key();
                return -ENOSPC;
            }
            if (isapnp_peek_bytes(payload + off, size) != 0) {
                isapnp_wait_for_key();
                return -EIO;
            }
            off += size;
            if (((tag >> 3) & 0x0FU) == ISAPNP_STAG_END) {
                break;
            }
        }
    }

    isapnp_wait_for_key();
    *payload_len = off;
    return 0;
}

static int isapnp_begin_config(uint8_t csn, int logical_device)
{
    if (csn == 0 || logical_device >= ISAPNP_MAX_LOGICAL_DEVICES) {
        return -EINVAL;
    }

    isapnp_wait_for_key();
    isapnp_send_key();
    isapnp_wake(csn);
    if (logical_device >= 0) {
        isapnp_select_logical((uint8_t)logical_device);
    }
    return 0;
}

static void isapnp_end_config(void)
{
    isapnp_wait_for_key();
}
#endif

void isapnp_init(void)
{
    isapnp_isolated_cards = 0;
#ifndef HOST_TEST
    isapnp_rdp_port = (int)ISAPNP_RDP_START;
    isapnp_rdp_reserved = 0;
#endif
}

int isapnp_isolate(void)
{
#ifdef HOST_TEST
    isapnp_isolated_cards = isapnp_test_card_count;
    return isapnp_isolated_cards;
#else
    uint8_t running_checksum = 0x6AU;
    uint8_t observed_checksum = 0;
    int cards = 0;
    int attempt = 1;

    isapnp_isolated_cards = 0;
    isapnp_rdp_port = (int)ISAPNP_RDP_START;
    if (isapnp_prepare_isolation() != 0) {
        return -1;
    }

    for (;;) {
        int bit_index;

        for (bit_index = 1; bit_index <= 64; bit_index++) {
            uint16_t pair = (uint16_t)(isapnp_read_data_port() << 8);
            uint8_t bit = 0;

            isapnp_delay(2048);
            pair = (uint16_t)(pair | isapnp_read_data_port());
            isapnp_delay(2048);
            if (pair == 0x55AAU) {
                bit = 1U;
            }
            running_checksum = (uint8_t)((((((running_checksum ^ (running_checksum >> 1)) & 0x01U) ^ bit) << 7)
                                         | (running_checksum >> 1)) & 0xFFU);
        }

        observed_checksum = 0;
        for (bit_index = 0; bit_index < 8; bit_index++) {
            uint16_t pair = (uint16_t)(isapnp_read_data_port() << 8);

            isapnp_delay(2048);
            pair = (uint16_t)(pair | isapnp_read_data_port());
            isapnp_delay(2048);
            if (pair == 0x55AAU) {
                observed_checksum |= (uint8_t)(1U << bit_index);
            }
        }

        if (running_checksum != 0 && running_checksum == observed_checksum) {
            cards++;
            isapnp_write_config(ISAPNP_REG_SET_CSN, (uint8_t)cards);
            isapnp_delay(2048);
            attempt++;
            isapnp_wake(0);
            isapnp_set_rdp();
            isapnp_delay(4096);
            isapnp_write_address(ISAPNP_REG_SERIAL_ISO);
            isapnp_delay(4096);
            if (cards == 255) {
                break;
            }
            running_checksum = 0x6AU;
            continue;
        }

        if (attempt == 1) {
            isapnp_rdp_port += (int)ISAPNP_RDP_STEP;
            if (isapnp_prepare_isolation() != 0) {
                return -1;
            }
        } else {
            break;
        }
        running_checksum = 0x6AU;
        observed_checksum = 0;
    }

    isapnp_wait_for_key();
    isapnp_isolated_cards = cards;
    return cards;
#endif
}

int isapnp_read_resources(uint8_t csn, isapnp_device_t *dev)
{
    uint8_t header[9];
    uint8_t raw[ISAPNP_RAW_MAX];
    size_t raw_len = 0;
    int rc;

    if (csn == 0 || dev == NULL) {
        return -EINVAL;
    }

    isapnp_zero_device(dev);
    dev->csn = csn;

#ifdef HOST_TEST
    if (csn > (uint8_t)isapnp_test_card_count || !isapnp_test_cards[csn - 1U].present) {
        return -ENODEV;
    }
    header[0] = (uint8_t)(isapnp_test_cards[csn - 1U].card_id & 0xFFU);
    header[1] = (uint8_t)((isapnp_test_cards[csn - 1U].card_id >> 8) & 0xFFU);
    header[2] = (uint8_t)((isapnp_test_cards[csn - 1U].card_id >> 16) & 0xFFU);
    header[3] = (uint8_t)((isapnp_test_cards[csn - 1U].card_id >> 24) & 0xFFU);
    header[4] = (uint8_t)(isapnp_test_cards[csn - 1U].serial & 0xFFU);
    header[5] = (uint8_t)((isapnp_test_cards[csn - 1U].serial >> 8) & 0xFFU);
    header[6] = (uint8_t)((isapnp_test_cards[csn - 1U].serial >> 16) & 0xFFU);
    header[7] = (uint8_t)((isapnp_test_cards[csn - 1U].serial >> 24) & 0xFFU);
    header[8] = 0;
    raw_len = isapnp_test_cards[csn - 1U].resource_len;
    if (raw_len > sizeof(raw)) {
        return -ENOSPC;
    }
    memcpy(raw, isapnp_test_cards[csn - 1U].resource_data, raw_len);
#else
    rc = isapnp_fetch_stream(csn, header, raw, sizeof(raw), &raw_len);
    if (rc != 0) {
        return rc;
    }
#endif

    dev->eisa_id = (uint32_t)header[0]
                 | ((uint32_t)header[1] << 8)
                 | ((uint32_t)header[2] << 16)
                 | ((uint32_t)header[3] << 24);
    dev->vendor_id = isapnp_low16(dev->eisa_id);
    dev->device_id = isapnp_decode_device(isapnp_high16(dev->eisa_id));
    dev->serial = (uint32_t)header[4]
                | ((uint32_t)header[5] << 8)
                | ((uint32_t)header[6] << 16)
                | ((uint32_t)header[7] << 24);
    dev->serial_checksum = header[8];
    isapnp_eisa_id_to_string(dev->eisa_id, dev->id);
    snprintf(dev->name, sizeof(dev->name), "%s", dev->id);

    rc = isapnp_parse_payload(dev, raw, raw_len);
    if (rc != 0) {
        isapnp_zero_device(dev);
        return rc;
    }
    return 0;
}

int isapnp_activate(isapnp_device_t *dev)
{
    unsigned i;

    if (dev == NULL) {
        return -EINVAL;
    }

    for (i = 0; i < dev->logical_count; i++) {
        isapnp_logical_device_t *logical = &dev->logical[i];
        unsigned idx;

        for (idx = 0; idx < logical->io_count; idx++) {
            if (logical->io[idx].base == 0) {
                logical->io[idx].base = isapnp_choose_io_base(&logical->io[idx]);
            } else if (request_region(logical->io[idx].base, logical->io[idx].length, "isapnp") == NULL) {
                logical->io[idx].base = 0;
            }
            if (logical->io[idx].length != 0 && logical->io[idx].base == 0) {
                isapnp_release_card_prefix(dev, i + 1);
                return -EBUSY;
            }
        }

        for (idx = 0; idx < logical->mem_count; idx++) {
            if (logical->mem[idx].base == 0) {
                logical->mem[idx].base = isapnp_choose_mem_base(&logical->mem[idx]);
            } else if (request_mem_region(logical->mem[idx].base, logical->mem[idx].length, "isapnp") == NULL) {
                logical->mem[idx].base = 0;
            }
            if (logical->mem[idx].length != 0 && logical->mem[idx].base == 0) {
                isapnp_release_card_prefix(dev, i + 1);
                return -EBUSY;
            }
        }

        for (idx = 0; idx < logical->irq_count; idx++) {
            if (logical->irq[idx].irq == 0xFFU) {
                logical->irq[idx].irq = (uint8_t)isapnp_lowest_set_bit16(logical->irq[idx].mask);
            }
            if (logical->irq[idx].mask != 0 && logical->irq[idx].irq == 0xFFU) {
                isapnp_release_card_prefix(dev, i + 1);
                return -EBUSY;
            }
        }

        for (idx = 0; idx < logical->dma_count; idx++) {
            if (logical->dma[idx].channel == 0xFFU) {
                logical->dma[idx].channel = (uint8_t)isapnp_lowest_set_bit8(logical->dma[idx].mask);
            }
            if (logical->dma[idx].mask != 0 && logical->dma[idx].channel == 0xFFU) {
                isapnp_release_card_prefix(dev, i + 1);
                return -EBUSY;
            }
        }

#ifdef HOST_TEST
        isapnp_test_cards[dev->csn - 1U].active_mask |= (uint8_t)(1U << logical->logical_device);
        for (idx = 0; idx < logical->io_count; idx++) {
            isapnp_test_cards[dev->csn - 1U].assigned_port[logical->logical_device][idx] = logical->io[idx].base;
        }
        for (idx = 0; idx < logical->irq_count; idx++) {
            isapnp_test_cards[dev->csn - 1U].assigned_irq[logical->logical_device][idx] = logical->irq[idx].irq;
        }
#else
        if (isapnp_begin_config(dev->csn, logical->logical_device) != 0) {
            isapnp_release_card_prefix(dev, i + 1);
            return -EIO;
        }
        for (idx = 0; idx < logical->io_count; idx++) {
            isapnp_write_word((uint8_t)(ISAPNP_CFG_PORT + (idx << 1)), logical->io[idx].base);
        }
        for (idx = 0; idx < logical->irq_count; idx++) {
            isapnp_write_config((uint8_t)(ISAPNP_CFG_IRQ + (idx << 1)), logical->irq[idx].irq == 2U ? 9U : logical->irq[idx].irq);
        }
        for (idx = 0; idx < logical->dma_count; idx++) {
            isapnp_write_config((uint8_t)(ISAPNP_CFG_DMA + idx), logical->dma[idx].channel);
        }
        for (idx = 0; idx < logical->mem_count; idx++) {
            isapnp_write_word((uint8_t)(ISAPNP_CFG_MEM + (idx << 3)), (uint16_t)((logical->mem[idx].base >> 8) & 0xFFFFU));
        }
        isapnp_write_config(ISAPNP_CFG_ACTIVATE, 1);
        isapnp_end_config();
#endif
        logical->active = 1;
    }

    return 0;
}

#ifdef HOST_TEST
void isapnp_test_reset(void)
{
    memset(isapnp_test_cards, 0, sizeof(isapnp_test_cards));
    isapnp_test_card_count = 0;
    isapnp_isolated_cards = 0;
}

int isapnp_test_add_card(const isapnp_test_card_t *card)
{
    isapnp_test_card_state_t *dst;

    if (card == NULL || card->resource_data == NULL || card->resource_len > ISAPNP_RAW_MAX) {
        return -EINVAL;
    }
    if (isapnp_test_card_count >= ISAPNP_MAX_CARDS) {
        return -ENOSPC;
    }

    dst = &isapnp_test_cards[isapnp_test_card_count++];
    memset(dst, 0, sizeof(*dst));
    dst->present = 1;
    dst->card_id = card->card_id;
    dst->serial = card->serial;
    dst->resource_len = card->resource_len;
    memcpy(dst->resource_data, card->resource_data, card->resource_len);
    return 0;
}

int isapnp_test_logical_active(uint8_t csn, uint8_t logical_device)
{
    if (csn == 0 || csn > (uint8_t)isapnp_test_card_count || logical_device >= ISAPNP_MAX_LOGICAL_DEVICES) {
        return 0;
    }
    return (isapnp_test_cards[csn - 1U].active_mask & (uint8_t)(1U << logical_device)) != 0;
}

uint16_t isapnp_test_logical_port(uint8_t csn, uint8_t logical_device, unsigned index)
{
    if (csn == 0 || csn > (uint8_t)isapnp_test_card_count || logical_device >= ISAPNP_MAX_LOGICAL_DEVICES || index >= ISAPNP_MAX_IO) {
        return 0;
    }
    return isapnp_test_cards[csn - 1U].assigned_port[logical_device][index];
}

uint8_t isapnp_test_logical_irq(uint8_t csn, uint8_t logical_device, unsigned index)
{
    if (csn == 0 || csn > (uint8_t)isapnp_test_card_count || logical_device >= ISAPNP_MAX_LOGICAL_DEVICES || index >= ISAPNP_MAX_IRQ) {
        return 0xFFU;
    }
    return isapnp_test_cards[csn - 1U].assigned_irq[logical_device][index];
}
#endif