#include <sys/input.h>
#include "../vfs/vfs.h"
#include "sched.h"
#include <string.h>

#define INPUT_QUEUE_SIZE 128
static input_event_t input_queue[INPUT_QUEUE_SIZE];
static int input_q_head = 0;
static int input_q_tail = 0;

void input_enqueue(uint16_t type, uint16_t code, int32_t value) {
    int next = (input_q_head + 1) % INPUT_QUEUE_SIZE;
    if (next != input_q_tail) {
        input_queue[input_q_head].type = type;
        input_queue[input_q_head].code = code;
        input_queue[input_q_head].value = value;
        input_q_head = next;
        // Wake up any threads waiting for input
        sched_wakeup(&input_queue);
    }
}

static uint32_t input_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    
    if (size < sizeof(input_event_t)) return 0;

    // Block until input available
    while (input_q_head == input_q_tail) {
        sched_sleep(&input_queue);
    }

    int count = 0;
    input_event_t *ev_buf = (input_event_t *)buffer;
    
    while (input_q_head != input_q_tail && size >= sizeof(input_event_t)) {
        ev_buf[count] = input_queue[input_q_tail];
        input_q_tail = (input_q_tail + 1) % INPUT_QUEUE_SIZE;
        size -= sizeof(input_event_t);
        count++;
    }

    return count * sizeof(input_event_t);
}

fs_node_t input_device_node;

void input_init(void) {
    memset(&input_device_node, 0, sizeof(fs_node_t));
    strcpy(input_device_node.name, "input");
    input_device_node.flags = FS_CHARDEVICE;
    input_device_node.read = &input_read;
    
    // In a real system, we would register this in /dev
    // For now it's just a global node.
}
