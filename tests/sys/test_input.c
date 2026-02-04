#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdint.h>

// Must match kernel definition
typedef struct input_event {
    uint64_t time_sec;
    uint64_t time_usec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
} input_event_t;

int main(int argc, char **argv) {
    int fd = open("/dev/input0", O_RDONLY);
    if (fd < 0) {
        printf("Failed to open /dev/input0\n");
        return 1;
    }

    printf("Reading input events...\n");
    input_event_t ev;
    while (1) {
        int ret = read(fd, &ev, sizeof(ev));
        if (ret == sizeof(ev)) {
            printf("Event: type=%d code=%d value=%d\n", ev.type, ev.code, ev.value);
        } else {
            // Non-blocking read might return 0 or -1
            if (ret < 0) perror("read");
        }
    }
    close(fd);
    return 0;
}
