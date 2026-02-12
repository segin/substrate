/*
 * libuu - Uuencoding/decoding Library
 *
 * libuu.h: Core definitions and prototypes.
 */

#ifndef _LIBUU_H
#define _LIBUU_H

#include <stddef.h>

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
ssize_t uu_decode_line(const char *line, unsigned char *out_buffer, size_t out_max);

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
int uu_parse_header(const char *line, int *mode, char *filename, size_t filename_size);

#endif
