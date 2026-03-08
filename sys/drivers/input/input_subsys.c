#include <sys/input.h>
#include <sys/file.h>
#include <vm/vm_kmem.h>
#include <kern/sched.h>
#include <string.h>
#include <kern/console.h>
#include <vfs/vfs.h>
#include <sys/time.h>
#include <kern/time.h>

#define INPUT_QUEUE_SIZE 64

// An open handle to an input device (or the multiplexer)
typedef struct input_handle {
    input_event_t queue[INPUT_QUEUE_SIZE];
    int head;
    int tail;
    int dropped;
    struct input_handle *next;
} input_handle_t;

// Global list of devices
static input_dev_t *input_devices = NULL;

static input_event_t global_event_log[INPUT_QUEUE_SIZE];
static uint64_t global_seq = 0; // Total events written

static fs_node_t event_node;

void input_register_devfs(void);

void input_init(void) {
    input_devices = NULL;
    kprint("Input Subsystem Initialized\n");
    input_register_devfs();
}

int input_register_device(input_dev_t *dev) {
    if (!dev) return -1;
    dev->next = input_devices;
    input_devices = dev;
    kprint("Input: Registered device: ");
    kprint(dev->name);
    kprint("\n");
    return 0;
}

void input_unregister_device(input_dev_t *dev) {
    if (!dev) return;

    if (input_devices == dev) {
        input_devices = dev->next;
        dev->next = NULL;
        kprint("Input: Unregistered device: ");
        kprint(dev->name);
        kprint("\n");
        return;
    }

    input_dev_t *curr = input_devices;
    while (curr) {
        if (curr->next == dev) {
            curr->next = dev->next;
            dev->next = NULL;
            kprint("Input: Unregistered device: ");
            kprint(dev->name);
            kprint("\n");
            return;
        }
        curr = curr->next;
    }
}

void input_notify_readers(void) {
    sched_wakeup(&global_event_log);
}

// Distribute event to all handles
void input_report_event(input_dev_t *dev, uint16_t type, uint16_t code, int32_t value) {
    (void)dev; // In future, use this to filter
    
    struct timeval tv;
    sys_gettimeofday(&tv, NULL);

    uint64_t idx = global_seq % INPUT_QUEUE_SIZE;
    global_event_log[idx].time_sec = tv.tv_sec;
    global_event_log[idx].time_usec = tv.tv_usec;
    global_event_log[idx].type = type;
    global_event_log[idx].code = code;
    global_event_log[idx].value = value;
    global_seq++;
    
    input_notify_readers();
}

void input_sync(input_dev_t *dev) {
    input_report_event(dev, EV_SYN, 0, 0);
}

// Legacy compatibility wrapper
static input_dev_t legacy_dev = { .name = "Legacy Input" };
void input_enqueue(uint16_t type, uint16_t code, int32_t value) {
    input_report_event(&legacy_dev, type, code, value);
}

static uint32_t input_read(fs_node_t *node, off_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    if (size < sizeof(input_event_t)) return 0;
    
    uint64_t current_seq = offset / sizeof(input_event_t);
    
    // WaitForData
    while (current_seq >= global_seq) {
         extern void sched_sleep(void *chan);
         sched_sleep(&global_event_log);
    }
    
    // Check for overrun
    if (global_seq > current_seq + INPUT_QUEUE_SIZE) {
        // Overrun!
        return -1; // EOVERFLOW equivalent
    }
    
    int read_count = 0;
    input_event_t *out = (input_event_t*)buffer;
    
    while (current_seq < global_seq && size >= sizeof(input_event_t)) {
        uint64_t idx = current_seq % INPUT_QUEUE_SIZE;
        *out = global_event_log[idx];
        out++;
        read_count++;
        current_seq++;
        size -= sizeof(input_event_t);
    }
    
    return read_count * sizeof(input_event_t);
}

void input_register_devfs(void) {
    memset(&event_node, 0, sizeof(fs_node_t));
    strlcpy(event_node.name, "input/event0", sizeof(event_node.name));
    event_node.flags = FS_CHARDEVICE;
    event_node.read = &input_read;
    devfs_register_device(&event_node);
}

