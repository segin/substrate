#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <demangle.h>
#include <demangle_internal.h>

#define RUST_RECURSION_DEFAULT_LIMIT 128

typedef demangle_buf_t rust_buf_t;

typedef struct rust_parser {
	const char *input;
	const char *cur;
	int options;

	int recursion_depth;
	int recursion_limit;

	/* the path being printed is a value's, whose generic arguments take
	 * the turbofish, f::<u8>, not a type's, Vec<u8> */
	int in_value;

	/* the path printed last ended in generic arguments, Foo<u8>, which a
	 * dyn trait's associated-type bindings join: Foo<u8, Item = i32> */
	int path_generic;

	/* lifetimes bound by the binders, for<'a>, of the types around */
	size_t bound_lifetimes;

	rust_buf_t out;
} rust_parser_t;

typedef struct rust_mark {
	const char *cur;
	size_t out_len;
} rust_mark_t;

static int starts_with(const char *s, const char *prefix);
static int is_hex_char(char ch);
static int rust_is_v0(const char *mangled);
static int rust_is_legacy(const char *mangled);

static int rust_buf_append_base10(rust_buf_t *buf, uint64_t v);

static int rust_parser_init(rust_parser_t *p, const char *mangled, int options);
static void rust_parser_destroy(rust_parser_t *p);
static int rust_parser_enter(rust_parser_t *p);
static void rust_parser_leave(rust_parser_t *p);
static int rust_parser_resolve_backref(rust_parser_t *p, size_t off, const char **target);

static void rust_mark_save(rust_parser_t *p, rust_mark_t *m);
static void rust_mark_restore(rust_parser_t *p, const rust_mark_t *m);

static int rust_parse_decimal(rust_parser_t *p, size_t *out);
static int rust_parse_base62(rust_parser_t *p, size_t *out);
static int rust_parse_optional_disambiguator(rust_parser_t *p, int *present, uint64_t *value);
static int rust_append_unicode_identifier(rust_parser_t *p, const char *bytes, size_t len);
static int rust_parse_v0_identifier(rust_parser_t *p);
static int rust_parse_v0_type(rust_parser_t *p);
static int rust_parse_v0_type_path(rust_parser_t *p);
static int rust_parse_v0_const(rust_parser_t *p);
static int rust_parse_v0_const_int(rust_parser_t *p, char type_tag);
static int rust_parse_v0_lifetime(rust_parser_t *p, int display);
static int rust_append_lifetime(rust_parser_t *p, size_t idx);
static int rust_parse_v0_binder(rust_parser_t *p);
static int rust_parse_v0_dyn_trait(rust_parser_t *p);
static int rust_parse_v0_generic_arg(rust_parser_t *p);
static int rust_parse_v0_path(rust_parser_t *p);

static int rust_parse_v0_symbol(rust_parser_t *p);
static char *rust_demangle_v0(const char *mangled, int options);
static int rust_legacy_process_component(const char *part, size_t len, int is_first, rust_buf_t *out);
static char *rust_demangle_legacy(const char *mangled, int options);
static int rust_parse_decimal_span(const char **cur, size_t *out);
static int rust_legacy_is_hash_component(const char *s, size_t len);
static int rust_legacy_decode_token(const char *tok, size_t tok_len, rust_buf_t *out);
static int rust_legacy_decode_component(const char *s, size_t len, rust_buf_t *out);
static int rust_has_n_bytes(const char *s, size_t n);
static int rust_utf8_append_codepoint(rust_buf_t *buf, uint32_t cp);
static int rust_puny_decode(const char *in, size_t in_len, rust_buf_t *out, int *non_ascii);

static int starts_with(const char *s, const char *prefix) {
	size_t n;

	if(s == NULL || prefix == NULL) {
		return 0;
	}

	n = strlen(prefix);
	return strncmp(s, prefix, n) == 0;
}

static int is_hex_char(char ch) {
	return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

static int rust_is_v0(const char *mangled) {
	return starts_with(mangled, "_R");
}

static int rust_is_legacy(const char *mangled) {
	size_t len;
	size_t i;

	if(!starts_with(mangled, "_ZN")) {
		return 0;
	}

	len = strlen(mangled);
	if(len < 24u || mangled[len - 1u] != 'E') {
		return 0;
	}

	if(mangled[len - 20u] != '1' || mangled[len - 19u] != '7' || mangled[len - 18u] != 'h') {
		return 0;
	}

	for(i = len - 17u; i < len - 1u; i++) {
		if(!is_hex_char(mangled[i])) {
			return 0;
		}
	}

	return 1;
}

static int rust_buf_append_base10(rust_buf_t *buf, uint64_t v) {
	char numbuf[32];
	size_t n = 0u;

	if(v == 0u) {
		numbuf[n++] = '0';
	} else {
		char rev[32];
		size_t r = 0u;
		while(v > 0u && r < sizeof(rev)) {
			rev[r++] = (char)('0' + (v % 10u));
			v /= 10u;
		}
		while(r > 0u) {
			numbuf[n++] = rev[--r];
		}
	}

	return demangle_buf_append(buf, numbuf, n);
}

static int rust_parser_init(rust_parser_t *p, const char *mangled, int options) {
	memset(p, 0, sizeof(*p));
	p->input = mangled;
	p->cur = mangled;
	p->options = options;
	p->recursion_limit = (options & DEMANGLE_NO_RECURSE_LIMIT) != 0 ? INT_MAX : RUST_RECURSION_DEFAULT_LIMIT;

	if(demangle_buf_reserve(&p->out, 0u) != 0) {
		return -1;
	}

	p->out.data[0] = '\0';
	return 0;
}

static void rust_parser_destroy(rust_parser_t *p) {
	if(p == NULL) {
		return;
	}

	demangle_buf_destroy(&p->out);
}

static int rust_parser_enter(rust_parser_t *p) {
	if(p->recursion_depth >= p->recursion_limit) {
		return -1;
	}

	p->recursion_depth++;
	return 0;
}

static void rust_parser_leave(rust_parser_t *p) {
	if(p->recursion_depth > 0) {
		p->recursion_depth--;
	}
}

static int rust_parser_resolve_backref(rust_parser_t *p, size_t off, const char **target) {
	const char *base;
	size_t len;

	if(p == NULL || target == NULL || p->input == NULL) {
		return -1;
	}

	base = p->input;
	if(starts_with(base, "_R")) {
		base += 2;
	}

	len = strlen(base);
	if(off > len) {
		return -1;
	}

	*target = base + off;
	return 0;
}

static void rust_mark_save(rust_parser_t *p, rust_mark_t *m) {
	m->cur = p->cur;
	m->out_len = p->out.len;
}

static void rust_mark_restore(rust_parser_t *p, const rust_mark_t *m) {
	p->cur = m->cur;
	if(m->out_len <= p->out.len) {
		p->out.len = m->out_len;
		p->out.data[p->out.len] = '\0';
	}
}

static int rust_parse_decimal(rust_parser_t *p, size_t *out) {
	size_t v;

	if(p == NULL || out == NULL || !isdigit((unsigned char)p->cur[0])) {
		return -1;
	}

	v = 0u;
	while(isdigit((unsigned char)p->cur[0])) {
		unsigned d = (unsigned)(p->cur[0] - '0');
		/* Pre-multiply saturation; see rust_parse_base62 / itanium. */
		if(v > ((size_t)-1 - d) / 10u) {
			return -1;
		}
		v = v * 10u + d;
		p->cur++;
	}

	*out = v;
	return 0;
}

static int rust_parse_base62(rust_parser_t *p, size_t *out) {
	size_t v;

	if(p == NULL || out == NULL) {
		return -1;
	}

	if(p->cur[0] == '_') {
		p->cur++;
		*out = 0u;
		return 0;
	}

	v = 0u;
	while(p->cur[0] != '\0' && p->cur[0] != '_') {
		unsigned d;

		if(p->cur[0] >= '0' && p->cur[0] <= '9') {
			d = (unsigned)(p->cur[0] - '0');
		} else if(p->cur[0] >= 'a' && p->cur[0] <= 'z') {
			d = 10u + (unsigned)(p->cur[0] - 'a');
		} else if(p->cur[0] >= 'A' && p->cur[0] <= 'Z') {
			d = 36u + (unsigned)(p->cur[0] - 'A');
		} else {
			return -1;
		}

		/* Pre-multiply saturation.  The post-multiply (nv < v) test
		 * misses overflows where v lands in [SIZE_MAX/62, SIZE_MAX/61): * v*62 wraps but the wrapped sum still exceeds v.  Reject
		 * before the wrap. */
		if(v > ((size_t)-1 - d) / 62u) {
			return -1;
		}
		v = v * 62u + d;
		p->cur++;
	}

	if(p->cur[0] != '_') {
		return -1;
	}

	p->cur++;
	/* Reject final-step overflow on `v + 1u`.  Combined with the
	 * pre-multiply check above this means a hostile mangled name
	 * can no longer roll v over to 0 here. */
	if(v == (size_t)-1) {
		return -1;
	}
	*out = v + 1u;
	return 0;
}

/*
 * [<disambiguator>] = "s" <base-62-number>; *value gets the number, s_ 0,
 * s0_ 1, and so on.  It is 64 bits wide -- a crate's is a hash, which
 * overflowed a size_t on i386 and had every crate refused -- and it is no
 * part of the name: it was printed, {s7577941804541299114}lib for the
 * crate lib.  A malformed one is an error, not an identifier.
 */
static int rust_parse_optional_disambiguator(rust_parser_t *p, int *present, uint64_t *value) {
	uint64_t v = 0u;

	if(present != NULL) {
		*present = 0;
	}
	if(value != NULL) {
		*value = 0u;
	}

	if(p->cur[0] != 's') {
		return 0;
	}
	p->cur++;

	if(p->cur[0] != '_') {
		while(p->cur[0] != '_') {
			unsigned d;

			if(p->cur[0] >= '0' && p->cur[0] <= '9') {
				d = (unsigned)(p->cur[0] - '0');
			} else if(p->cur[0] >= 'a' && p->cur[0] <= 'z') {
				d = 10u + (unsigned)(p->cur[0] - 'a');
			} else if(p->cur[0] >= 'A' && p->cur[0] <= 'Z') {
				d = 36u + (unsigned)(p->cur[0] - 'A');
			} else {
				return -1;
			}
			if(v > (UINT64_MAX - d) / 62u) {
				return -1;
			}
			v = v * 62u + d;
			p->cur++;
		}
		/* the number is one more than its digits, and a disambiguator
		 * one more again (rustc-demangle's, GNU's): 2^64 - 3 at most */
		if(v > UINT64_MAX - 2u) {
			return -1;
		}
		v++;
	}
	p->cur++; /* the '_' */

	if(present != NULL) {
		*present = 1;
	}
	if(value != NULL) {
		*value = v;
	}
	return 0;
}

static int rust_has_n_bytes(const char *s, size_t n) {
	size_t i;

	for(i = 0u; i < n; i++) {
		if(s[i] == '\0') {
			return 0;
		}
	}

	return 1;
}

static int rust_utf8_append_codepoint(rust_buf_t *buf, uint32_t cp) {
	char tmp[4];
	size_t n;

	/* a UTF-16 surrogate is no character: as UTF-8 it is ill-formed */
	if(cp >= 0xD800u && cp <= 0xDFFFu) {
		return -1;
	}
	if(cp <= 0x7Fu) {
		tmp[0] = (char)cp;
		n = 1u;
	} else if(cp <= 0x7FFu) {
		tmp[0] = (char)(0xC0u | (cp >> 6));
		tmp[1] = (char)(0x80u | (cp & 0x3Fu));
		n = 2u;
	} else if(cp <= 0xFFFFu) {
		tmp[0] = (char)(0xE0u | (cp >> 12));
		tmp[1] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
		tmp[2] = (char)(0x80u | (cp & 0x3Fu));
		n = 3u;
	} else if(cp <= 0x10FFFFu) {
		tmp[0] = (char)(0xF0u | (cp >> 18));
		tmp[1] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
		tmp[2] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
		tmp[3] = (char)(0x80u | (cp & 0x3Fu));
		n = 4u;
	} else {
		return -1;
	}

	return demangle_buf_append(buf, tmp, n);
}

static unsigned rust_puny_digit(char ch) {
	if(ch >= 'a' && ch <= 'z') {
		return (unsigned)(ch - 'a');
	}
	if(ch >= 'A' && ch <= 'Z') {
		return (unsigned)(ch - 'A');
	}
	if(ch >= '0' && ch <= '9') {
		return 26u + (unsigned)(ch - '0');
	}
	return 0xFFFFFFFFu;
}

static int rust_puny_insert(uint32_t **cps, size_t *cp_count, size_t *cp_cap, size_t ins, uint32_t val) {
	if(*cp_count == *cp_cap) {
		size_t next_cap = (*cp_cap == 0u) ? 16u : *cp_cap * 2u;
		uint32_t *next = (uint32_t *)realloc(*cps, next_cap * sizeof(*next));
		if(next == NULL) {
			return -1;
		}
		*cps = next;
		*cp_cap = next_cap;
	}

	if(ins < *cp_count) {
		memmove(&(*cps)[ins + 1u], &(*cps)[ins], (*cp_count - ins) * sizeof(**cps));
	}
	(*cps)[ins] = val;
	(*cp_count)++;
	return 0;
}

static unsigned rust_puny_adapt(unsigned delta, unsigned numpoints, int first_time) {
	unsigned k;

	delta = first_time ? (delta / 700u) : (delta / 2u);
	delta += delta / numpoints;

	k = 0u;
	while(delta > 455u) {
		delta /= 35u;
		k += 36u;
	}

	return k + (36u * delta) / (delta + 38u);
}

static int rust_puny_decode(const char *in, size_t in_len, rust_buf_t *out, int *non_ascii) {
	uint32_t *cps;
	size_t cp_count;
	size_t cp_cap;
	size_t idx;
	size_t basic_end;
	unsigned n;
	unsigned i;
	unsigned bias;

	if(in == NULL || out == NULL || non_ascii == NULL) {
		return -1;
	}

	cps = NULL;
	cp_count = 0u;
	cp_cap = 0u;
	*non_ascii = 0;

	basic_end = 0u;
	for(idx = 0u; idx < in_len; idx++) {
		if(in[idx] == '-') {
			basic_end = idx + 1u;
		}
	}

	for(idx = 0u; idx < basic_end; idx++) {
		uint32_t cp;
		if(in[idx] == '-') {
			continue;
		}
		cp = (uint32_t)(unsigned char)in[idx];
		if(rust_puny_insert(&cps, &cp_count, &cp_cap, cp_count, cp) != 0) {
			free(cps);
			return -1;
		}
	}

	n = 128u;
	i = 0u;
	bias = 72u;
	idx = basic_end;

	while(idx < in_len) {
		unsigned oldi;
		unsigned w;
		unsigned k;
		size_t ins;

		oldi = i;
		w = 1u;
		for(k = 36u;; k += 36u) {
			unsigned digit;
			unsigned t;
			unsigned step;

			if(idx >= in_len) {
				free(cps);
				return -1;
			}

			digit = rust_puny_digit(in[idx++]);
			if(digit >= 36u) {
				free(cps);
				return -1;
			}

			/*
			 * Punycode accumulation is i += digit * w. Guard the multiply and
			 * add together by bounding digit against the remaining headroom.
			 */
			if(digit > (UINT32_MAX - i) / w) {
				free(cps);
				return -1;
			}
			i += digit * w;

			if(k <= bias + 1u) {
				t = 1u;
			} else if(k >= bias + 26u) {
				t = 26u;
			} else {
				t = k - bias;
			}

			if(digit < t) {
				break;
			}

			step = 36u - t;
			/* w *= step for the next digit position; keep it in uint32_t. */
			if(w > UINT32_MAX / step) {
				free(cps);
				return -1;
			}
			w *= step;
		}

		bias = rust_puny_adapt(i - oldi, (unsigned)(cp_count + 1u), oldi == 0u);
		if(i / (cp_count + 1u) > UINT32_MAX - n) {
			free(cps);
			return -1;
		}

		n += i / (unsigned)(cp_count + 1u);
		ins = i % (unsigned)(cp_count + 1u);
		i = (unsigned)ins;

		if(rust_puny_insert(&cps, &cp_count, &cp_cap, ins, n) != 0) {
			free(cps);
			return -1;
		}
		i++;
	}

	for(idx = 0u; idx < cp_count; idx++) {
		if(cps[idx] > 0x7Fu) {
			*non_ascii = 1;
		}
		if(rust_utf8_append_codepoint(out, cps[idx]) != 0) {
			free(cps);
			return -1;
		}
	}

	free(cps);
	return 0;
}

static int rust_append_unicode_identifier(rust_parser_t *p, const char *bytes, size_t len) {
	rust_buf_t decoded;
	int non_ascii;

	memset(&decoded, 0, sizeof(decoded));
	if(rust_puny_decode(bytes, len, &decoded, &non_ascii) != 0) {
		demangle_buf_destroy(&decoded);
		return -1;
	}

	if(non_ascii && demangle_buf_appendc(&p->out, '{') != 0) {
		demangle_buf_destroy(&decoded);
		return -1;
	}
	if(decoded.len > 0u && demangle_buf_append(&p->out, decoded.data, decoded.len) != 0) {
		demangle_buf_destroy(&decoded);
		return -1;
	}
	if(non_ascii && demangle_buf_appendc(&p->out, '}') != 0) {
		demangle_buf_destroy(&decoded);
		return -1;
	}
	demangle_buf_destroy(&decoded);

	return 0;
}

static int rust_parse_v0_identifier(rust_parser_t *p) {
	const char *bytes;
	size_t len;
	int is_unicode;
	int had_sep;
	int need_sep;

	if(p->cur[0] == 'u') {
		is_unicode = 1;
		p->cur++;
	} else {
		is_unicode = 0;
	}

	if(rust_parse_decimal(p, &len) != 0) {
		return -1;
	}

	had_sep = 0;
	if(p->cur[0] == '_') {
		had_sep = 1;
		p->cur++;
	}

	if(len == 0u || !rust_has_n_bytes(p->cur, len)) {
		return -1;
	}

	bytes = p->cur;
	need_sep = (bytes[0] == '_' || isdigit((unsigned char)bytes[0]));
	if((need_sep && !had_sep) || (!need_sep && had_sep)) {
		return -1;
	}

	if(is_unicode) {
		if(rust_append_unicode_identifier(p, bytes, len) != 0) {
			return -1;
		}
	} else if(demangle_buf_append(&p->out, bytes, len) != 0) {
		return -1;
	}

	p->cur = bytes + len;
	return 0;
}

static int rust_parse_v0_lifetime(rust_parser_t *p, int display) {
	size_t idx;

	if(p->cur[0] != 'L') {
		return -1;
	}

	p->cur++;
	if(rust_parse_base62(p, &idx) != 0) {
		return -1;
	}

	/* a lifetime bound by no binder is no lifetime (rustc-demangle;
	 * GNU c++filt prints '_18446744073709551615) */
	if(idx > p->bound_lifetimes) {
		return -1;
	}
	if(!display) {
		return 0;
	}
	return rust_append_lifetime(p, idx);
}

/*
 * The lifetime @p idx, a de Bruijn index: 0 is the erased '_, 1 the one
 * the innermost binder bound last, and so on outwards.  They are named by
 * depth, from the outermost binder's first, 'a, as rustc-demangle and GNU
 * name them; past 'z, '_26.  This named index 1 'a wherever it was --
 * fn(&'a u8, &'a u8) for for<'a, 'b> fn(&'b u8, &'a u8).
 */
static int rust_append_lifetime(rust_parser_t *p, size_t idx) {
	size_t depth;

	if(demangle_buf_appendc(&p->out, '\'') != 0) {
		return -1;
	}
	if(idx == 0u) {
		return demangle_buf_appendc(&p->out, '_');
	}
	depth = p->bound_lifetimes - idx;
	if(depth < 26u) {
		return demangle_buf_appendc(&p->out, (char)('a' + depth));
	}
	return demangle_buf_appendc(&p->out, '_') != 0 ? -1 : rust_buf_append_base10(&p->out, depth);
}

/*
 * [<binder>] = "G" <base-62-number>: for<'a, 'b> before a function or dyn
 * type, binding one lifetime more than its number.  They are counted in
 * bound_lifetimes, which rust_parse_v0_type() puts back when the type
 * ends.  The binder was read and dropped.
 */
static int rust_parse_v0_binder(rust_parser_t *p) {
	size_t n;
	size_t i;

	if(p->cur[0] != 'G') {
		return 0;
	}
	p->cur++;
	/* no more than the symbol is long: a short one must not print
	 * for<'a, ... '_4294967295> */
	if(rust_parse_base62(p, &n) != 0 || n >= strlen(p->input) || p->bound_lifetimes > strlen(p->input)) {
		return -1;
	}
	n++;
	if(demangle_buf_append(&p->out, "for<", 4u) != 0) {
		return -1;
	}
	for(i = 0u; i < n; i++) {
		if(i > 0u && demangle_buf_append(&p->out, ", ", 2u) != 0) {
			return -1;
		}
		p->bound_lifetimes++;
		if(rust_append_lifetime(p, 1u) != 0) {
			return -1;
		}
	}
	return demangle_buf_append(&p->out, "> ", 2u);
}

static int rust_parse_v0_const(rust_parser_t *p) {
	rust_mark_t m;
	size_t value_off;
	size_t idx;
	char type_tag;

	if(p->cur[0] == 'p') {
		p->cur++;
		return demangle_buf_appendc(&p->out, '_');
	}

	if(p->cur[0] == 'B') {
		const char *backref_start = p->cur;
		const char *save_cur;
		const char *target;
		rust_mark_t mark;

		p->cur++;
		if(rust_parse_base62(p, &idx) != 0) {
			return -1;
		}

		save_cur = p->cur;
		/* only to what came before this B: one at the B itself, or past
		 * it, would refer back to itself until the stack ran out */
		if(rust_parser_resolve_backref(p, idx, &target) != 0 || target >= backref_start) {
			return -1;
		}

		rust_mark_save(p, &mark);
		if(rust_parser_enter(p) != 0) {
			return -1;
		}
		p->cur = target;
		if(rust_parse_v0_const(p) != 0) {
			rust_mark_restore(p, &mark);
			rust_parser_leave(p);
			return -1;
		}
		rust_parser_leave(p);
		p->cur = save_cur;
		return 0;
	}

	rust_mark_save(p, &m);
	type_tag = p->cur[0];
	value_off = p->out.len;
	if(rust_parse_v0_type(p) != 0) {
		return -1;
	}
	p->out.len = value_off;
	p->out.data[p->out.len] = '\0';

	if(rust_parse_v0_const_int(p, type_tag) != 0) {
		rust_mark_restore(p, &m);
		return -1;
	}

	return 0;
}

/*
 * <const-data> = ["n"] {<hex-digit>} "_", read as its type says, and
 * printed as GNU c++filt prints it: an integer in decimal (in hex, 0x...,
 * only past 64 bits), signed ones alone taking the n; a bool true or false
 * from 1 or 0; a char quoted, with \t \r \n escaped, the rest of printable
 * ASCII as it is (' and \ too, as GNU has them), and anything else as
 * \u{e9}.  This printed every integer in
 * hex, [u8; 0x5]; took 0_ and 1_ for false and true whatever the type,
 * so a usize 0 was false; let any type be negative; and escaped chars
 * its own way.
 */
static int rust_parse_v0_const_int(rust_parser_t *p, char type_tag) {
	const char *digits;
	size_t digits_len;
	uint64_t v = 0u;
	int negative = 0;
	int wide = 0;
	int is_signed = (strchr("aslxni", type_tag) != NULL);

	if(type_tag == '\0' || strchr("bchtmyojaslxni", type_tag) == NULL) {
		return -1;
	}
	if(p->cur[0] == 'n') {
		if(!is_signed) {
			return -1;
		}
		negative = 1;
		p->cur++;
	}
	digits = p->cur;
	while((p->cur[0] >= '0' && p->cur[0] <= '9') || (p->cur[0] >= 'a' && p->cur[0] <= 'f')) {
		unsigned d = (p->cur[0] <= '9') ? (unsigned)(p->cur[0] - '0') : 10u + (unsigned)(p->cur[0] - 'a');

		if(v > (UINT64_MAX >> 4)) {
			wide = 1;
		}
		v = (v << 4) | d;
		p->cur++;
	}
	digits_len = (size_t)(p->cur - digits);
	if(p->cur[0] != '_') {
		return -1;
	}
	p->cur++;

	if(type_tag == 'b') {
		if(wide || v > 1u) {
			return -1;
		}
		return v != 0u ? demangle_buf_append(&p->out, "true", 4u) : demangle_buf_append(&p->out, "false", 5u);
	}
	if(type_tag == 'c') {
		char esc[16];
		int n;

		if(wide || v > 0x10FFFFu || (v >= 0xD800u && v <= 0xDFFFu)) {
			return -1;
		}
		switch(v) {
		case '\t': n = snprintf(esc, sizeof(esc), "'\\t'"); break;
		case '\r': n = snprintf(esc, sizeof(esc), "'\\r'"); break;
		case '\n': n = snprintf(esc, sizeof(esc), "'\\n'"); break;
		default:
			if(v >= 0x20u && v < 0x7Fu) {
				n = snprintf(esc, sizeof(esc), "'%c'", (char)v);
			} else {
				n = snprintf(esc, sizeof(esc), "'\\u{%lx}'", (unsigned long)v);
			}
			break;
		}
		if(n < 0 || (size_t)n >= sizeof(esc)) {
			return -1;
		}
		return demangle_buf_append(&p->out, esc, (size_t)n);
	}

	if(digits_len == 0u) {
		return -1;
	}
	if(negative && demangle_buf_appendc(&p->out, '-') != 0) {
		return -1;
	}
	if(wide) {
		return demangle_buf_append(&p->out, "0x", 2u) != 0 ? -1 : demangle_buf_append(&p->out, digits, digits_len);
	}
	return rust_buf_append_base10(&p->out, v);
}

/*
 * <dyn-trait> = <path> {<dyn-trait-assoc-binding>}, a binding being
 * "p" <undisambiguated-identifier> <type>: Iterator<Item = i32>, the
 * bindings among the trait's own generic arguments if it has any.  The
 * p was left unread, and parsed as the type _, with the name after it
 * then refused: every dyn Iterator<Item = ...> was.
 */
static int rust_parse_v0_dyn_trait(rust_parser_t *p) {
	int open;

	if(rust_parse_v0_path(p) != 0) {
		return -1;
	}

	open = p->path_generic;
	if(open) {
		p->out.len--; /* the '>' of the trait's arguments: reopened */
		p->out.data[p->out.len] = '\0';
	}
	while(p->cur[0] == 'p') {
		p->cur++;
		if((open ? demangle_buf_append(&p->out, ", ", 2u) : demangle_buf_appendc(&p->out, '<')) != 0 || rust_parse_v0_identifier(p) != 0 ||
		   demangle_buf_append(&p->out, " = ", 3u) != 0 || rust_parse_v0_type(p) != 0) {
			return -1;
		}
		open = 1;
	}
	if(open && demangle_buf_appendc(&p->out, '>') != 0) {
		return -1;
	}

	return 0;
}

static int rust_parse_v0_type_inner(rust_parser_t *p);

static int rust_parse_v0_type(rust_parser_t *p) {
	int rc;

	/* parse_v0_type recurses heavily — pointers, references, slices,
	 * arrays, tuples, function pointers, dyn-traits, named types
	 * all eventually call back here.  Path productions also re-enter
	 * via rust_parse_v0_generic_arg → rust_parse_v0_type.  Bound the
	 * descent so deeply-nested generic args can't blow the stack. */
	int saved_in_value = p->in_value;
	size_t saved_bound = p->bound_lifetimes;

	if(rust_parser_enter(p) != 0) {
		return -1;
	}
	p->in_value = 0;
	rc = rust_parse_v0_type_inner(p);
	p->in_value = saved_in_value;
	p->bound_lifetimes = saved_bound; /* a binder binds in its type */
	rust_parser_leave(p);
	return rc;
}

/* A path that names a type or trait, in a value's path: an impl's
 * trait, <Foo as Trait<u8>>, takes no turbofish. */
static int rust_parse_v0_type_path(rust_parser_t *p) {
	int saved_in_value = p->in_value;
	int rc;

	p->in_value = 0;
	rc = rust_parse_v0_path(p);
	p->in_value = saved_in_value;
	return rc;
}

static int rust_parse_v0_type_inner(rust_parser_t *p) {
	static const struct {
		char code;
		const char *name;
	} basic[] = {{'b', "bool"}, {'c', "char"},  {'e', "str"},   {'u', "()"},  {'a', "i8"},  {'s', "i16"}, {'l', "i32"},
	             {'x', "i64"},  {'n', "i128"},  {'i', "isize"}, {'h', "u8"},  {'t', "u16"}, {'m', "u32"}, {'y', "u64"},
	             {'o', "u128"}, {'j', "usize"}, {'f', "f32"},   {'d', "f64"}, {'z', "!"},   {'p', "_"},   {'v', "..."}};
	rust_mark_t m;
	size_t i;

	for(i = 0u; i < sizeof(basic) / sizeof(basic[0]); i++) {
		if(p->cur[0] == basic[i].code) {
			p->cur++;
			return demangle_buf_append(&p->out, basic[i].name, strlen(basic[i].name));
		}
	}

	if(p->cur[0] == 'R' || p->cur[0] == 'Q') {
		int is_mut = (p->cur[0] == 'Q');
		p->cur++;
		if(demangle_buf_appendc(&p->out, '&') != 0) {
			return -1;
		}
		/* &'a T, but &T for the erased lifetime, L_, as rustc-demangle and
		 * GNU print it: it was &'_ T */
		if(p->cur[0] == 'L' && p->cur[1] == '_') {
			p->cur += 2;
		} else if(p->cur[0] == 'L') {
			if(rust_parse_v0_lifetime(p, 1) != 0 || demangle_buf_appendc(&p->out, ' ') != 0) {
				return -1;
			}
		}
		if(is_mut && demangle_buf_append(&p->out, "mut ", 4u) != 0) {
			return -1;
		}
		return rust_parse_v0_type(p);
	}

	if(p->cur[0] == 'P' || p->cur[0] == 'O') {
		int is_mut = (p->cur[0] == 'O');
		p->cur++;
		if(demangle_buf_append(&p->out, is_mut ? "*mut " : "*const ", is_mut ? 5u : 7u) != 0) {
			return -1;
		}
		return rust_parse_v0_type(p);
	}

	if(p->cur[0] == 'A') {
		p->cur++;
		if(demangle_buf_appendc(&p->out, '[') != 0 || rust_parse_v0_type(p) != 0 || demangle_buf_append(&p->out, "; ", 2u) != 0 ||
		   rust_parse_v0_const(p) != 0 || demangle_buf_appendc(&p->out, ']') != 0) {
			return -1;
		}
		return 0;
	}

	if(p->cur[0] == 'S') {
		p->cur++;
		if(demangle_buf_appendc(&p->out, '[') != 0 || rust_parse_v0_type(p) != 0 || demangle_buf_appendc(&p->out, ']') != 0) {
			return -1;
		}
		return 0;
	}

	if(p->cur[0] == 'T') {
		int first = 1;
		int count = 0;

		p->cur++;
		if(demangle_buf_appendc(&p->out, '(') != 0) {
			return -1;
		}
		while(p->cur[0] != '\0' && p->cur[0] != 'E') {
			if(!first && demangle_buf_append(&p->out, ", ", 2u) != 0) {
				return -1;
			}
			if(rust_parse_v0_type(p) != 0) {
				return -1;
			}
			first = 0;
			count++;
		}
		if(p->cur[0] != 'E') {
			return -1;
		}
		p->cur++;
		if(count == 1 && demangle_buf_appendc(&p->out, ',') != 0) {
			return -1;
		}
		return demangle_buf_appendc(&p->out, ')');
	}

	if(p->cur[0] == 'F') {
		int is_unsafe = 0;
		size_t ret_off;
		size_t ret_len;

		p->cur++;

		if(rust_parse_v0_binder(p) != 0) {
			return -1;
		}

		if(p->cur[0] == 'U') {
			is_unsafe = 1;
			p->cur++;
		}

		if(is_unsafe && demangle_buf_append(&p->out, "unsafe ", 7u) != 0) {
			return -1;
		}
		/*
		 * K <abi>, <abi> = "C" | <undisambiguated-identifier>, the
		 * identifier's _ standing for - : K6system, K8C_unwind.  Only C
		 * was read; any other ABI was left for the parameters, and the
		 * type refused.
		 */
		if(p->cur[0] == 'K') {
			p->cur++;
			if(demangle_buf_append(&p->out, "extern \"", 8u) != 0) {
				return -1;
			}
			if(p->cur[0] == 'C') {
				p->cur++;
				if(demangle_buf_appendc(&p->out, 'C') != 0) {
					return -1;
				}
			} else {
				size_t abi_off = p->out.len;
				size_t i;

				if(p->cur[0] == 'u' || rust_parse_v0_identifier(p) != 0) {
					return -1;
				}
				for(i = abi_off; i < p->out.len; i++) {
					if(p->out.data[i] == '_') {
						p->out.data[i] = '-';
					}
				}
			}
			if(demangle_buf_append(&p->out, "\" ", 2u) != 0) {
				return -1;
			}
		}
		if(demangle_buf_append(&p->out, "fn(", 3u) != 0) {
			return -1;
		}

		{
			int first = 1;
			while(p->cur[0] != '\0' && p->cur[0] != 'E') {
				if(!first && demangle_buf_append(&p->out, ", ", 2u) != 0) {
					return -1;
				}
				if(rust_parse_v0_type(p) != 0) {
					return -1;
				}
				first = 0;
			}
		}

		if(p->cur[0] != 'E') {
			return -1;
		}
		p->cur++;

		if(demangle_buf_appendc(&p->out, ')') != 0) {
			return -1;
		}

		ret_off = p->out.len;
		if(rust_parse_v0_type(p) != 0) {
			return -1;
		}
		ret_len = p->out.len - ret_off;
		if(!(ret_len == 2u && memcmp(p->out.data + ret_off, "()", 2u) == 0)) {
			if(demangle_buf_insert(&p->out, ret_off, " -> ", 4u) != 0) {
				return -1;
			}
		} else {
			p->out.len = ret_off;
			p->out.data[p->out.len] = '\0';
		}
		return 0;
	}

	if(p->cur[0] == 'D') {
		int first = 1;
		size_t outer_bound = p->bound_lifetimes;

		p->cur++;
		/* dyn for<'a> Trait<'a> + Other: the binder binds in the traits */
		if(demangle_buf_append(&p->out, "dyn ", 4u) != 0 || rust_parse_v0_binder(p) != 0) {
			return -1;
		}

		while(p->cur[0] != '\0' && p->cur[0] != 'E') {
			if(!first && demangle_buf_append(&p->out, " + ", 3u) != 0) {
				return -1;
			}
			if(rust_parse_v0_dyn_trait(p) != 0) {
				return -1;
			}
			first = 0;
		}

		if(p->cur[0] != 'E') {
			return -1;
		}
		p->cur++;
		p->bound_lifetimes = outer_bound; /* and not in the bound */

		/* the object's lifetime bound -- but not the erased one, L_, which
		 * rustc gives nearly every dyn type and nothing prints: it came
		 * out dyn Send + '_ */
		if(p->cur[0] == 'L' && p->cur[1] == '_') {
			p->cur += 2;
		} else if(p->cur[0] == 'L') {
			if(demangle_buf_append(&p->out, " + ", 3u) != 0 || rust_parse_v0_lifetime(p, 1) != 0) {
				return -1;
			}
		}
		return 0;
	}

	if(p->cur[0] == 'B') {
		const char *backref_start = p->cur;
		const char *save_cur;
		const char *target;
		size_t off;
		rust_mark_t mark;

		p->cur++;
		if(rust_parse_base62(p, &off) != 0) {
			return -1;
		}

		save_cur = p->cur;
		/* only to what came before this B (as for a const's) */
		if(rust_parser_resolve_backref(p, off, &target) != 0 || target >= backref_start) {
			return -1;
		}

		rust_mark_save(p, &mark);
		if(rust_parser_enter(p) != 0) {
			return -1;
		}

		p->cur = target;
		if(rust_parse_v0_type(p) != 0) {
			rust_mark_restore(p, &mark);
			rust_parser_leave(p);
			return -1;
		}
		rust_parser_leave(p);
		p->cur = save_cur;
		return 0;
	}

	rust_mark_save(p, &m);
	if(rust_parse_v0_path(p) == 0) {
		return 0;
	}
	rust_mark_restore(p, &m);
	return -1;
}

static int rust_parse_v0_generic_arg(rust_parser_t *p) {
	rust_mark_t m;
	int saved_in_value = p->in_value;
	int rc = 0;

	/* an argument is a type's, or a const's: no turbofish in it */
	p->in_value = 0;
	rust_mark_save(p, &m);
	if(rust_parse_v0_type(p) == 0) {
		goto out;
	}

	rust_mark_restore(p, &m);
	if(rust_parse_v0_const(p) == 0) {
		goto out;
	}

	rust_mark_restore(p, &m);
	if(rust_parse_v0_lifetime(p, 1) == 0) {
		goto out;
	}

	rust_mark_restore(p, &m);
	rc = rust_parse_v0_path(p);

out:
	p->in_value = saved_in_value;
	return rc;
}

static int rust_parse_v0_path(rust_parser_t *p) {
	rust_mark_t m;
	const char *start;
	const char *resume;
	const char *target;
	size_t off;

	if(rust_parser_enter(p) != 0) {
		return -1;
	}

	start = p->cur;
	rust_mark_save(p, &m);

	if(p->cur[0] == 'B') {
		p->cur++;
		/* only to what came before this B: _RB_ referred to itself, which
		 * recursed until the limit, or with DEMANGLE_NO_RECURSE_LIMIT ran
		 * the stack out */
		if(rust_parse_base62(p, &off) != 0 || rust_parser_resolve_backref(p, off, &target) != 0 || target >= start) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}

		resume = p->cur;
		p->cur = target;
		if(rust_parse_v0_path(p) != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
		p->cur = resume;
	} else if(p->cur[0] == 'C') {
		p->cur++;
		if(rust_parse_optional_disambiguator(p, NULL, NULL) != 0 || rust_parse_v0_identifier(p) != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
	} else if(p->cur[0] == 'N') {
		char ns;
		int has_dis;
		uint64_t dis_val;

		p->cur++;
		ns = p->cur[0];
		if(ns == '\0') {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
		p->cur++;

		has_dis = 0;
		dis_val = 0u;

		if(rust_parse_v0_path(p) != 0 || rust_parse_optional_disambiguator(p, &has_dis, &dis_val) != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}

		/*
		 * The namespace says how the name prints.  A lower-case one -- t
		 * for types, v for values -- is the name as it is: foo::bar.  An
		 * upper-case one is something the compiler made, braced, with its
		 * number: {closure#0}, {shim:vtable#0}, {X:name#1}.  This braced
		 * every lower-case name, lib::{f}, and told closures and shims by
		 * the text "closure" or "shim" in the name, so a function named
		 * __rust_no_alloc_shim_is_unstable_v2 was {shim:vtable#0}.
		 */
		if(demangle_buf_append(&p->out, "::", 2u) != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
		if(isupper((unsigned char)ns)) {
			size_t colon;
			size_t name_off;

			if(demangle_buf_appendc(&p->out, '{') != 0 || (ns == 'C'   ? demangle_buf_append(&p->out, "closure", 7u)
			                                               : ns == 'S' ? demangle_buf_append(&p->out, "shim", 4u)
			                                                           : demangle_buf_appendc(&p->out, ns)) != 0) {
				rust_mark_restore(p, &m);
				rust_parser_leave(p);
				return -1;
			}
			colon = p->out.len;
			if(demangle_buf_appendc(&p->out, ':') != 0) {
				rust_mark_restore(p, &m);
				rust_parser_leave(p);
				return -1;
			}
			name_off = p->out.len;
			if(p->cur[0] == '0') {
				p->cur++; /* no name, as rustc gives a closure */
			} else if(rust_parse_v0_identifier(p) != 0) {
				rust_mark_restore(p, &m);
				rust_parser_leave(p);
				return -1;
			}
			if(p->out.len == name_off) {
				p->out.len = colon; /* no name: {closure#0} */
				p->out.data[p->out.len] = '\0';
			}
			/* none is #0, s_ #1 (it printed s_ as #0 too) */
			if(demangle_buf_appendc(&p->out, '#') != 0 || rust_buf_append_base10(&p->out, has_dis ? dis_val + 1u : 0u) != 0 ||
			   demangle_buf_appendc(&p->out, '}') != 0) {
				rust_mark_restore(p, &m);
				rust_parser_leave(p);
				return -1;
			}
		} else if(islower((unsigned char)ns)) {
			if(p->cur[0] == '0') {
				p->cur++; /* no name, no "::" */
				p->out.len -= 2u;
				p->out.data[p->out.len] = '\0';
			} else if(rust_parse_v0_identifier(p) != 0) {
				rust_mark_restore(p, &m);
				rust_parser_leave(p);
				return -1;
			}
		} else {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
	} else if(p->cur[0] == 'M') {
		size_t before;

		p->cur++;

		/* <impl-path> = [<disambiguator>] <path>: a second impl of a
		 * type in one module, Ms_..., was refused */
		before = p->out.len;
		if(rust_parse_optional_disambiguator(p, NULL, NULL) != 0 || rust_parse_v0_path(p) != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
		p->out.len = before;
		p->out.data[p->out.len] = '\0';

		if(demangle_buf_appendc(&p->out, '<') != 0 || rust_parse_v0_type(p) != 0 || demangle_buf_appendc(&p->out, '>') != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
	} else if(p->cur[0] == 'X') {
		size_t before;

		p->cur++;

		/* <impl-path>, as for M */
		before = p->out.len;
		if(rust_parse_optional_disambiguator(p, NULL, NULL) != 0 || rust_parse_v0_path(p) != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
		p->out.len = before;
		p->out.data[p->out.len] = '\0';

		if(demangle_buf_appendc(&p->out, '<') != 0 || rust_parse_v0_type(p) != 0 || demangle_buf_append(&p->out, " as ", 4u) != 0 ||
		   rust_parse_v0_type_path(p) != 0 || demangle_buf_appendc(&p->out, '>') != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
	} else if(p->cur[0] == 'Y') {
		p->cur++;
		if(demangle_buf_appendc(&p->out, '<') != 0 || rust_parse_v0_type(p) != 0 || demangle_buf_append(&p->out, " as ", 4u) != 0 ||
		   rust_parse_v0_type_path(p) != 0 || demangle_buf_appendc(&p->out, '>') != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
	} else if(p->cur[0] == 'I') {
		int first;

		p->cur++;
		/* f::<u8> for a value, Vec<u8> for a type: it was ::< for both */
		if(rust_parse_v0_path(p) != 0 || (p->in_value ? demangle_buf_append(&p->out, "::<", 3u) : demangle_buf_appendc(&p->out, '<')) != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}

		first = 1;
		while(p->cur[0] != '\0' && p->cur[0] != 'E') {
			if(!first && demangle_buf_append(&p->out, ", ", 2u) != 0) {
				rust_mark_restore(p, &m);
				rust_parser_leave(p);
				return -1;
			}

			if(rust_parse_v0_generic_arg(p) != 0) {
				rust_mark_restore(p, &m);
				rust_parser_leave(p);
				return -1;
			}
			first = 0;
		}

		if(p->cur[0] != 'E' || first) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}

		p->cur++;
		if(demangle_buf_appendc(&p->out, '>') != 0) {
			rust_mark_restore(p, &m);
			rust_parser_leave(p);
			return -1;
		}
	} else {
		rust_mark_restore(p, &m);
		rust_parser_leave(p);
		return -1;
	}

	/* a back reference's path set it already */
	if(start[0] != 'B') {
		p->path_generic = (start[0] == 'I');
	}
	rust_parser_leave(p);
	return 0;
}

/*
 * _R <path> [<instantiating-crate>]: a generic instance names, after its
 * path, the crate it was instantiated in -- B2_, or Cs..._3lib -- which is
 * not printed (rustc-demangle, GNU).  rustc puts it on every instance,
 * and every one was refused.  A back reference there is not followed, as
 * GNU c++filt follows none it does not print: it need only be well formed.
 */
static int rust_parse_v0_symbol(rust_parser_t *p) {
	size_t len;

	p->in_value = 1; /* the symbol is a value: f::<u8> */
	if(rust_parse_v0_path(p) != 0) {
		return -1;
	}
	p->in_value = 0;
	if(p->cur[0] == '\0') {
		return 0;
	}
	if(p->cur[0] == 'B') {
		size_t off;

		p->cur++;
		return rust_parse_base62(p, &off);
	}
	len = p->out.len;
	if(rust_parse_v0_path(p) != 0) {
		return -1;
	}
	p->out.len = len;
	p->out.data[p->out.len] = '\0';
	return 0;
}

static char *rust_demangle_v0(const char *mangled, int options) {
	rust_parser_t parser;
	char *ret;

	if(!rust_is_v0(mangled)) {
		return NULL;
	}

	if(rust_parser_init(&parser, mangled, options) != 0) {
		return NULL;
	}

	parser.cur = mangled + 2;
	if(rust_parse_v0_symbol(&parser) != 0 || parser.cur[0] != '\0') {
		rust_parser_destroy(&parser);
		return NULL;
	}

	ret = demangle_buf_take(&parser.out);
	rust_parser_destroy(&parser);
	return ret;
}

static int rust_parse_decimal_span(const char **cur, size_t *out) {
	size_t v;
	const char *s;

	if(cur == NULL || *cur == NULL || out == NULL || !isdigit((unsigned char)**cur)) {
		return -1;
	}

	v = 0u;
	s = *cur;
	while(isdigit((unsigned char)*s)) {
		size_t d = (size_t)(*s - '0');

		/* before the multiply: after it, nv < v misses a wrap from
		 * [SIZE_MAX / 9, SIZE_MAX / 10 * 10] */
		if(v > (SIZE_MAX - d) / 10u) {
			return -1;
		}
		v = v * 10u + d;
		s++;
	}

	*cur = s;
	*out = v;
	return 0;
}

static int rust_legacy_is_hash_component(const char *s, size_t len) {
	size_t i;

	if(s == NULL || len != 17u || s[0] != 'h') {
		return 0;
	}

	for(i = 1u; i < len; i++) {
		if(!is_hex_char(s[i])) {
			return 0;
		}
	}

	return 1;
}

static int rust_legacy_decode_token(const char *tok, size_t tok_len, rust_buf_t *out) {
	if(tok_len == 2u && memcmp(tok, "LT", 2u) == 0) {
		if(demangle_buf_appendc(out, '<') != 0)
			return -1;
	} else if(tok_len == 2u && memcmp(tok, "GT", 2u) == 0) {
		if(demangle_buf_appendc(out, '>') != 0)
			return -1;
	} else if(tok_len == 2u && memcmp(tok, "RF", 2u) == 0) {
		if(demangle_buf_appendc(out, '&') != 0)
			return -1;
	} else if(tok_len == 2u && memcmp(tok, "LP", 2u) == 0) {
		if(demangle_buf_appendc(out, '(') != 0)
			return -1;
	} else if(tok_len == 2u && memcmp(tok, "RP", 2u) == 0) {
		if(demangle_buf_appendc(out, ')') != 0)
			return -1;
	} else if(tok_len == 1u && memcmp(tok, "C", 1u) == 0) {
		if(demangle_buf_appendc(out, ',') != 0)
			return -1;
	} else if(tok_len == 2u && memcmp(tok, "SP", 2u) == 0) {
		if(demangle_buf_appendc(out, '@') != 0)
			return -1;
	} else if(tok_len == 2u && memcmp(tok, "BP", 2u) == 0) {
		if(demangle_buf_appendc(out, '*') != 0)
			return -1;
	} else if(tok_len == 3u && memcmp(tok, "u20", 3u) == 0) {
		if(demangle_buf_appendc(out, ' ') != 0)
			return -1;
	} else if(tok_len == 3u && memcmp(tok, "u27", 3u) == 0) {
		if(demangle_buf_appendc(out, '\'') != 0)
			return -1;
	} else if(tok_len == 3u && memcmp(tok, "u5b", 3u) == 0) {
		if(demangle_buf_appendc(out, '[') != 0)
			return -1;
	} else if(tok_len == 3u && memcmp(tok, "u5d", 3u) == 0) {
		if(demangle_buf_appendc(out, ']') != 0)
			return -1;
	} else if(tok_len == 3u && memcmp(tok, "u7b", 3u) == 0) {
		if(demangle_buf_appendc(out, '{') != 0)
			return -1;
	} else if(tok_len == 3u && memcmp(tok, "u7d", 3u) == 0) {
		if(demangle_buf_appendc(out, '}') != 0)
			return -1;
	} else if(tok_len == 3u && memcmp(tok, "u3b", 3u) == 0) {
		if(demangle_buf_appendc(out, ';') != 0)
			return -1;
	} else if(tok_len == 3u && memcmp(tok, "u7e", 3u) == 0) {
		if(demangle_buf_appendc(out, '~') != 0)
			return -1;
	} else if(tok_len >= 2u && tok[0] == 'u') {
		size_t k;
		unsigned long cp = 0ul;

		for(k = 1u; k < tok_len; k++) {
			unsigned v;
			if(!is_hex_char(tok[k])) {
				return -1;
			}
			if(tok[k] >= '0' && tok[k] <= '9') {
				v = (unsigned)(tok[k] - '0');
			} else if(tok[k] >= 'a' && tok[k] <= 'f') {
				v = 10u + (unsigned)(tok[k] - 'a');
			} else {
				v = 10u + (unsigned)(tok[k] - 'A');
			}
			/* past U+10FFFF it is no character -- and on LP64 the
			 * uint32_t below cut $u100000041$ to U+0041, A */
			cp = cp * 16ul + (unsigned long)v;
			if(cp > 0x10FFFFul) {
				return -1;
			}
		}
		if(rust_utf8_append_codepoint(out, (uint32_t)cp) != 0) {
			return -1;
		}
	} else {
		return -1;
	}

	return 0;
}

static int rust_legacy_decode_component(const char *s, size_t len, rust_buf_t *out) {
	size_t i;

	if(s == NULL || out == NULL) {
		return -1;
	}

	for(i = 0u; i < len;) {
		if(s[i] != '$') {
			if(s[i] == '.' && i + 1u < len && s[i + 1u] == '.') {
				if(demangle_buf_append(out, "::", 2u) != 0) {
					return -1;
				}
				i += 2u;
			} else {
				if(demangle_buf_appendc(out, s[i]) != 0) {
					return -1;
				}
				i++;
			}
			continue;
		}

		{
			size_t j = i + 1u;
			while(j < len && s[j] != '$') {
				j++;
			}
			if(j >= len) {
				return -1;
			}

			{
				const char *tok = s + i + 1u;
				size_t tok_len = j - i - 1u;

				if(rust_legacy_decode_token(tok, tok_len, out) != 0) {
					return -1;
				}
			}

			i = j + 1u;
		}
	}

	return 0;
}

static int rust_legacy_process_component(const char *part, size_t len, int is_first, rust_buf_t *out) {
	size_t start_off;

	if(!is_first && demangle_buf_append(out, "::", 2u) != 0) {
		return -1;
	}
	start_off = out->len;
	if(rust_legacy_decode_component(part, len, out) != 0) {
		return -1;
	}
	if(out->len > start_off + 1u && out->data[start_off] == '_' && (out->data[start_off + 1u] == '<' || out->data[start_off + 1u] == '(')) {
		memmove(out->data + start_off, out->data + start_off + 1u, out->len - start_off);
		out->len--;
	}
	return 0;
}

static char *rust_demangle_legacy(const char *mangled, int options) {
	const char *cur;
	rust_buf_t out;
	int first;

	(void)options;

	if(!rust_is_legacy(mangled)) {
		return NULL;
	}

	memset(&out, 0, sizeof(out));
	if(demangle_buf_reserve(&out, 0u) != 0) {
		demangle_buf_destroy(&out);
		return NULL;
	}
	out.data[0] = '\0';

	cur = mangled + 3;
	first = 1;
	while(*cur != '\0' && *cur != 'E') {
		size_t len;
		const char *part;

		if(rust_parse_decimal_span(&cur, &len) != 0 || len == 0u || !rust_has_n_bytes(cur, len)) {
			demangle_buf_destroy(&out);
			return NULL;
		}

		part = cur;
		cur += len;

		if(*cur == 'E' && rust_legacy_is_hash_component(part, len)) {
			break;
		}

		if(rust_legacy_process_component(part, len, first, &out) != 0) {
			demangle_buf_destroy(&out);
			return NULL;
		}
		first = 0;
	}

	if(*cur != 'E' || first) {
		demangle_buf_destroy(&out);
		return NULL;
	}

	return demangle_buf_take(&out);
}

char *demangle_rust(const char *mangled, int options) {
	if(mangled == NULL || mangled[0] == '\0') {
		return NULL;
	}

	/* both schemes, whether Rust was asked for or guessed: DEMANGLE_RUST
	 * refused every legacy name, which demangle(3) says it takes */
	if(rust_is_v0(mangled)) {
		return rust_demangle_v0(mangled, options);
	}

	if(rust_is_legacy(mangled)) {
		return rust_demangle_legacy(mangled, options);
	}

	return NULL;
}
