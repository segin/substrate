#include <stddef.h>
#include <stdint.h>

#include "cat_cooked.h"

struct fuzz_sink {
    unsigned char out[8192];
    size_t used;
};

static int fuzz_emit(void *ctx, const unsigned char *data, size_t len)
{
    struct fuzz_sink *sink = (struct fuzz_sink *)ctx;
    size_t i;

    for (i = 0; i < len; i++) {
        if (sink->used < sizeof(sink->out)) {
            sink->out[sink->used] = data[i];
            sink->used++;
        }
    }

    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct cat_cooked_cfg cfg;
    struct cat_cooked_state state;
    struct fuzz_sink sink;

    if (size == 0) {
        return 0;
    }

    cfg.number_all = (data[0] & 0x01u) != 0;
    cfg.number_nonblank = (data[0] & 0x02u) != 0;
    cfg.squeeze_blank = (data[0] & 0x04u) != 0;
    cfg.show_ends = (data[0] & 0x08u) != 0;
    cfg.show_tabs = (data[0] & 0x10u) != 0;
    cfg.show_nonprint = (data[0] & 0x20u) != 0;

    cat_cooked_state_init(&state);
    sink.used = 0;

    (void)cat_cooked_process(data + 1, size - 1, &cfg, &state, fuzz_emit, &sink);
    return 0;
}
