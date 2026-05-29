#include <drivers/input/mouse.h>
#include <drivers/input/ps2.h>
#include <arch/i386/cpu.h>
#include <arch/x86-common/io.h>
#include <kern/console.h>
#include <sys/input.h>
#include <sys/random.h>

static uint8_t mouse_buttons = 0;

#define MOUSE_QUEUE_SIZE 64
static mouse_event_t mouse_queue[MOUSE_QUEUE_SIZE];
static int mouse_q_head = 0;
static int mouse_q_tail = 0;

static void mouse_q_push(int32_t dx, int32_t dy, int32_t wheel, uint8_t buttons) {
    int next = (mouse_q_head + 1) % MOUSE_QUEUE_SIZE;
    if (next != mouse_q_tail) {
        mouse_queue[mouse_q_head].dx = dx;
        mouse_queue[mouse_q_head].dy = dy;
        mouse_queue[mouse_q_head].wheel = wheel;
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
static int8_t  mouse_byte[4];   /* byte 3 used only on IntelliMouse / Explorer */
static int32_t mouse_x = 0;
static int32_t mouse_y = 0;

static int32_t mouse_clamp_delta(int32_t delta, uint8_t overflow, uint8_t negative) {
    if (!overflow) {
        return delta;
    }
    return negative ? -255 : 255;
}

void mouse_handler(registers_t *regs) {
    uint8_t status = inb(PS2_STATUS_PORT);
    if (status & 1) {
        uint8_t data = inb(PS2_DATA_PORT);

        /* Harvest entropy from mouse event timing */
        uint32_t entropy_data[2];
        i386_cpu_cycle_counter_split(&entropy_data[0], &entropy_data[1]);
        entropy_data[1] ^= data; /* Mix in data byte */
        random_harvest_fast(entropy_data, sizeof(entropy_data));

        int gen = ps2_mouse_get_generation();
        int packet_len = (gen == 3 || gen == 4) ? 4 : 3;

        switch (mouse_cycle) {
            case 0:
                mouse_byte[0] = data;
                /* Bit 3 of byte 0 is always 1 on a valid packet header. */
                if (data & 0x08) mouse_cycle++;
                break;
            case 1:
                mouse_byte[1] = data;
                mouse_cycle++;
                break;
            case 2:
                mouse_byte[2] = data;
                if (packet_len == 3) {
                    mouse_cycle = 0;
                    goto deliver;
                }
                mouse_cycle++;
                break;
            case 3:
                mouse_byte[3] = data;
                mouse_cycle = 0;

            deliver: {
                /* Decode */
                int32_t dx = (int32_t)mouse_byte[1];
                int32_t dy = (int32_t)mouse_byte[2];
                int32_t wheel = 0;
                uint8_t buttons = mouse_byte[0] & 0x07;   /* L,R,M */

                /* Sign extend / overflow clamp on the 9-bit X/Y deltas. */
                if (mouse_byte[0] & 0x10) dx -= 256;
                if (mouse_byte[0] & 0x20) dy -= 256;
                dx = mouse_clamp_delta(dx, mouse_byte[0] & 0x40, mouse_byte[0] & 0x10);
                dy = mouse_clamp_delta(dy, mouse_byte[0] & 0x80, mouse_byte[0] & 0x20);

                if (gen == 3) {
                    /* IntelliMouse: byte 3 is a signed 8-bit Z-axis.
                     * The cast to int8_t does the sign extension. */
                    wheel = (int32_t)(int8_t)mouse_byte[3];
                } else if (gen == 4) {
                    /* IntelliMouse Explorer: byte 3 layout is
                     *   bits 0-3 — Z-axis, signed 4-bit (-8..7)
                     *   bit  4   — button 4 (BTN_SIDE)
                     *   bit  5   — button 5 (BTN_EXTRA)
                     *   bits 6-7 — reserved (always zero)
                     */
                    int8_t z4 = (int8_t)(mouse_byte[3] & 0x0F);
                    if (z4 & 0x08) z4 |= 0xF0;          /* sign-extend bit 3 */
                    wheel = (int32_t)z4;
                    if (mouse_byte[3] & 0x10) buttons |= (1u << 3);   /* btn 4 */
                    if (mouse_byte[3] & 0x20) buttons |= (1u << 4);   /* btn 5 */
                }

                /* Buttons that changed since the previous packet — must be
                 * computed BEFORE updating mouse_buttons. */
                uint8_t changed = (uint8_t)(buttons ^ mouse_buttons);
                int reported = 0;

                mouse_buttons = buttons;
                mouse_x += dx;
                mouse_y -= dy;  /* PS/2 Y-axis is inverted relative to screen */

                mouse_q_push(dx, -dy, wheel, buttons);

                /* Report only what actually changed — as the Linux input
                 * core (and substrate's USB HID mouse) already do.  Posting
                 * the full button state plus both axes on EVERY packet (even
                 * idle ones) floods the 64-entry input ring; once an X client
                 * such as matwm2 consumes events the server falls behind, the
                 * ring overflows, and real press/release transitions get
                 * dropped — the janky motion and stuck/misread buttons seen
                 * under matwm2. */
                if (dx)    { input_report_rel(&mouse_dev, REL_X, dx);        reported = 1; }
                if (dy)    { input_report_rel(&mouse_dev, REL_Y, -dy);       reported = 1; }
                if (wheel) { input_report_rel(&mouse_dev, REL_WHEEL, wheel); reported = 1; }
                if (changed & 0x01) { input_report_key(&mouse_dev, BTN_LEFT,   (buttons & 0x01) ? 1 : 0); reported = 1; }
                if (changed & 0x02) { input_report_key(&mouse_dev, BTN_RIGHT,  (buttons & 0x02) ? 1 : 0); reported = 1; }
                if (changed & 0x04) { input_report_key(&mouse_dev, BTN_MIDDLE, (buttons & 0x04) ? 1 : 0); reported = 1; }
                if (gen == 4) {
                    if (changed & 0x08) { input_report_key(&mouse_dev, BTN_SIDE,  (buttons & 0x08) ? 1 : 0); reported = 1; }
                    if (changed & 0x10) { input_report_key(&mouse_dev, BTN_EXTRA, (buttons & 0x10) ? 1 : 0); reported = 1; }
                }
                if (reported) input_sync(&mouse_dev);
                break;
            }
        }
    }

    /* EOI handled by IDT dispatcher */
    (void)regs;
}

void mouse_get_state(int32_t *x, int32_t *y, uint8_t *buttons) {
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (buttons) *buttons = mouse_buttons;
}
