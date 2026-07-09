/*
 * scanf.c - scanf family implementation for Substrate libc
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <ctype.h>
#include <sys/types.h>

typedef int (*scan_getc_fn)(void *ctx);
typedef void (*scan_ungetc_fn)(void *ctx, int c);

typedef struct {
	scan_getc_fn getc_fn;
	scan_ungetc_fn ungetc_fn;
	void *ctx;
	int chars_consumed;
	int eof_seen;
} scan_input_t;

typedef struct {
	const char *p;
} str_scan_ctx_t;

typedef struct {
	FILE *stream;
} file_scan_ctx_t;

typedef enum {
	LN_NONE,
	LN_HH,
	LN_H,
	LN_L,
	LN_LL,
	LN_J,
	LN_Z,
	LN_T,
	LN_LONGDBL
} scan_length_t;

static int str_scan_getc(void *ctx) {
	str_scan_ctx_t *st = (str_scan_ctx_t *)ctx;
	if(*st->p == '\0') return EOF;
	return (unsigned char)*st->p++;
}

static void str_scan_ungetc(void *ctx, int c) {
	str_scan_ctx_t *st = (str_scan_ctx_t *)ctx;
	if(c != EOF && st->p) st->p--;
}

static int file_scan_getc(void *ctx) {
	file_scan_ctx_t *st = (file_scan_ctx_t *)ctx;
	return fgetc(st->stream);
}

static void file_scan_ungetc(void *ctx, int c) {
	file_scan_ctx_t *st = (file_scan_ctx_t *)ctx;
	if(c != EOF) ungetc(c, st->stream);
}

static int scan_getc(scan_input_t *in) {
	int c = in->getc_fn(in->ctx);
	if(c == EOF) {
		in->eof_seen = 1;
		return EOF;
	}
	in->chars_consumed++;
	return c;
}

static void scan_ungetc(scan_input_t *in, int c) {
	if(c == EOF) return;
	in->ungetc_fn(in->ctx, c);
	in->chars_consumed--;
	in->eof_seen = 0;
}

static void scan_skip_ws(scan_input_t *in) {
	int c;
	while((c = scan_getc(in)) != EOF) {
		if(!isspace((unsigned char)c)) {
			scan_ungetc(in, c);
			return;
		}
	}
}

static int is_hex_digit(int c) {
	return (c >= '0' && c <= '9') ||
		       (c >= 'a' && c <= 'f') ||
		       (c >= 'A' && c <= 'F');
}

static int is_oct_digit(int c) {
	return c >= '0' && c <= '7';
}

static int scan_decimal_float_token(scan_input_t *in, int width, char *buf, size_t bufsz, int *input_failure) {
	int idx = 0;
	int c;
	int saw_digit = 0;
	int saw_dot = 0;
	int saw_exp = 0;
	int exp_needs_digit = 0;
	int consumed = 0;

	*input_failure = 0;

	if(width <= 0) return 0;

	c = scan_getc(in);
	if(c == EOF) {
		*input_failure = 1;
		return 0;
	}

	if(c == '+' || c == '-') {
		if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
		width--;
		consumed = 1;
		if(width <= 0) {
			buf[idx] = '\0';
			return 0;
		}
		c = scan_getc(in);
		if(c == EOF) {
			buf[idx] = '\0';
			return 0;
		}
	}

	while(1) {
		if(c >= '0' && c <= '9') {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			saw_digit = 1;
			if(exp_needs_digit) exp_needs_digit = 0;
			consumed = 1;
		} else if(c == '.' && !saw_dot && !saw_exp) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			saw_dot = 1;
			consumed = 1;
		} else if((c == 'e' || c == 'E') && !saw_exp && saw_digit) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			saw_exp = 1;
			exp_needs_digit = 1;
			consumed = 1;
		} else if((c == '+' || c == '-') && saw_exp && exp_needs_digit) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			consumed = 1;
		} else {
			scan_ungetc(in, c);
			break;
		}

		width--;
		if(width <= 0) break;
		c = scan_getc(in);
		if(c == EOF) break;
	}

	if(exp_needs_digit && idx > 0) {
		int back = buf[idx - 1];
		if(back == '+' || back == '-') idx--;
		if(idx > 0 && (buf[idx - 1] == 'e' || buf[idx - 1] == 'E')) idx--;
	}

	buf[idx] = '\0';
	if(!saw_digit) {
		(void)consumed;
		return 0;
	}

	return 1;
}

/*
 * LIBC-09: combined float token for %a/%A.  These specifiers accept BOTH a
 * C99 hex float ("0x1.8p3") AND an ordinary decimal float ("0", "3.14",
 * "1e5"), exactly like strtod/strtof.  The previous scan_hex_float_token
 * consumed the leading '0' (and any sign) while probing for the "0x" prefix
 * and, on discovering no 'x', could only push a single character back — so
 * the '0' was lost and the decimal fallback then began at EOF, making
 * sscanf("0","%a",&f) return EOF instead of assigning 0.0.  Scanning both
 * forms in one pass removes the destructive lookahead.
 */
static int scan_float_token(scan_input_t *in, int width, char *buf, size_t bufsz, int *input_failure) {
	int idx = 0;
	int c;
	int saw_digit = 0;
	int saw_dot = 0;
	int saw_exp = 0;
	int exp_needs_digit = 0;
	int is_hex = 0;

	*input_failure = 0;

	if(width <= 0) return 0;

	c = scan_getc(in);
	if(c == EOF) {
		*input_failure = 1;
		return 0;
	}

	if(c == '+' || c == '-') {
		if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
		width--;
		if(width <= 0) {
			buf[idx] = '\0';
			return 0;
		}
		c = scan_getc(in);
		if(c == EOF) {
			buf[idx] = '\0';
			return 0;
		}
	}

	/* Optional 0x/0X hex-float prefix.  A leading '0' NOT followed by x/X is
	 * an ordinary decimal digit and must be kept, not discarded. */
	if(c == '0') {
		if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
		saw_digit = 1;
		width--;
		if(width <= 0) {
			buf[idx] = '\0';
			return 1;               /* a bare "0" */
		}
		c = scan_getc(in);
		if(c == 'x' || c == 'X') {
			is_hex = 1;
			saw_digit = 0;          /* still need a hex mantissa digit */
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			width--;
			if(width <= 0) {
				buf[idx] = '\0';
				return 0;           /* "0x" alone is invalid */
			}
			c = scan_getc(in);
			if(c == EOF) {
				buf[idx] = '\0';
				return 0;
			}
		} else if(c == EOF) {
			buf[idx] = '\0';
			return 1;               /* a bare "0" at end of input */
		}
		/* else: c holds the char following the leading '0'; fall through. */
	}

	for(;;) {
		int mant_digit = is_hex ? is_hex_digit(c) : (c >= '0' && c <= '9');
		int exp_marker = is_hex ? (c == 'p' || c == 'P') : (c == 'e' || c == 'E');

		if(mant_digit && !saw_exp) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			saw_digit = 1;
		} else if(c == '.' && !saw_dot && !saw_exp) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			saw_dot = 1;
		} else if(exp_marker && !saw_exp && saw_digit) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			saw_exp = 1;
			exp_needs_digit = 1;
		} else if((c == '+' || c == '-') && saw_exp && exp_needs_digit) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
		} else if(c >= '0' && c <= '9' && saw_exp) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			exp_needs_digit = 0;
		} else {
			scan_ungetc(in, c);
			break;
		}

		width--;
		if(width <= 0) break;
		c = scan_getc(in);
		if(c == EOF) break;
	}

	/* Drop a dangling exponent marker (and its sign) that never got a digit. */
	if(exp_needs_digit && idx > 0) {
		int back = buf[idx - 1];
		if(back == '+' || back == '-') idx--;
		if(idx > 0 && (buf[idx - 1] == 'e' || buf[idx - 1] == 'E' ||
		               buf[idx - 1] == 'p' || buf[idx - 1] == 'P')) idx--;
		saw_exp = 0;
	}

	buf[idx] = '\0';
	if(!saw_digit) return 0;
	if(is_hex && !saw_exp) return 0;    /* C99 hex floats require a p-exponent */
	return 1;
}

static int build_scanset(const char **fmtp, unsigned char table[256], int *negated) {
	const char *f = *fmtp;
	int have_char = 0;

	memset(table, 0, 256);
	*negated = 0;

	if(*f == '^') {
		*negated = 1;
		f++;
	}

	if(*f == ']') {
		table[(unsigned char)']'] = 1;
		have_char = 1;
		f++;
	}

	while(*f && *f != ']') {
		unsigned char a = (unsigned char)*f;
		if(f[1] == '-' && f[2] && f[2] != ']') {
			unsigned char b = (unsigned char)f[2];
			if(a <= b) {
				for(unsigned char c = a; c <= b; c++) table[c] = 1;
			} else {
				for(unsigned char c = b; c <= a; c++) table[c] = 1;
			}
			have_char = 1;
			f += 3;
			continue;
		}
		table[a] = 1;
		have_char = 1;
		f++;
	}

	if(*f == ']') f++;
	*fmtp = f;
	return have_char;
}

static int scan_integer_token(scan_input_t *in, int width, char spec, char *buf, size_t bufsz, int *input_failure) {
	int idx = 0;
	int c;
	int got_digit = 0;
	int base = 10;

	*input_failure = 0;

	if(width <= 0) return 0;

	c = scan_getc(in);
	if(c == EOF) {
		*input_failure = 1;
		return 0;
	}

	if(spec != 'p' && (c == '+' || c == '-')) {
		if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
		width--;
		if(width <= 0) {
			buf[idx] = '\0';
			return 0;
		}
		c = scan_getc(in);
		if(c == EOF) {
			buf[idx] = '\0';
			return 0;
		}
	}

	if(spec == 'd' || spec == 'u') {
		base = 10;
		if(!isdigit((unsigned char)c)) {
			scan_ungetc(in, c);
			buf[idx] = '\0';
			return 0;
		}
		while(width > 0 && c != EOF && isdigit((unsigned char)c)) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			got_digit = 1;
			width--;
			if(width <= 0) break;
			c = scan_getc(in);
		}
		if(c != EOF && !isdigit((unsigned char)c)) scan_ungetc(in, c);
	} else if(spec == 'o') {
		base = 8;
		if(!is_oct_digit(c)) {
			scan_ungetc(in, c);
			buf[idx] = '\0';
			return 0;
		}
		while(width > 0 && c != EOF && is_oct_digit(c)) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			got_digit = 1;
			width--;
			if(width <= 0) break;
			c = scan_getc(in);
		}
		if(c != EOF && !is_oct_digit(c)) scan_ungetc(in, c);
	} else if(spec == 'x' || spec == 'X' || spec == 'p') {
		base = 16;

		if(c == '0' && width > 1) {
			if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
			got_digit = 1;
			width--;
			c = scan_getc(in);
			if(c == 'x' || c == 'X') {
				if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
				got_digit = 0;
				width--;
				c = scan_getc(in);
			} else {
				scan_ungetc(in, c);
				c = EOF;
			}
		}

		if(c != EOF && is_hex_digit(c)) {
			while(width > 0 && c != EOF && is_hex_digit(c)) {
				if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
				got_digit = 1;
				width--;
				if(width <= 0) break;
				c = scan_getc(in);
			}
			if(c != EOF && !is_hex_digit(c)) scan_ungetc(in, c);
		} else if(c != EOF && !got_digit) {
			scan_ungetc(in, c);
		}
	} else {
		/* %i */
		base = 0;
		if(c == '0') {
			if((size_t)idx + 1 < bufsz) buf[idx++] = '0';
			got_digit = 1;
			width--;
			if(width > 0) {
				c = scan_getc(in);
				if(c == 'x' || c == 'X') {
					if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
					got_digit = 0;
					width--;
					if(width > 0) {
						c = scan_getc(in);
						while(width > 0 && c != EOF && is_hex_digit(c)) {
							if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
							got_digit = 1;
							width--;
							if(width <= 0) break;
							c = scan_getc(in);
						}
						if(c != EOF && !is_hex_digit(c)) scan_ungetc(in, c);
					}
				} else {
					if(c != EOF && is_oct_digit(c)) {
						while(width > 0 && c != EOF && is_oct_digit(c)) {
							if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
							got_digit = 1;
							width--;
							if(width <= 0) break;
							c = scan_getc(in);
						}
						if(c != EOF && !is_oct_digit(c)) scan_ungetc(in, c);
					} else if(c != EOF) {
						scan_ungetc(in, c);
					}
				}
			}
		} else if(c >= '1' && c <= '9') {
			while(width > 0 && c != EOF && isdigit((unsigned char)c)) {
				if((size_t)idx + 1 < bufsz) buf[idx++] = (char)c;
				got_digit = 1;
				width--;
				if(width <= 0) break;
				c = scan_getc(in);
			}
			if(c != EOF && !isdigit((unsigned char)c)) scan_ungetc(in, c);
		} else {
			scan_ungetc(in, c);
			buf[idx] = '\0';
			return 0;
		}
	}

	(void)base;
	buf[idx] = '\0';
	return got_digit;
}

static int scan_core(scan_input_t *in, const char *format, va_list ap) {
	const char *f = format;
	int assigned = 0;
	int c;

	while(*f) {
		if(isspace((unsigned char)*f)) {
			while(isspace((unsigned char)*f)) f++;
			scan_skip_ws(in);
			continue;
		}

		if(*f != '%') {
			c = scan_getc(in);
			if(c == EOF) return assigned == 0 ? EOF : assigned;
			if(c != (unsigned char)*f) {
				scan_ungetc(in, c);
				return assigned;
			}
			f++;
			continue;
		}

		f++;
		if(*f == '%') {
			c = scan_getc(in);
			if(c == EOF) return assigned == 0 ? EOF : assigned;
			if(c != '%') {
				scan_ungetc(in, c);
				return assigned;
			}
			f++;
			continue;
		}

		int suppress = 0;
		int width = 0;
		scan_length_t length = LN_NONE;
		char spec;

		if(*f == '*') {
			suppress = 1;
			f++;
		}

		while(*f >= '0' && *f <= '9') {
			width = width * 10 + (*f - '0');
			f++;
		}
		if(width == 0) width = INT_MAX;

		if(*f == 'h') {
			f++;
			if(*f == 'h') {
				length = LN_HH;
				f++;
			} else {
				length = LN_H;
			}
		} else if(*f == 'l') {
			f++;
			if(*f == 'l') {
				length = LN_LL;
				f++;
			} else {
				length = LN_L;
			}
		} else if(*f == 'j') {
			length = LN_J;
			f++;
		} else if(*f == 'z') {
			length = LN_Z;
			f++;
		} else if(*f == 't') {
			length = LN_T;
			f++;
		} else if(*f == 'L') {
			length = LN_LONGDBL;
			f++;
		}

		spec = *f;
		if(spec == '\0') return assigned;
		f++;

		if(spec != 'c' && spec != '[' && spec != 'n') {
			scan_skip_ws(in);
		}

		switch(spec) {
		case 'd':
		case 'i':
		case 'u':
		case 'o':
		case 'x':
		case 'X':
		case 'p': {
			char ibuf[256];
			char *end;
			int input_failure = 0;
			int ok = scan_integer_token(in, width, spec, ibuf, sizeof(ibuf), &input_failure);

			if(input_failure) return assigned == 0 ? EOF : assigned;
			if(!ok) return assigned;

			if(!suppress) {
				if(spec == 'd' || spec == 'i') {
					long long sval = strtoll(ibuf, &end, (spec == 'd') ? 10 : 0);
					if(end == ibuf) return assigned;

					if(length == LN_HH) {
						signed char *p = va_arg(ap, signed char *);
						*p = (signed char)sval;
					} else if(length == LN_H) {
						short *p = va_arg(ap, short *);
						*p = (short)sval;
					} else if(length == LN_L) {
						long *p = va_arg(ap, long *);
						*p = (long)sval;
					} else if(length == LN_LL) {
						long long *p = va_arg(ap, long long *);
						*p = sval;
					} else if(length == LN_J) {
						intmax_t *p = va_arg(ap, intmax_t *);
						*p = (intmax_t)sval;
					} else if(length == LN_Z) {
						ssize_t *p = va_arg(ap, ssize_t *);
						*p = (ssize_t)sval;
					} else if(length == LN_T) {
						ptrdiff_t *p = va_arg(ap, ptrdiff_t *);
						*p = (ptrdiff_t)sval;
					} else {
						int *p = va_arg(ap, int *);
						*p = (int)sval;
					}
					assigned++;
				} else {
					int base = 10;
					if(spec == 'o') base = 8;
					if(spec == 'x' || spec == 'X' || spec == 'p') base = 16;
					unsigned long long uval = strtoull(ibuf, &end, base);
					if(end == ibuf) return assigned;

					if(spec == 'p') {
						void **p = va_arg(ap, void **);
						*p = (void *)(uintptr_t)uval;
					} else if(length == LN_HH) {
						unsigned char *p = va_arg(ap, unsigned char *);
						*p = (unsigned char)uval;
					} else if(length == LN_H) {
						unsigned short *p = va_arg(ap, unsigned short *);
						*p = (unsigned short)uval;
					} else if(length == LN_L) {
						unsigned long *p = va_arg(ap, unsigned long *);
						*p = (unsigned long)uval;
					} else if(length == LN_LL) {
						unsigned long long *p = va_arg(ap, unsigned long long *);
						*p = uval;
					} else if(length == LN_J) {
						uintmax_t *p = va_arg(ap, uintmax_t *);
						*p = (uintmax_t)uval;
					} else if(length == LN_Z) {
						size_t *p = va_arg(ap, size_t *);
						*p = (size_t)uval;
					} else if(length == LN_T) {
						ptrdiff_t *p = va_arg(ap, ptrdiff_t *);
						*p = (ptrdiff_t)uval;
					} else {
						unsigned int *p = va_arg(ap, unsigned int *);
						*p = (unsigned int)uval;
					}
					assigned++;
				}
			}
			break;
		}

		case 'f':
		case 'F':
		case 'e':
		case 'E':
		case 'g':
		case 'G':
		case 'a':
		case 'A': {
			char fbuf[256];
			int input_failure = 0;
			int ok;

			if(spec == 'a' || spec == 'A') {
				/* LIBC-09: one pass accepts both hex and decimal floats. */
				ok = scan_float_token(in, width, fbuf, sizeof(fbuf), &input_failure);
			} else {
				ok = scan_decimal_float_token(in, width, fbuf, sizeof(fbuf), &input_failure);
			}

			if(input_failure) return assigned == 0 ? EOF : assigned;
			if(!ok) return assigned;

			if(!suppress) {
				if(length == LN_LONGDBL) {
					long double *p = va_arg(ap, long double *);
					*p = strtold(fbuf, NULL);
				} else if(length == LN_L) {
					double *p = va_arg(ap, double *);
					*p = strtod(fbuf, NULL);
				} else {
					float *p = va_arg(ap, float *);
					*p = strtof(fbuf, NULL);
				}
				assigned++;
			}
			break;
		}

		case 'c': {
			int count = (width == INT_MAX) ? 1 : width;
			char *dest = suppress ? NULL : va_arg(ap, char *);
			int i;

			for(i = 0; i < count; i++) {
				c = scan_getc(in);
				if(c == EOF) break;
				if(dest) dest[i] = (char)c;
			}

			if(i == 0) return assigned == 0 ? EOF : assigned;
			if(!suppress) assigned++;
			break;
		}

		case 's': {
			char *dest = suppress ? NULL : va_arg(ap, char *);
			int i = 0;

			c = scan_getc(in);
			if(c == EOF) return assigned == 0 ? EOF : assigned;
			if(isspace((unsigned char)c)) {
				scan_ungetc(in, c);
				return assigned;
			}

			while(i < width) {
				if(c == EOF || isspace((unsigned char)c)) {
					if(c != EOF) scan_ungetc(in, c);
					break;
				}
				if(dest) dest[i] = (char)c;
				i++;
				if(i >= width) break;
				c = scan_getc(in);
			}

			if(i == 0) return assigned;
			if(dest) dest[i] = '\0';
			if(!suppress) assigned++;
			break;
		}

		case '[': {
			unsigned char table[256];
			int negated;
			char *dest = suppress ? NULL : va_arg(ap, char *);
			int i = 0;
			int have_set = build_scanset(&f, table, &negated);

			if(!have_set) return assigned;

			while(i < width) {
				int accept;
				c = scan_getc(in);
				if(c == EOF) break;
				accept = table[(unsigned char)c] ? 1 : 0;
				if(negated) accept = !accept;
				if(!accept) {
					scan_ungetc(in, c);
					break;
				}
				if(dest) dest[i] = (char)c;
				i++;
			}

			if(i == 0) return assigned == 0 && in->eof_seen ? EOF : assigned;
			if(dest) dest[i] = '\0';
			if(!suppress) assigned++;
			break;
		}

		case 'n': {
			if(!suppress) {
				if(length == LN_HH) {
					signed char *p = va_arg(ap, signed char *);
					*p = (signed char)in->chars_consumed;
				} else if(length == LN_H) {
					short *p = va_arg(ap, short *);
					*p = (short)in->chars_consumed;
				} else if(length == LN_L) {
					long *p = va_arg(ap, long *);
					*p = (long)in->chars_consumed;
				} else if(length == LN_LL) {
					long long *p = va_arg(ap, long long *);
					*p = (long long)in->chars_consumed;
				} else if(length == LN_J) {
					intmax_t *p = va_arg(ap, intmax_t *);
					*p = (intmax_t)in->chars_consumed;
				} else if(length == LN_Z) {
					size_t *p = va_arg(ap, size_t *);
					*p = (size_t)in->chars_consumed;
				} else if(length == LN_T) {
					ptrdiff_t *p = va_arg(ap, ptrdiff_t *);
					*p = (ptrdiff_t)in->chars_consumed;
				} else {
					int *p = va_arg(ap, int *);
					*p = in->chars_consumed;
				}
			}
			break;
		}

		default:
			return assigned;
		}
	}

	return assigned;
}

int vsscanf(const char *str, const char *format, va_list ap) {
	str_scan_ctx_t ctx;
	scan_input_t in;

	ctx.p = str ? str : "";
	in.getc_fn = str_scan_getc;
	in.ungetc_fn = str_scan_ungetc;
	in.ctx = &ctx;
	in.chars_consumed = 0;
	in.eof_seen = 0;

	return scan_core(&in, format, ap);
}

int vfscanf(FILE *stream, const char *format, va_list ap) {
	file_scan_ctx_t ctx;
	scan_input_t in;

	ctx.stream = stream;
	in.getc_fn = file_scan_getc;
	in.ungetc_fn = file_scan_ungetc;
	in.ctx = &ctx;
	in.chars_consumed = 0;
	in.eof_seen = 0;

	return scan_core(&in, format, ap);
}

int fscanf(FILE *stream, const char *format, ...) {
	va_list ap;
	int ret;
	va_start(ap, format);
	ret = vfscanf(stream, format, ap);
	va_end(ap);
	return ret;
}

int scanf(const char *format, ...) {
	va_list ap;
	int ret;
	va_start(ap, format);
	ret = vfscanf(stdin, format, ap);
	va_end(ap);
	return ret;
}

int vscanf(const char *format, va_list ap) {
	return vfscanf(stdin, format, ap);
}

int sscanf(const char *str, const char *format, ...) {
	va_list ap;
	int ret;
	va_start(ap, format);
	ret = vsscanf(str, format, ap);
	va_end(ap);
	return ret;
}
