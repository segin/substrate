/*
 * utf8.c - UTF-8 support for EditLine library
 *
 * Provides UTF-8 decoding, validation, display width calculation,
 * and codepoint-aware navigation helpers.  The line buffer stores
 * UTF-8 bytes; cursor and length track byte positions.
 */
#include <stdlib.h>
#include <string.h>
#include "el.h"

/*
 * Decode one UTF-8 codepoint from `s` (up to `n` bytes available).
 * On success: stores codepoint in *cp, returns number of bytes consumed (1-4).
 * On error (invalid/overlong/surrogate/too-large): returns -1.
 * On incomplete sequence (n too small): returns 0.
 */
int utf8_decode(const char *s, size_t n, uint32_t *cp)
{
	unsigned char c;
	uint32_t val;
	int need;

	if (n == 0)
		return 0;
	c = (unsigned char)s[0];

	if (c < 0x80) {
		*cp = c;
		return 1;
	} else if ((c & 0xE0) == 0xC0) {
		val = c & 0x1F;
		need = 2;
	} else if ((c & 0xF0) == 0xE0) {
		val = c & 0x0F;
		need = 3;
	} else if ((c & 0xF8) == 0xF0) {
		val = c & 0x07;
		need = 4;
	} else {
		return -1; /* invalid lead byte */
	}

	if ((size_t)need > n)
		return 0; /* incomplete */

	for (int i = 1; i < need; i++) {
		c = (unsigned char)s[i];
		if ((c & 0xC0) != 0x80)
			return -1; /* invalid continuation */
		val = (val << 6) | (c & 0x3F);
	}

	/* Reject overlong encodings */
	if (need == 2 && val < 0x80)
		return -1;
	if (need == 3 && val < 0x800)
		return -1;
	if (need == 4 && val < 0x10000)
		return -1;

	/* Reject surrogates and values > U+10FFFF */
	if (val >= 0xD800 && val <= 0xDFFF)
		return -1;
	if (val > 0x10FFFF)
		return -1;

	*cp = val;
	return need;
}

/*
 * Encode a codepoint as UTF-8 into `buf` (must have room for 4 bytes).
 * Returns number of bytes written (1-4), or 0 on invalid codepoint.
 */
int utf8_encode(uint32_t cp, char *buf)
{
	if (cp < 0x80) {
		buf[0] = (char)cp;
		return 1;
	} else if (cp < 0x800) {
		buf[0] = (char)(0xC0 | (cp >> 6));
		buf[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	} else if (cp < 0x10000) {
		if (cp >= 0xD800 && cp <= 0xDFFF)
			return 0;
		buf[0] = (char)(0xE0 | (cp >> 12));
		buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		buf[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	} else if (cp <= 0x10FFFF) {
		buf[0] = (char)(0xF0 | (cp >> 18));
		buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
		buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
		buf[3] = (char)(0x80 | (cp & 0x3F));
		return 4;
	}
	return 0;
}

/*
 * Return the display width of a codepoint.
 * 0 for combining characters, 2 for CJK wide, 1 for most printable,
 * -1 for non-printable control characters.
 */
int utf8_width(uint32_t cp)
{
	/* C0 controls */
	if (cp < 0x20)
		return -1;
	if (cp == 0x7F)
		return -1;
	/* C1 controls */
	if (cp >= 0x80 && cp < 0xA0)
		return -1;

	/* Zero-width combining marks (selected Unicode ranges) */
	/* Combining Diacritical Marks */
	if (cp >= 0x0300 && cp <= 0x036F)
		return 0;
	/* Combining Diacritical Marks Extended */
	if (cp >= 0x1AB0 && cp <= 0x1AFF)
		return 0;
	/* Combining Diacritical Marks Supplement */
	if (cp >= 0x1DC0 && cp <= 0x1DFF)
		return 0;
	/* Combining Diacritical Marks for Symbols */
	if (cp >= 0x20D0 && cp <= 0x20FF)
		return 0;
	/* Combining Half Marks */
	if (cp >= 0xFE20 && cp <= 0xFE2F)
		return 0;
	/* Zero-width space, zero-width non-joiner, zero-width joiner */
	if (cp == 0x200B || cp == 0x200C || cp == 0x200D)
		return 0;
	/* Soft hyphen */
	if (cp == 0x00AD)
		return 0;
	/* Format characters (selected) */
	if (cp == 0xFEFF) /* BOM / ZWNBSP */
		return 0;

	/* CJK double-width characters */
	/* CJK Unified Ideographs */
	if (cp >= 0x4E00 && cp <= 0x9FFF)
		return 2;
	/* CJK Unified Ideographs Extension A */
	if (cp >= 0x3400 && cp <= 0x4DBF)
		return 2;
	/* CJK Compatibility Ideographs */
	if (cp >= 0xF900 && cp <= 0xFAFF)
		return 2;
	/* CJK Unified Ideographs Extension B-F, G-K */
	if (cp >= 0x20000 && cp <= 0x3FFFF)
		return 2;
	/* Hangul Syllables */
	if (cp >= 0xAC00 && cp <= 0xD7AF)
		return 2;
	/* Hangul Jamo */
	if (cp >= 0x1100 && cp <= 0x115F)
		return 2;
	if (cp >= 0x2329 && cp <= 0x232A)
		return 2;
	/* Fullwidth Forms */
	if (cp >= 0xFF01 && cp <= 0xFF60)
		return 2;
	if (cp >= 0xFFE0 && cp <= 0xFFE6)
		return 2;
	/* CJK Radicals Supplement, Kangxi, Ideographic Description */
	if (cp >= 0x2E80 && cp <= 0x303E)
		return 2;
	/* Kanbun, CJK Strokes */
	if (cp >= 0x3190 && cp <= 0x31BF)
		return 2;
	/* Katakana, Bopomofo */
	if (cp >= 0x31F0 && cp <= 0x31FF)
		return 2;
	if (cp >= 0x3200 && cp <= 0x33FF)
		return 2;
	/* Yijing, CJK Compatibility */
	if (cp >= 0xA000 && cp <= 0xA4CF)
		return 2;
	/* Enclosed CJK */
	if (cp >= 0xFE30 && cp <= 0xFE4F)
		return 2;

	return 1;
}

/*
 * Return the byte length of the UTF-8 character starting at buf[pos]
 * within a buffer of `len` bytes. Returns 1 for ASCII or invalid bytes.
 */
int utf8_char_len(const char *buf, size_t pos, size_t len)
{
	uint32_t cp;
	int n;

	if (pos >= len)
		return 0;
	n = utf8_decode(buf + pos, len - pos, &cp);
	return (n > 0) ? n : 1;
}

/*
 * Move cursor backward by one codepoint in a UTF-8 buffer.
 * Returns new byte position. Skips continuation bytes.
 */
size_t utf8_prev(const char *buf, size_t pos)
{
	if (pos == 0)
		return 0;
	pos--;
	/* Skip continuation bytes (10xxxxxx) */
	while (pos > 0 && ((unsigned char)buf[pos] & 0xC0) == 0x80)
		pos--;
	return pos;
}

/*
 * Move cursor forward by one codepoint in a UTF-8 buffer.
 * Returns new byte position.
 */
size_t utf8_next(const char *buf, size_t pos, size_t len)
{
	if (pos >= len)
		return len;
	return pos + (size_t)utf8_char_len(buf, pos, len);
}

/*
 * Check if a codepoint is a "word" character for word-boundary navigation.
 * Returns 1 for letters and digits (Unicode-aware categories: L, N).
 */
int utf8_is_word(uint32_t cp)
{
	/* ASCII fast path */
	if (cp < 0x80) {
		return (cp >= 'a' && cp <= 'z') ||
		       (cp >= 'A' && cp <= 'Z') ||
		       (cp >= '0' && cp <= '9') ||
		       cp == '_';
	}
	/* Latin-1 Supplement letters */
	if (cp >= 0xC0 && cp <= 0xFF && cp != 0xD7 && cp != 0xF7)
		return 1;
	/* General: treat most non-ASCII non-control chars as word chars */
	if (cp >= 0x100)
		return 1;
	return 0;
}

/*
 * Calculate the display width of a UTF-8 string from `buf` of `len` bytes.
 * Non-printable characters are counted as 2 (^X display).
 * Codepoints > U+FFFF with negative width are counted as 6 (\uXXXX display).
 */
int utf8_display_width(const char *buf, size_t len)
{
	size_t pos = 0;
	int width = 0;

	while (pos < len) {
		uint32_t cp;
		int n = utf8_decode(buf + pos, len - pos, &cp);
		if (n <= 0) {
			/* Invalid byte: display as ^? or similar */
			width += 2;
			pos++;
			continue;
		}
		int w = utf8_width(cp);
		if (w < 0) {
			/* Non-printable: ^X = 2 columns */
			width += 2;
		} else {
			width += w;
		}
		pos += (size_t)n;
	}
	return width;
}

/*
 * Detect whether the current locale uses UTF-8 encoding.
 * Checks $LC_CTYPE, $LC_ALL, $LANG environment variables.
 * Returns 1 for UTF-8, 0 for ASCII/other.
 */
int utf8_is_locale_utf8(void)
{
	const char *env;

	env = getenv("LC_CTYPE");
	if (!env || !*env)
		env = getenv("LC_ALL");
	if (!env || !*env)
		env = getenv("LANG");
	if (!env)
		return 0;

	/* Look for "UTF-8" or "utf-8" or "utf8" in the string */
	if (strstr(env, "UTF-8") || strstr(env, "utf-8") ||
	    strstr(env, "UTF8") || strstr(env, "utf8"))
		return 1;
	return 0;
}
