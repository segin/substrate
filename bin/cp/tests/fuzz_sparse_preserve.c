#include "cp_opts.h"
#include "cp_test.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *buf;
    const char *err = NULL;
    size_t parsed_size;
    struct cp_options opts;
    char *argv[6];

    if (size == 0 || size > 4096) {
        return 0;
    }

    buf = (char *)malloc(size + 1);
    if (!buf) {
        return 0;
    }

    memcpy(buf, data, size);
    buf[size] = '\0';

    cp_test_buf_all_zero((const unsigned char *)buf, size);

    argv[0] = (char *)"cp";
    argv[1] = (char *)"--preserve";
    argv[2] = buf;
    argv[3] = (char *)"src";
    argv[4] = (char *)"dst";

    (void)cp_parse_options(&opts, 5, argv, &err);
    (void)cp_parse_size(buf, &parsed_size, &err);

    free(buf);
    return 0;
}
