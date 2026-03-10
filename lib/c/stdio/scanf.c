/*
 * scanf.c - scanf family implementation for Substrate libc
 *
 * Implements vsscanf (core engine), vfscanf, fscanf, scanf, sscanf, vscanf.
 * Supports: d, i, u, o, x/X, f/e/g/a, c, s, [scanset], p, n, %%
 * Length modifiers: hh, h, l, ll, j, z, t, L
 * Assignment suppression (*), field width
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <ctype.h>

// String-based input source for vsscanf
typedef struct {
	const char *p;
	int chars_consumed;
} scan_state_t;

static int scan_getc(scan_state_t *st) {
	if(!*st->p) return EOF;
	st->chars_consumed++;
	return (unsigned char)*st->p++;
}

static void scan_ungetc(scan_state_t *st, int c) {
	if(c != EOF) {
		st->p--;
		st->chars_consumed--;
	}
}

static int is_in_scanset(const char *set, int setlen, int c, int negated) {
	int found = 0;
	for(int i = 0; i < setlen; i++) {
		if(i + 2 < setlen && set[i + 1] == '-') {
			// Range: a-z
			if(c >= (unsigned char)set[i] && c <= (unsigned char)set[i + 2]) found = 1;
			i += 2;
		} else {
			if(c == (unsigned char)set[i]) found = 1;
		}
	}
	return negated ? !found : found;
}

int vsscanf(const char *str, const char *format, va_list ap) {
	scan_state_t st = { str, 0 };
	const char *f = format;
	int assigned = 0;
	int c;

	while(*f) {
		// Whitespace in format: skip whitespace in input
		if(*f == ' ' || *f == '\t' || *f == '\n') {
			while(*f == ' ' || *f == '\t' || *f == '\n') f++;
			while((c = scan_getc(&st)) != EOF && (c == ' ' || c == '\t' || c == '\n' || c == '\r'));
			if(c != EOF) scan_ungetc(&st, c);
			continue;
		}

		if(*f != '%') {
			// Literal match
			c = scan_getc(&st);
			if(c != (unsigned char)*f) return (c == EOF && assigned == 0) ? EOF : assigned;
			f++;
			continue;
		}

		f++; // skip %

		if(*f == '%') {
			// Literal %
			c = scan_getc(&st);
			if(c != '%') return (c == EOF && assigned == 0) ? EOF : assigned;
			f++;
			continue;
		}

		// Assignment suppression
		int suppress = 0;
		if(*f == '*') { suppress = 1; f++; }

		// Field width
		int width = 0;
		while(*f >= '0' && *f <= '9') { width = width * 10 + (*f - '0'); f++; }
		if(width == 0) width = INT_MAX;

		// Length modifier
		enum { LN_NONE, LN_HH, LN_H, LN_L, LN_LL, LN_J, LN_Z, LN_T, LN_LONGDBL } length = LN_NONE;
		if(*f == 'h') { f++; if(*f == 'h') { length = LN_HH; f++; } else length = LN_H; }
		else if(*f == 'l') { f++; if(*f == 'l') { length = LN_LL; f++; } else length = LN_L; }
		else if(*f == 'j') { length = LN_J; f++; }
		else if(*f == 'z') { length = LN_Z; f++; }
		else if(*f == 't') { length = LN_T; f++; }
		else if(*f == 'L') { length = LN_LONGDBL; f++; }

		char spec = *f++;

		switch(spec) {
		case 'd': case 'i': case 'u': case 'o': case 'x': case 'X': case 'p': {
			// Skip whitespace
			while((c = scan_getc(&st)) != EOF && (c == ' ' || c == '\t' || c == '\n' || c == '\r'));
			if(c == EOF) return assigned == 0 ? EOF : assigned;
			scan_ungetc(&st, c);

			// Parse integer
			int sign = 1;
			int base = 10;
			int chars_read = 0;
			uint64_t val = 0;
			int got_digit = 0;

			if(spec == 'o') base = 8;
			else if(spec == 'x' || spec == 'X' || spec == 'p') base = 16;

			// Sign
			c = scan_getc(&st);
			if(c == EOF) return assigned == 0 ? EOF : assigned;
			if(c == '-' && spec != 'u' && spec != 'o' && spec != 'x' && spec != 'X' && spec != 'p') {
				sign = -1; chars_read++;
			} else if(c == '+') {
				chars_read++;
			} else {
				scan_ungetc(&st, c);
			}

			// Base prefix for %i
			if(spec == 'i' && chars_read < width) {
				c = scan_getc(&st);
				if(c == '0') {
					chars_read++;
					c = scan_getc(&st);
					if(c == 'x' || c == 'X') {
						base = 16; chars_read++;
					} else {
						base = 8;
						if(c != EOF) scan_ungetc(&st, c);
					}
				} else {
					if(c != EOF) scan_ungetc(&st, c);
				}
			}

			// 0x prefix for hex
			if((spec == 'x' || spec == 'X' || spec == 'p') && chars_read < width) {
				c = scan_getc(&st);
				if(c == '0') {
					chars_read++;
					c = scan_getc(&st);
					if(c == 'x' || c == 'X') { chars_read++; }
					else { if(c != EOF) scan_ungetc(&st, c); got_digit = 1; val = 0; }
				} else {
					if(c != EOF) scan_ungetc(&st, c);
				}
			}

			// Read digits
			while(chars_read < width) {
				c = scan_getc(&st);
				if(c == EOF) break;
				int digit = -1;
				if(c >= '0' && c <= '9') digit = c - '0';
				else if(c >= 'a' && c <= 'f') digit = c - 'a' + 10;
				else if(c >= 'A' && c <= 'F') digit = c - 'A' + 10;
				if(digit < 0 || digit >= base) { scan_ungetc(&st, c); break; }
				val = val * base + digit;
				got_digit = 1;
				chars_read++;
			}

			if(!got_digit) return assigned;

			if(!suppress) {
				int64_t sval = (int64_t)val * sign;
				if(spec == 'p') { void **pp = va_arg(ap, void**); *pp = (void*)(uintptr_t)val; }
				else if(spec == 'u' || spec == 'o' || spec == 'x' || spec == 'X') {
					if(length == LN_HH) { unsigned char *p = va_arg(ap, unsigned char*); *p = (unsigned char)val; }
					else if(length == LN_H) { unsigned short *p = va_arg(ap, unsigned short*); *p = (unsigned short)val; }
					else if(length == LN_L) { unsigned long *p = va_arg(ap, unsigned long*); *p = (unsigned long)val; }
					else if(length == LN_LL || length == LN_J) { unsigned long long *p = va_arg(ap, unsigned long long*); *p = (unsigned long long)val; }
					else if(length == LN_Z) { size_t *p = va_arg(ap, size_t*); *p = (size_t)val; }
					else { unsigned int *p = va_arg(ap, unsigned int*); *p = (unsigned int)val; }
				} else {
					if(length == LN_HH) { signed char *p = va_arg(ap, signed char*); *p = (signed char)sval; }
					else if(length == LN_H) { short *p = va_arg(ap, short*); *p = (short)sval; }
					else if(length == LN_L) { long *p = va_arg(ap, long*); *p = (long)sval; }
					else if(length == LN_LL || length == LN_J) { long long *p = va_arg(ap, long long*); *p = (long long)sval; }
					else if(length == LN_Z) { ssize_t *p = va_arg(ap, ssize_t*); *p = (ssize_t)sval; }
					else if(length == LN_T) { ptrdiff_t *p = va_arg(ap, ptrdiff_t*); *p = (ptrdiff_t)sval; }
					else { int *p = va_arg(ap, int*); *p = (int)sval; }
				}
				assigned++;
			}
			break;
		}

		case 'f': case 'e': case 'g': case 'a':
		case 'F': case 'E': case 'G': case 'A': {
			char fbuf[128];
			int fi = 0, got_digit = 0, got_dot = 0, got_exp = 0;
			int k_width = width;
			if(k_width > 127) k_width = 127;

			// Skip whitespace
			while((c = scan_getc(&st)) != EOF && isspace(c));
			if(c == EOF) return (assigned == 0) ? EOF : assigned;

			while(fi < k_width) {
				if(isdigit(c)) {
					fbuf[fi++] = c;
					got_digit = 1;
				} else if(c == '.' && !got_dot && !got_exp) {
					fbuf[fi++] = c;
					got_dot = 1;
				} else if((c == 'e' || c == 'E') && !got_exp && got_digit) {
					fbuf[fi++] = c;
					got_exp = 1;
					c = scan_getc(&st);
					if(c == '+' || c == '-') {
						if(fi < k_width) fbuf[fi++] = c;
					} else if(c != EOF) {
						scan_ungetc(&st, c);
					}
				} else if((c == '-' || c == '+') && fi == 0) {
					fbuf[fi++] = c;
				} else {
					if(c != EOF) scan_ungetc(&st, c);
					break;
				}
				if(fi >= k_width) break;
				c = scan_getc(&st);
			}
			fbuf[fi] = '\0';

			if(!got_digit) return assigned;

			if(!suppress) {
				if(length == LN_LONGDBL) {
					*(long double *)va_arg(ap, long double *) = strtold(fbuf, NULL);
				} else if(length == LN_L) {
					*(double *)va_arg(ap, double *) = strtod(fbuf, NULL);
				} else {
					*(float *)va_arg(ap, float *) = strtof(fbuf, NULL);
				}
				assigned++;
			}
			break;
		}

		case 'c': {
			int count = (width == INT_MAX) ? 1 : width;
			char *dest = suppress ? NULL : va_arg(ap, char*);
			int i;
			for(i = 0; i < count; i++) {
				c = scan_getc(&st);
				if(c == EOF) break;
				if(dest) dest[i] = c;
			}
			if(i == 0) return assigned == 0 ? EOF : assigned;
			if(!suppress) assigned++;
			break;
		}

		case 's': {
			// Skip whitespace
			while((c = scan_getc(&st)) != EOF && (c == ' ' || c == '\t' || c == '\n' || c == '\r'));
			if(c == EOF) return assigned == 0 ? EOF : assigned;
			scan_ungetc(&st, c);

			char *dest = suppress ? NULL : va_arg(ap, char*);
			int i = 0;
			while(i < width) {
				c = scan_getc(&st);
				if(c == EOF || c == ' ' || c == '\t' || c == '\n' || c == '\r') {
					if(c != EOF) scan_ungetc(&st, c);
					break;
				}
				if(dest) dest[i] = c;
				i++;
			}
			if(i == 0) return assigned;
			if(dest) dest[i] = '\0';
			if(!suppress) assigned++;
			break;
		}

		case '[': {
			// Parse scanset
			int negated = 0;
			if(*f == '^') { negated = 1; f++; }
			const char *set_start = f;
			// ] as first char is part of set
			if(*f == ']') f++;
			while(*f && *f != ']') f++;
			int setlen = f - set_start;
			if(*f == ']') f++;

			char *dest = suppress ? NULL : va_arg(ap, char*);
			int i = 0;
			while(i < width) {
				c = scan_getc(&st);
				if(c == EOF || !is_in_scanset(set_start, setlen, c, negated)) {
					if(c != EOF) scan_ungetc(&st, c);
					break;
				}
				if(dest) dest[i] = c;
				i++;
			}
			if(i == 0) return assigned;
			if(dest) dest[i] = '\0';
			if(!suppress) assigned++;
			break;
		}

		case 'n': {
			if(!suppress) {
				if(length == LN_L) { long *p = va_arg(ap, long*); *p = st.chars_consumed; }
				else if(length == LN_LL) { long long *p = va_arg(ap, long long*); *p = st.chars_consumed; }
				else { int *p = va_arg(ap, int*); *p = st.chars_consumed; }
			}
			// %n does not count as an assignment
			break;
		}

		default:
			return assigned;
		}
	}

	return assigned;
}

int sscanf(const char *str, const char *format, ...) {
	va_list ap; va_start(ap, format);
	int ret = vsscanf(str, format, ap);
	va_end(ap);
	return ret;
}

int vfscanf(FILE *stream, const char *format, va_list ap) {
	// Read available input into buffer, then delegate to vsscanf
	// This is simplified: read up to 4096 bytes
	char buf[4096];
	int i = 0;
	int c;
	while(i < 4095) {
		c = fgetc(stream);
		if(c == EOF) break;
		buf[i++] = c;
	}
	buf[i] = '\0';
	if(i == 0 && c == EOF) return EOF;
	return vsscanf(buf, format, ap);
}

int fscanf(FILE *stream, const char *format, ...) {
	va_list ap; va_start(ap, format);
	int ret = vfscanf(stream, format, ap);
	va_end(ap);
	return ret;
}

int scanf(const char *format, ...) {
	va_list ap; va_start(ap, format);
	int ret = vfscanf(stdin, format, ap);
	va_end(ap);
	return ret;
}

int vscanf(const char *format, va_list ap) {
	return vfscanf(stdin, format, ap);
}
