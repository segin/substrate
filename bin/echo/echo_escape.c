#include "echo_escape.h"

#include <stddef.h>

static int
echo_emit_bytes(echo_emit_fn emit, void *emit_ctx, const unsigned char *data,
    size_t len)
{
    if (len == 0) {
        return 0;
    }
    return emit(emit_ctx, data, len);
}

static int
echo_emit_byte(echo_emit_fn emit, void *emit_ctx, unsigned char byte)
{
    return echo_emit_bytes(emit, emit_ctx, &byte, 1);
}

static int
echo_is_oct_digit(char ch)
{
    return ch >= '0' && ch <= '7';
}

static int
echo_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

static int
echo_emit_utf8(unsigned long codepoint, echo_emit_fn emit, void *emit_ctx)
{
    unsigned char buffer[4];
    size_t len;

    if (codepoint > 0x10FFFFUL ||
        (codepoint >= 0xD800UL && codepoint <= 0xDFFFUL)) {
        return -1;
    }

    if (codepoint <= 0x7FUL) {
        buffer[0] = (unsigned char)codepoint;
        len = 1;
    } else if (codepoint <= 0x7FFUL) {
        buffer[0] = (unsigned char)(0xC0U | ((codepoint >> 6) & 0x1FU));
        buffer[1] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        len = 2;
    } else if (codepoint <= 0xFFFFUL) {
        buffer[0] = (unsigned char)(0xE0U | ((codepoint >> 12) & 0x0FU));
        buffer[1] = (unsigned char)(0x80U | ((codepoint >> 6) & 0x3FU));
        buffer[2] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        len = 3;
    } else {
        buffer[0] = (unsigned char)(0xF0U | ((codepoint >> 18) & 0x07U));
        buffer[1] = (unsigned char)(0x80U | ((codepoint >> 12) & 0x3FU));
        buffer[2] = (unsigned char)(0x80U | ((codepoint >> 6) & 0x3FU));
        buffer[3] = (unsigned char)(0x80U | (codepoint & 0x3FU));
        len = 4;
    }

    return echo_emit_bytes(emit, emit_ctx, buffer, len);
}

static int
echo_emit_literal_escape(char marker, echo_emit_fn emit, void *emit_ctx)
{
    if (echo_emit_byte(emit, emit_ctx, '\\') != 0) {
        return -1;
    }
    return echo_emit_byte(emit, emit_ctx, (unsigned char)marker);
}

int
echo_emit_text(const char *text, bool interpret_escapes, echo_emit_fn emit,
    void *emit_ctx, bool *stop_output)
{
    size_t index;

    if (stop_output != NULL) {
        *stop_output = false;
    }
    for (index = 0; text[index] != '\0'; ++index) {
        if (!interpret_escapes || text[index] != '\\') {
            if (echo_emit_byte(emit, emit_ctx, (unsigned char)text[index]) != 0) {
                return -1;
            }
            continue;
        }
        ++index;
        if (text[index] == '\0') {
            return echo_emit_byte(emit, emit_ctx, '\\');
        }
        switch (text[index]) {
        case '\\':
            if (echo_emit_byte(emit, emit_ctx, '\\') != 0) {
                return -1;
            }
            break;
        case 'a':
            if (echo_emit_byte(emit, emit_ctx, '\a') != 0) {
                return -1;
            }
            break;
        case 'b':
            if (echo_emit_byte(emit, emit_ctx, '\b') != 0) {
                return -1;
            }
            break;
        case 'c':
            if (stop_output != NULL) {
                *stop_output = true;
            }
            return 0;
        case 'e':
        case 'E':
            if (echo_emit_byte(emit, emit_ctx, 0x1BU) != 0) {
                return -1;
            }
            break;
        case 'f':
            if (echo_emit_byte(emit, emit_ctx, '\f') != 0) {
                return -1;
            }
            break;
        case 'n':
            if (echo_emit_byte(emit, emit_ctx, '\n') != 0) {
                return -1;
            }
            break;
        case 'r':
            if (echo_emit_byte(emit, emit_ctx, '\r') != 0) {
                return -1;
            }
            break;
        case 't':
            if (echo_emit_byte(emit, emit_ctx, '\t') != 0) {
                return -1;
            }
            break;
        case 'v':
            if (echo_emit_byte(emit, emit_ctx, '\v') != 0) {
                return -1;
            }
            break;
        case '0': {
            unsigned int value;
            int digits;

            value = 0;
            for (digits = 0; digits < 3 && echo_is_oct_digit(text[index + 1]);
                    ++digits) {
                value = (value << 3) | (unsigned int)(text[index + 1] - '0');
                ++index;
            }
            if (echo_emit_byte(emit, emit_ctx, (unsigned char)value) != 0) {
                return -1;
            }
            break;
        }
        case 'x': {
            unsigned int value;
            int digits;
            int hex_digit;

            value = 0;
            digits = 0;
            while (digits < 2 && (hex_digit = echo_hex_value(text[index + 1])) >= 0) {
                value = (value << 4) | (unsigned int)hex_digit;
                ++digits;
                ++index;
            }
            if (digits == 0) {
                if (echo_emit_literal_escape('x', emit, emit_ctx) != 0) {
                    return -1;
                }
                break;
            }
            if (echo_emit_byte(emit, emit_ctx, (unsigned char)value) != 0) {
                return -1;
            }
            break;
        }
        case 'u':
        case 'U': {
            unsigned long value;
            int digits;
            int needed;
            int hex_digit;
            size_t cursor;

            needed = (text[index] == 'u') ? 4 : 8;
            value = 0;
            cursor = index;
            for (digits = 0; digits < needed; ++digits) {
                hex_digit = echo_hex_value(text[cursor + 1]);
                if (hex_digit < 0) {
                    break;
                }
                value = (value << 4) | (unsigned long)hex_digit;
                ++cursor;
            }
            if (digits != needed || echo_emit_utf8(value, emit, emit_ctx) != 0) {
                if (echo_emit_literal_escape(text[index], emit, emit_ctx) != 0) {
                    return -1;
                }
                break;
            }
            index = cursor;
            break;
        }
        default:
            if (echo_emit_literal_escape(text[index], emit, emit_ctx) != 0) {
                return -1;
            }
            break;
        }
    }

    return 0;
}