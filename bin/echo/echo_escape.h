#ifndef ECHO_ESCAPE_H
#define ECHO_ESCAPE_H

#include <stddef.h>
#include <stdbool.h>

typedef int (*echo_emit_fn)(void *ctx, const unsigned char *data, size_t len);

int echo_emit_text(const char *text, bool interpret_escapes, echo_emit_fn emit,
    void *emit_ctx, bool *stop_output);

#endif