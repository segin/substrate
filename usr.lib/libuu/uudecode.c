/*
 * libuu - Uuencoding/decoding Library
 *
 * uudecode.c: Implementation of uuencoding/decoding functions.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include "libuu.h"

#define DEC(c) (((c) - ' ') & 077)

/*
 * Decodes a single line of uuencoded data into a buffer.
 *
 * The input `line` is expected to be a null-terminated string starting
 * with the length character.
 *
 * @param line The input line from the uuencoded file.
 * @param out_buffer Buffer to write decoded bytes to.
 * @param out_max Maximum bytes to write (buffer size).
 * @return The number of bytes written to out_buffer, or -1 on error.
 */
ssize_t uu_decode_line(const char *line, unsigned char *out_buffer, size_t out_max) {
    if (!line || !out_buffer) {
        return -1;
    }

    size_t written = 0;
    int n;

    /* Get the number of bytes encoded on this line */
    /* Check if length char is valid uuencode char (space to backtick) */
    if (*line < ' ' || *line > '`') {
        /* Standard uuencode length char is usually 'M' (45 bytes),
           or check range.
           If it's a newline or null, it's invalid start.
           However, empty line might just have space/backtick as length 0. */
        return -1;
    }

    n = DEC(*line);
    if (n < 0) {
        return -1;
    }
    if (n == 0) {
        return 0; /* End of encoded data */
    }

    line++; /* Skip length char */

    /* Process 4 characters at a time to produce 3 bytes */
    while (n > 0) {
        if (*line == '\0' || *(line+1) == '\0') {
            /* Unexpected end of line */
            break;
        }

        /* Decode 4 characters */
        int ch[4];

        for (int i = 0; i < 4; i++) {
            if (*line == '\0' || *line == '\n' || *line == '\r') {
                /* Short line, pad with 0 */
                ch[i] = 0;
            } else {
                /* Validate char range?
                   Some uuencoders use space, some use backtick.
                   Just rely on DEC masking. */
                ch[i] = DEC(*line);
                line++;
            }
        }

        /* Convert 4x6 bits to 3x8 bits */
        if (n > 0) {
            if (written >= out_max) return -1; /* Buffer overflow */
            out_buffer[written++] = (ch[0] << 2) | ((ch[1] >> 4) & 0x3);
            n--;
        }
        if (n > 0) {
            if (written >= out_max) return -1;
            out_buffer[written++] = ((ch[1] << 4) & 0xF0) | ((ch[2] >> 2) & 0xF);
            n--;
        }
        if (n > 0) {
            if (written >= out_max) return -1;
            out_buffer[written++] = ((ch[2] << 6) & 0xC0) | (ch[3] & 0x3F);
            n--;
        }
    }

    return written;
}

/*
 * Parses the "begin" header line of a uuencoded file.
 *
 * Format: begin <mode> <file>
 *
 * @param line The input line to parse.
 * @param mode Pointer to an integer to store the file mode (e.g., 0644).
 * @param filename Buffer to store the extracted filename.
 * @param filename_size Size of the filename buffer.
 * @return 0 on success, -1 if the line is not a valid begin header.
 */
int uu_parse_header(const char *line, int *mode, char *filename, size_t filename_size) {
    if (!line || !mode || !filename || filename_size == 0) {
        return -1;
    }

    if (strncmp(line, "begin ", 6) != 0) {
        return -1;
    }

    /* Skip "begin " */
    const char *p = line + 6;

    /* Parse mode (octal) */
    long m = 0;

    /* Skip extra spaces before mode if any */
    while (*p == ' ') p++;

    if (!isdigit(*p)) return -1;

    m = 0;
    while (*p >= '0' && *p <= '7') {
        m = (m << 3) + (*p - '0');
        p++;
    }

    *mode = (int)m;

    /* Skip space after mode */
    if (*p != ' ') return -1;
    while (*p == ' ') p++;

    /* The rest is the filename */
    /* Remove trailing newline */
    size_t len = 0;
    while (*p != '\0' && *p != '\n' && *p != '\r') {
        if (len < filename_size - 1) {
            filename[len++] = *p;
        }
        p++;
    }
    filename[len] = '\0';

    if (len == 0) return -1; /* Empty filename */

    return 0;
}
