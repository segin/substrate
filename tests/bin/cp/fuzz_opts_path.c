#include "cp_opts.h"
#include "cp_path.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    char *buf;
    char *argv[20];
    int argc = 0;
    size_t i;
    struct cp_options opts;
    const char *err = NULL;

    if (size == 0 || size > 4096) {
        return 0;
    }

    buf = (char *)malloc(size + 1);
    if (!buf) {
        return 0;
    }

    for (i = 0; i < size; ++i) {
        unsigned char c = data[i];
        if (c == '\0') {
            c = ' ';
        }
        buf[i] = (char)c;
    }
    buf[size] = '\0';

    argv[argc++] = (char *)"cp";
    argv[argc++] = buf;

    for (i = 0; i < size && argc < 19; ++i) {
        if (buf[i] == '\n' || buf[i] == ' ' || buf[i] == '\t') {
            buf[i] = '\0';
            if (i + 1 < size && buf[i + 1] != '\0') {
                argv[argc++] = &buf[i + 1];
            }
        }
    }

    if (argc < 3) {
        argv[argc++] = (char *)"src";
        argv[argc++] = (char *)"dst";
    }

    (void)cp_parse_options(&opts, argc, argv, &err);

    for (i = 1; i < (size_t)argc; ++i) {
        char *j = cp_path_join("/tmp", argv[i]);
        (void)cp_path_basename(argv[i]);
        if (j) {
            free(j);
        }
    }

    free(buf);
    return 0;
}
