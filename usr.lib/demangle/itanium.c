#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <demangle.h>
#include <demangle_internal.h>

#define DM_SUBST_INITIAL_CAP 256u
#define DM_TEMPLATE_STACK_INITIAL_CAP 64u
#define DM_TEMPLATE_ARG_CAP 64u
#define DM_RECURSION_DEFAULT_LIMIT 256

enum { DM_MEMBER_CONST = 1u << 0, DM_MEMBER_VOLATILE = 1u << 1, DM_MEMBER_RESTRICT = 1u << 2, DM_MEMBER_REF_LVALUE = 1u << 3, DM_MEMBER_REF_RVALUE = 1u << 4 };

typedef demangle_buf_t dm_buf_t;

/* An entry is a slice of the output -- or, once text was inserted inside
 * it (a pointer's (*) into an array's int[4]), a copy of what it was. */
typedef struct dm_subst {
	size_t off;
	size_t len;
	char *text; /* the copy, or NULL: the slice */
} dm_subst_t;

/*
 * A template argument list's arguments, as T_, T0_, ... number them: each
 * is a run of the elements whose text the off/len arrays beside it hold --
 * one element, or a pack's, J...E, which may be none.
 */
typedef struct dm_targ_map {
	size_t first[DM_TEMPLATE_ARG_CAP]; /* its first element */
	size_t n[DM_TEMPLATE_ARG_CAP]; /* how many elements */
	unsigned char pack[DM_TEMPLATE_ARG_CAP];
	size_t count;
} dm_targ_map_t;

typedef struct dm_template_frame {
	size_t output_off;
	size_t arg_off[DM_TEMPLATE_ARG_CAP];
	size_t arg_len[DM_TEMPLATE_ARG_CAP];
	size_t arg_count;
	dm_targ_map_t map;
} dm_template_frame_t;

typedef struct dm_itanium_parser {
	const char *input;
	const char *cur;
	int options;

	int recursion_depth;
	int recursion_limit;

	unsigned member_quals;

	dm_buf_t out;

	dm_subst_t *substitutions;
	size_t substitution_count;
	size_t substitution_cap;

	dm_template_frame_t *template_frames;
	size_t template_depth;
	size_t template_cap;

	size_t last_template_off[DM_TEMPLATE_ARG_CAP];
	size_t last_template_len[DM_TEMPLATE_ARG_CAP];
	size_t last_template_count;

	int in_type_context;
	/* local names, Z <encoding> E, being parsed: their encodings end at E */
	int local_depth;
	/* a substitution is being read as a nested name's prefix; and the one
	 * read last, if a standard abbreviation, the length of its template's
	 * name after "std::" (basic_string), which names its constructor */
	int sub_is_prefix;
	size_t std_last_len;
	/* the encoding parsed next is a local name's, and its function's
	 * return type is then not printed (drop_return_type, for the bare
	 * function type that follows its name) */
	int next_encoding_is_local;
	int drop_return_type;
	int has_return_type;
	/* where the encoding being parsed began in the output: a template
	 * function's return type goes there, after any thunk's words or the
	 * scope of a local name */
	size_t encoding_off;
	/* the array type parsed last: where its outermost bounds begin, which
	 * an outer bound or a pointer's (*) goes in front of */
	size_t array_dims_off;
	/* the pointer or reference to an array parsed last: where the ) of its
	 * declarator is, (*)[4], which a pointer to it writes its * before;
	 * SIZE_MAX when the type parsed last was no such thing */
	size_t decl_close;

	size_t func_tmpl_off[DM_TEMPLATE_ARG_CAP];
	size_t func_tmpl_len[DM_TEMPLATE_ARG_CAP];
	size_t func_tmpl_count;

	dm_targ_map_t last_map; /* the arguments of last_template_* */
	dm_targ_map_t func_map; /* and of func_tmpl_* */

	/* a pack expansion, Dp, being parsed: its pattern is parsed once for
	 * each element of the pack a T_ in it names, the element pack_index;
	 * that T_ notes the pack's length in pack_seen (SIZE_MAX: none yet) */
	int pack_expanding;
	size_t pack_index;
	size_t pack_seen;
} dm_itanium_parser_t;

/* All a speculative parse can change, so that backtracking undoes it --
 * has_return_type and the last template's arguments too, which a failed
 * template type left set for the parse that came after it. */
typedef struct dm_parse_mark {
	const char *cur;
	size_t out_len;
	unsigned member_quals;
	size_t substitution_count;
	size_t template_depth;
	size_t last_template_count;
	size_t last_template_off[DM_TEMPLATE_ARG_CAP];
	size_t last_template_len[DM_TEMPLATE_ARG_CAP];
	dm_targ_map_t last_map;
	int has_return_type;
} dm_parse_mark_t;

static int parser_begin(dm_itanium_parser_t *p);
static void parser_destroy(dm_itanium_parser_t *p);
static int parser_enter(dm_itanium_parser_t *p);
static void parser_leave(dm_itanium_parser_t *p);
static int parser_ensure_substitution_capacity(dm_itanium_parser_t *p, size_t need);
static int parser_ensure_template_capacity(dm_itanium_parser_t *p, size_t need);
static int parser_push_template_frame(dm_itanium_parser_t *p);
static int parser_pop_template_frame(dm_itanium_parser_t *p);
static int parser_record_template_arg(dm_itanium_parser_t *p, size_t off, size_t len);
static int parser_capture_template_frame(dm_itanium_parser_t *p);
static void targ_map_copy(dm_targ_map_t *dst, const dm_targ_map_t *src);
static int parser_record_template_map(dm_itanium_parser_t *p, size_t first, size_t n, int pack);
static int append_template_arg(dm_itanium_parser_t *p, const dm_targ_map_t *map, const size_t *off, const size_t *len, size_t elems, size_t idx);
static int parser_add_substitution(dm_itanium_parser_t *p, size_t off, size_t len);
static int parser_append_output_slice(dm_itanium_parser_t *p, size_t off, size_t len);
static int parser_lookup_substitution(dm_itanium_parser_t *p, size_t idx);

static int parse_number(dm_itanium_parser_t *p, size_t *out);
static int parse_seq_id(dm_itanium_parser_t *p, size_t *out);
static int parse_signed_number_token(dm_itanium_parser_t *p, long long *out);

static int parse_encoding(dm_itanium_parser_t *p);
static int parse_special_name(dm_itanium_parser_t *p);
static int parse_name(dm_itanium_parser_t *p);
static int parse_nested_name(dm_itanium_parser_t *p);
static int parse_unscoped_name(dm_itanium_parser_t *p);
static int parse_local_name(dm_itanium_parser_t *p);
static int parse_unqualified_name(dm_itanium_parser_t *p, size_t prev_off, size_t prev_len, size_t *comp_off, size_t *comp_len);
static int parse_source_name(dm_itanium_parser_t *p);
static int parse_substitution(dm_itanium_parser_t *p);
static int parse_operator_name(dm_itanium_parser_t *p);
static int parse_ctor_dtor_name(dm_itanium_parser_t *p, size_t prev_off, size_t prev_len);
static int parse_unnamed_type_name(dm_itanium_parser_t *p);

static int parse_template_args(dm_itanium_parser_t *p);
static int parse_template_arg(dm_itanium_parser_t *p);

static int parse_bare_function_type(dm_itanium_parser_t *p);

static int parse_type(dm_itanium_parser_t *p);
static int parse_template_param(dm_itanium_parser_t *p);
static int parse_function_type(dm_itanium_parser_t *p);
static int parse_array_type(dm_itanium_parser_t *p);
static int parse_pointer_to_member(dm_itanium_parser_t *p);
static int parse_decltype_type(dm_itanium_parser_t *p);
static int parse_vendor_type(dm_itanium_parser_t *p);
static int parse_expression(dm_itanium_parser_t *p);
static int parse_expression_operator(dm_itanium_parser_t *p);
static int parse_cast_expression(dm_itanium_parser_t *p);
static int parse_fold_expression(dm_itanium_parser_t *p);
static int parse_pack_expansion(dm_itanium_parser_t *p);
static int parse_expr_primary(dm_itanium_parser_t *p);

static void mark_save(dm_itanium_parser_t *p, dm_parse_mark_t *m);
static void mark_restore(dm_itanium_parser_t *p, const dm_parse_mark_t *m);

static int is_void_text(const dm_buf_t *buf, size_t off, size_t len);
static int has_n_bytes(const char *s, size_t n);

static int parser_begin(dm_itanium_parser_t *p) {
	memset(p, 0, sizeof(*p));
	p->recursion_limit = DM_RECURSION_DEFAULT_LIMIT;
	p->decl_close = SIZE_MAX;

	p->substitutions = (dm_subst_t *)calloc(DM_SUBST_INITIAL_CAP, sizeof(*p->substitutions));
	if(p->substitutions == NULL) {
		return -1;
	}
	p->substitution_cap = DM_SUBST_INITIAL_CAP;

	p->template_frames = (dm_template_frame_t *)calloc(DM_TEMPLATE_STACK_INITIAL_CAP, sizeof(*p->template_frames));
	if(p->template_frames == NULL) {
		free(p->substitutions);
		p->substitutions = NULL;
		p->substitution_cap = 0u;
		return -1;
	}
	p->template_cap = DM_TEMPLATE_STACK_INITIAL_CAP;

	if(demangle_buf_reserve(&p->out, 0u) != 0) {
		parser_destroy(p);
		return -1;
	}

	p->out.data[0] = '\0';
	return 0;
}

static void parser_destroy(dm_itanium_parser_t *p) {
	if(p == NULL) {
		return;
	}

	if(p->substitutions != NULL) {
		size_t i;

		for(i = 0u; i < p->substitution_cap; i++) {
			free(p->substitutions[i].text);
		}
	}
	free(p->substitutions);
	p->substitutions = NULL;
	p->substitution_count = 0u;
	p->substitution_cap = 0u;

	free(p->template_frames);
	p->template_frames = NULL;
	p->template_depth = 0u;
	p->template_cap = 0u;

	demangle_buf_destroy(&p->out);
}

static int parser_enter(dm_itanium_parser_t *p) {
	if(p->recursion_depth >= p->recursion_limit) {
		return -1;
	}

	p->recursion_depth++;
	return 0;
}

static void parser_leave(dm_itanium_parser_t *p) {
	if(p->recursion_depth > 0) {
		p->recursion_depth--;
	}
}

static int parser_ensure_substitution_capacity(dm_itanium_parser_t *p, size_t need) {
	size_t cap;
	dm_subst_t *next;

	if(need <= p->substitution_cap) {
		return 0;
	}

	cap = (p->substitution_cap == 0u) ? DM_SUBST_INITIAL_CAP : p->substitution_cap;
	while(cap < need) {
		size_t doubled = cap << 1;
		if(doubled < cap || doubled > (size_t)-1 / sizeof(*p->substitutions)) {
			return -1;
		}
		cap = doubled;
	}

	next = (dm_subst_t *)realloc(p->substitutions, cap * sizeof(*p->substitutions));
	if(next == NULL) {
		return -1;
	}

	memset(next + p->substitution_cap, 0, (cap - p->substitution_cap) * sizeof(*next));
	p->substitutions = next;
	p->substitution_cap = cap;
	return 0;
}

static int parser_ensure_template_capacity(dm_itanium_parser_t *p, size_t need) {
	size_t cap;
	dm_template_frame_t *next;

	if(need <= p->template_cap) {
		return 0;
	}

	cap = (p->template_cap == 0u) ? DM_TEMPLATE_STACK_INITIAL_CAP : p->template_cap;
	while(cap < need) {
		size_t doubled = cap << 1;
		if(doubled < cap || doubled > (size_t)-1 / sizeof(*p->template_frames)) {
			return -1;
		}
		cap = doubled;
	}

	next = (dm_template_frame_t *)realloc(p->template_frames, cap * sizeof(*p->template_frames));
	if(next == NULL) {
		return -1;
	}

	p->template_frames = next;
	p->template_cap = cap;
	return 0;
}

static int parser_push_template_frame(dm_itanium_parser_t *p) {
	if(parser_ensure_template_capacity(p, p->template_depth + 1u) != 0) {
		return -1;
	}

	memset(&p->template_frames[p->template_depth], 0, sizeof(p->template_frames[p->template_depth]));
	p->template_frames[p->template_depth].output_off = p->out.len;
	p->template_depth++;
	return 0;
}

static int parser_pop_template_frame(dm_itanium_parser_t *p) {
	if(p->template_depth == 0u) {
		return -1;
	}

	p->template_depth--;
	return 0;
}

static int parser_record_template_arg(dm_itanium_parser_t *p, size_t off, size_t len) {
	dm_template_frame_t *frame;

	if(p->template_depth == 0u) {
		return -1;
	}

	frame = &p->template_frames[p->template_depth - 1u];
	if(frame->arg_count >= DM_TEMPLATE_ARG_CAP) {
		return -1;
	}

	frame->arg_off[frame->arg_count] = off;
	frame->arg_len[frame->arg_count] = len;
	frame->arg_count++;
	return 0;
}

static int parser_capture_template_frame(dm_itanium_parser_t *p) {
	dm_template_frame_t *frame;
	size_t i;

	if(p->template_depth == 0u) {
		return -1;
	}

	frame = &p->template_frames[p->template_depth - 1u];
	p->last_template_count = frame->arg_count;
	for(i = 0u; i < frame->arg_count; i++) {
		p->last_template_off[i] = frame->arg_off[i];
		p->last_template_len[i] = frame->arg_len[i];
	}
	targ_map_copy(&p->last_map, &frame->map);

	return 0;
}

/* Copy the arguments in use of one map to another. */
static void targ_map_copy(dm_targ_map_t *dst, const dm_targ_map_t *src) {
	dst->count = src->count;
	memcpy(dst->first, src->first, src->count * sizeof(dst->first[0]));
	memcpy(dst->n, src->n, src->count * sizeof(dst->n[0]));
	memcpy(dst->pack, src->pack, src->count * sizeof(dst->pack[0]));
}

/* Enter, in the innermost argument list, an argument of the @p n elements
 * from @p first on: one, or a pack's. */
static int parser_record_template_map(dm_itanium_parser_t *p, size_t first, size_t n, int pack) {
	dm_targ_map_t *map;

	if(p->template_depth == 0u) {
		return -1;
	}
	map = &p->template_frames[p->template_depth - 1u].map;
	if(map->count >= DM_TEMPLATE_ARG_CAP) {
		return -1;
	}
	map->first[map->count] = first;
	map->n[map->count] = n;
	map->pack[map->count] = (unsigned char)(pack != 0);
	map->count++;
	return 0;
}

static int parser_add_substitution(dm_itanium_parser_t *p, size_t off, size_t len) {
	if(len == 0u) {
		return 0;
	}

	if(parser_ensure_substitution_capacity(p, p->substitution_count + 1u) != 0) {
		return -1;
	}

	if(off + len > p->out.len) {
		return -1;
	}

	/* a slot backtracking gave up may still hold a copy */
	free(p->substitutions[p->substitution_count].text);
	p->substitutions[p->substitution_count].text = NULL;
	p->substitutions[p->substitution_count].off = off;
	p->substitutions[p->substitution_count].len = len;

	p->substitution_count++;
	return 0;
}

static int parser_append_output_slice(dm_itanium_parser_t *p, size_t off, size_t len) {
	if(off + len > p->out.len) {
		return -1;
	}

	if(demangle_buf_reserve(&p->out, len) != 0) {
		return -1;
	}

	memmove(p->out.data + p->out.len, p->out.data + off, len);
	p->out.len += len;
	p->out.data[p->out.len] = '\0';
	return 0;
}

static int parser_lookup_substitution(dm_itanium_parser_t *p, size_t idx) {
	if(idx >= p->substitution_count) {
		return -1;
	}

	if(p->substitutions[idx].text != NULL) {
		return demangle_buf_append(&p->out, p->substitutions[idx].text, p->substitutions[idx].len);
	}
	return parser_append_output_slice(p, p->substitutions[idx].off, p->substitutions[idx].len);
}

/*
 * Insert @p n bytes of @p s at @p pos in the output, inside the type that
 * began at @p type_off, keeping the tables right: what lies after @p pos
 * moves along, and an entry made inside that type that @p pos falls
 * within keeps the text it had, as a copy.  One that began before the
 * type holds the whole of it, insertion and all, and grows.  demangle_buf_insert()
 * alone left every offset after @p pos behind.
 */
static int parser_insert_in_type(dm_itanium_parser_t *p, size_t type_off, size_t pos, const char *s, size_t n) {
	size_t i;

	for(i = 0u; i < p->substitution_count; i++) {
		dm_subst_t *e = &p->substitutions[i];

		if(e->text != NULL || e->off + e->len <= pos) {
			continue;
		}
		if(e->off >= pos) {
			e->off += n;
		} else if(e->off >= type_off) {
			e->text = (char *)malloc(e->len + 1u);
			if(e->text == NULL) {
				return -1;
			}
			memcpy(e->text, p->out.data + e->off, e->len);
			e->text[e->len] = '\0';
		} else {
			e->len += n;
		}
	}
	for(i = 0u; i < p->last_template_count; i++) {
		if(p->last_template_off[i] >= pos) {
			p->last_template_off[i] += n;
		}
	}
	for(i = 0u; i < p->func_tmpl_count; i++) {
		if(p->func_tmpl_off[i] >= pos) {
			p->func_tmpl_off[i] += n;
		}
	}
	return demangle_buf_insert(&p->out, pos, s, n);
}

/* Before the output is cut back to @p pos: make the entries that reach
 * past it copies of their text, which the cut would leave them without. */
static int parser_freeze_from(dm_itanium_parser_t *p, size_t pos) {
	size_t i;

	for(i = 0u; i < p->substitution_count; i++) {
		dm_subst_t *e = &p->substitutions[i];

		if(e->text != NULL || e->off + e->len <= pos) {
			continue;
		}
		e->text = (char *)malloc(e->len + 1u);
		if(e->text == NULL) {
			return -1;
		}
		memcpy(e->text, p->out.data + e->off, e->len);
		e->text[e->len] = '\0';
	}
	return 0;
}

/*
 * Find, in the type text from @p off to the end of @p out, the array bounds
 * outside any template argument list or parenthesis: int[4], int (*)[4].
 * *@p dims gets where they begin.  *@p decl gets the position of the ) of
 * a pointer's or reference's declarator in front of them, (*), or SIZE_MAX
 * when there is none.  A bracket that is no bound, Foo[abi:cxx11], is
 * passed over.  A function type's parameters count as its bounds -- void
 * (int), void (*)(int) -- and *@p is_func says which it found.  Returns 0
 * when there are bounds or parameters, -1 when the type has neither.
 */
static int find_array_declarator(const dm_buf_t *out, size_t off, size_t *dims, size_t *decl, int *is_func) {
	size_t i;
	size_t depth = 0u;
	size_t close = SIZE_MAX;

	*is_func = 0;
	/* decltype (x) is no function */
	if(out->len - off >= 8u && memcmp(out->data + off, "decltype", 8u) == 0) {
		return -1;
	}
	for(i = off; i < out->len; i++) {
		char c = out->data[i];

		if(c == '<' || c == '(') {
			/* a pointer's declarator: " (" then only * & -- or a member
			 * pointer's, A::* first -- and ")" */
			if(c == '(' && depth == 0u) {
				size_t j = i + 1u;
				size_t k;

				for(k = i + 1u; k < out->len && out->data[k] != ')' && out->data[k] != '(' && out->data[k] != '<'; k++) {
					if(k + 2u < out->len && out->data[k] == ':' && out->data[k + 1u] == ':' && out->data[k + 2u] == '*') {
						j = k + 2u;
					}
				}
				while(j < out->len && (out->data[j] == '*' || out->data[j] == '&')) {
					j++;
				}
				if(j > i + 1u && j < out->len && out->data[j] == ')') {
					close = j;
					i = j;
					continue;
				}
				/* else a function's parameters, after a space or a declarator */
				if(i > off && (out->data[i - 1u] == ' ' || (close != SIZE_MAX && close + 1u == i))) {
					*dims = i;
					*decl = (close != SIZE_MAX && close + 1u == i) ? close : SIZE_MAX;
					*is_func = 1;
					return 0;
				}
			}
			depth++;
		} else if(c == '>' || c == ')') {
			if(depth == 0u) {
				return -1;
			}
			depth--;
		} else if(c == '[' && depth == 0u && i + 1u < out->len && (out->data[i + 1u] == ']' || isdigit((unsigned char)out->data[i + 1u]))) {
			*dims = i;
			*decl = (close != SIZE_MAX && close + 1u == i) ? close : SIZE_MAX;
			return 0;
		} else {
			close = SIZE_MAX; /* the declarator must be just before the bounds */
		}
	}
	return -1;
}

static void mark_save(dm_itanium_parser_t *p, dm_parse_mark_t *m) {
	m->cur = p->cur;
	m->out_len = p->out.len;
	m->member_quals = p->member_quals;
	m->substitution_count = p->substitution_count;
	m->template_depth = p->template_depth;
	m->last_template_count = p->last_template_count;
	memcpy(m->last_template_off, p->last_template_off, p->last_template_count * sizeof(m->last_template_off[0]));
	memcpy(m->last_template_len, p->last_template_len, p->last_template_count * sizeof(m->last_template_len[0]));
	targ_map_copy(&m->last_map, &p->last_map);
	m->has_return_type = p->has_return_type;
}

static void mark_restore(dm_itanium_parser_t *p, const dm_parse_mark_t *m) {
	p->cur = m->cur;
	p->member_quals = m->member_quals;
	p->substitution_count = m->substitution_count;
	p->template_depth = m->template_depth;
	p->last_template_count = m->last_template_count;
	memcpy(p->last_template_off, m->last_template_off, m->last_template_count * sizeof(p->last_template_off[0]));
	memcpy(p->last_template_len, m->last_template_len, m->last_template_count * sizeof(p->last_template_len[0]));
	targ_map_copy(&p->last_map, &m->last_map);
	p->has_return_type = m->has_return_type;

	if(m->out_len <= p->out.len) {
		p->out.len = m->out_len;
		p->out.data[p->out.len] = '\0';
	}
}

static int has_n_bytes(const char *s, size_t n) {
	size_t i;

	for(i = 0u; i < n; i++) {
		if(s[i] == '\0') {
			return 0;
		}
	}

	return 1;
}

static int parse_number(dm_itanium_parser_t *p, size_t *out) {
	const char *s;
	size_t v;

	if(p == NULL || out == NULL || !isdigit((unsigned char)p->cur[0])) {
		return -1;
	}

	s = p->cur;
	v = 0u;
	while(*s != '\0' && isdigit((unsigned char)*s)) {
		unsigned d = (unsigned)(*s - '0');
		/* Pre-multiply saturation.  The post-multiply `nv < v` check
		 * misses overflow when v lands in [SIZE_MAX/10, SIZE_MAX/9):
		 * v*10 wraps but the wrapped result still happens to be ≥ v.
		 * Catch it before the wrap by checking v against the boundary. */
		if(v > ((size_t)-1 - d) / 10u) {
			return -1;
		}
		v = v * 10u + d;
		s++;
	}

	p->cur = s;
	*out = v;
	return 0;
}

static int parse_seq_id(dm_itanium_parser_t *p, size_t *out) {
	size_t v;
	int saw;

	if(p == NULL || out == NULL) {
		return -1;
	}

	v = 0u;
	saw = 0;
	while(isalnum((unsigned char)p->cur[0])) {
		unsigned d;

		saw = 1;
		if(p->cur[0] >= '0' && p->cur[0] <= '9') {
			d = (unsigned)(p->cur[0] - '0');
		} else if(p->cur[0] >= 'A' && p->cur[0] <= 'Z') {
			d = 10u + (unsigned)(p->cur[0] - 'A');
		} else if(p->cur[0] >= 'a' && p->cur[0] <= 'z') {
			d = 10u + (unsigned)(p->cur[0] - 'a');
		} else {
			return -1;
		}

		/* Pre-multiply saturation; see parse_number for details. */
		if(v > ((size_t)-1 - d) / 36u) {
			return -1;
		}
		v = v * 36u + d;
		p->cur++;
	}

	if(!saw) {
		return -1;
	}

	*out = v;
	return 0;
}

static int parse_signed_number_token(dm_itanium_parser_t *p, long long *out) {
	size_t mag;
	int neg;

	if(p == NULL || out == NULL) {
		return -1;
	}

	neg = 0;
	if(p->cur[0] == 'n') {
		neg = 1;
		p->cur++;
	}

	/* past LLONG_MAX it is no long long: it came out wrapped */
	if(parse_number(p, &mag) != 0 || p->cur[0] != '_' || mag > (size_t)LLONG_MAX) {
		return -1;
	}

	p->cur++;
	if(neg) {
		*out = -(long long)mag;
	} else {
		*out = (long long)mag;
	}

	return 0;
}

static int parse_source_name(dm_itanium_parser_t *p) {
	const char *id;
	size_t len;

	if(parse_number(p, &len) != 0) {
		return -1;
	}

	id = p->cur;
	if(len == 0u || !has_n_bytes(id, len)) {
		return -1;
	}

	if(demangle_buf_append(&p->out, id, len) != 0) {
		return -1;
	}

	p->cur += len;

	if(p->cur[0] == 'B') {
		size_t tag_len;
		p->cur++;
		if(parse_number(p, &tag_len) != 0 || tag_len == 0u || !has_n_bytes(p->cur, tag_len)) {
			return -1;
		}
		if(demangle_buf_appendc(&p->out, '[') != 0 || demangle_buf_append(&p->out, "abi:", 4u) != 0 || demangle_buf_append(&p->out, p->cur, tag_len) != 0 ||
		   demangle_buf_appendc(&p->out, ']') != 0) {
			return -1;
		}
		p->cur += tag_len;
	}

	return 0;
}

static int parse_substitution(dm_itanium_parser_t *p) {
	size_t idx;

	if(p->cur[0] != 'S') {
		return -1;
	}

	if(p->cur[1] == '_') {
		p->cur += 2;
		return parser_lookup_substitution(p, 0u);
	}

	/*
	 * The standard abbreviations, as GNU c++filt prints them: Ss, Si, So and
	 * Sd in full -- c++filt is verbose -- and short only for
	 * DEMANGLE_NO_VERBOSE, but for a constructor or destructor, which
	 * is named for the template, basic_string(), as in full.  std_last_len
	 * is the length of that name, after "std::".  This printed std::string
	 * whatever the options, and its constructor std::string::std::string().
	 */
	p->std_last_len = 0u;
	{
		static const struct {
			char code;
			const char *simple;
			const char *full;
			size_t last_len;
		} std_subs[] = {
		    {'a', "std::allocator", "std::allocator", 9u},
		    {'b', "std::basic_string", "std::basic_string", 12u},
		    {'s', "std::string", "std::basic_string<char, std::char_traits<char>, std::allocator<char>>", 12u},
		    {'i', "std::istream", "std::basic_istream<char, std::char_traits<char>>", 13u},
		    {'o', "std::ostream", "std::basic_ostream<char, std::char_traits<char>>", 13u},
		    {'d', "std::iostream", "std::basic_iostream<char, std::char_traits<char>>", 14u},
		};
		size_t k;

		if(p->cur[1] == 't') {
			p->cur += 2;
			return demangle_buf_append(&p->out, "std", 3u);
		}
		for(k = 0u; k < sizeof(std_subs) / sizeof(std_subs[0]); k++) {
			if(p->cur[1] == std_subs[k].code) {
				int full = (p->options & DEMANGLE_NO_VERBOSE) == 0 || (p->sub_is_prefix && (p->cur[2] == 'C' || p->cur[2] == 'D'));
				const char *s = full ? std_subs[k].full : std_subs[k].simple;

				p->cur += 2;
				p->std_last_len = std_subs[k].last_len;
				return demangle_buf_append(&p->out, s, strlen(s));
			}
		}
	}

	p->cur++;
	if(parse_seq_id(p, &idx) != 0 || p->cur[0] != '_') {
		return -1;
	}

	p->cur++;
	return parser_lookup_substitution(p, idx + 1u);
}

static const char *lookup_operator_name(const char *code) {
	static const struct {
		const char code[3];
		const char *name;
	} ops[] = {{"nw", "operator new"}, {"na", "operator new[]"}, {"dl", "operator delete"}, {"da", "operator delete[]"}, {"ps", "operator+"},
	           {"ng", "operator-"},    {"ad", "operator&"},      {"de", "operator*"},       {"co", "operator~"},         {"pl", "operator+"},
	           {"mi", "operator-"},    {"ml", "operator*"},      {"dv", "operator/"},       {"rm", "operator%"},         {"an", "operator&"},
	           {"or", "operator|"},    {"eo", "operator^"},      {"ls", "operator<<"},      {"rs", "operator>>"},        {"eq", "operator=="},
	           {"ne", "operator!="},   {"lt", "operator<"},      {"gt", "operator>"},       {"le", "operator<="},        {"ge", "operator>="},
	           {"ss", "operator<=>"},  {"nt", "operator!"},      {"pp", "operator++"},      {"mm", "operator--"},        {"cm", "operator,"},
	           {"pm", "operator->*"},  {"pt", "operator->"},     {"cl", "operator()"},      {"ix", "operator[]"},        {"qu", "operator?"},
	           {"st", "sizeof"},       {"sz", "sizeof..."},      {"at", "alignof"},         {"az", "alignof..."},        {"li", "operator\"\""},
	           /* assignment, the logical operators and co_await, which
	            * were missing: every class's operator= was refused */
	           {"aS", "operator="},    {"pL", "operator+="},     {"mI", "operator-="},      {"mL", "operator*="},        {"dV", "operator/="},
	           {"rM", "operator%="},   {"aN", "operator&="},     {"oR", "operator|="},      {"eO", "operator^="},        {"lS", "operator<<="},
	           {"rS", "operator>>="},  {"aa", "operator&&"},     {"oo", "operator||"},      {"aw", "operator co_await"}};
	size_t i;

	for(i = 0u; i < sizeof(ops) / sizeof(ops[0]); i++) {
		if(code[0] == ops[i].code[0] && code[1] == ops[i].code[1]) {
			return ops[i].name;
		}
	}
	return NULL;
}

static int parse_operator_name(dm_itanium_parser_t *p) {
	const char *name;

	if(p->cur[0] == 'c' && p->cur[1] == 'v') {
		p->cur += 2;
		if(demangle_buf_append(&p->out, "operator ", 9u) != 0) {
			return -1;
		}
		return parse_type(p);
	}

	if(p->cur[0] == 'v' && isdigit((unsigned char)p->cur[1])) {
		p->cur += 2;
		if(demangle_buf_append(&p->out, "operator ", 9u) != 0) {
			return -1;
		}
		return parse_source_name(p);
	}

	name = lookup_operator_name(p->cur);
	if(name != NULL) {
		p->cur += 2;
		return demangle_buf_append(&p->out, name, strlen(name));
	}

	return -1;
}

static int parse_ctor_dtor_name(dm_itanium_parser_t *p, size_t prev_off, size_t prev_len) {
	int is_dtor;

	if(prev_len == 0u) {
		return -1;
	}

	is_dtor = 0;

	/* C4 and D4 are GCC's unified constructor and destructor, C5 and D5 its
	 * comdat ones: named as the others are, and refused before */
	if(p->cur[0] == 'C' && p->cur[1] >= '1' && p->cur[1] <= '5') {
		p->cur += 2;
	} else if(p->cur[0] == 'C' && p->cur[1] == 'I' && (p->cur[2] == '1' || p->cur[2] == '2')) {
		p->cur += 3;
	} else if(p->cur[0] == 'D' && p->cur[1] >= '0' && p->cur[1] <= '5' && p->cur[1] != '3') {
		p->cur += 2;
		is_dtor = 1;
	} else {
		return -1;
	}

	if(is_dtor && demangle_buf_appendc(&p->out, '~') != 0) {
		return -1;
	}

	return parser_append_output_slice(p, prev_off, prev_len);
}

static int parse_unnamed_type_name(dm_itanium_parser_t *p) {
	size_t id;

	if(p->cur[0] == 'U' && p->cur[1] == 'l') {
		int first;
		int has_id;
		size_t lambda_id;

		p->cur += 2;
		has_id = 0;
		lambda_id = 0u;

		if(demangle_buf_append(&p->out, "{lambda(", 8u) != 0) {
			return -1;
		}

		/* a lambda of no parameters has the one v: {lambda()#1} */
		if(p->cur[0] == 'v' && p->cur[1] == 'E') {
			p->cur++;
		}
		first = 1;
		while(p->cur[0] != '\0' && p->cur[0] != 'E') {
			/* the separator before the parameter, not inserted after it,
			 * which left its substitutions' offsets behind */
			if(!first && demangle_buf_append(&p->out, ", ", 2u) != 0) {
				return -1;
			}
			if(parser_enter(p) != 0) {
				return -1;
			}
			if(parse_type(p) != 0) {
				parser_leave(p);
				return -1;
			}
			parser_leave(p);
			first = 0;
		}

		if(p->cur[0] != 'E') {
			return -1;
		}
		p->cur++;

		if(isdigit((unsigned char)p->cur[0])) {
			if(parse_number(p, &lambda_id) != 0) {
				return -1;
			}
			has_id = 1;
		}

		if(p->cur[0] != '_' || lambda_id > SIZE_MAX - 2u) {
			return -1;
		}
		p->cur++;

		/*
		 * The ABI numbers them from the second, 0_; the first is _ alone.
		 * GNU c++filt prints the count, #1 and #2 -- this printed
		 * {lambda(void)} for the first and #0 for the second.
		 */
		return demangle_buf_printf(&p->out, ")#%zu}", has_id ? lambda_id + 2u : 1u);
	}

	if(!(p->cur[0] == 'U' && p->cur[1] == 't')) {
		return -1;
	}

	p->cur += 2;

	/* numbered as lambdas are: _ the first, #1, and N_ #N+2 */
	if(isdigit((unsigned char)p->cur[0])) {
		if(parse_number(p, &id) != 0) {
			return -1;
		}
		if(p->cur[0] != '_' || id > SIZE_MAX - 2u) {
			return -1;
		}
		p->cur++;
		return demangle_buf_printf(&p->out, "{unnamed type#%zu}", id + 2u);
	}

	if(p->cur[0] != '_') {
		return -1;
	}

	p->cur++;
	return demangle_buf_append(&p->out, "{unnamed type#1}", 16u);
}

static int parse_unqualified_name(dm_itanium_parser_t *p, size_t prev_off, size_t prev_len, size_t *comp_off, size_t *comp_len) {
	dm_parse_mark_t m;

	if(comp_off == NULL || comp_len == NULL) {
		return -1;
	}

	mark_save(p, &m);
	*comp_off = p->out.len;

	if(parse_source_name(p) == 0 || parse_operator_name(p) == 0 || parse_ctor_dtor_name(p, prev_off, prev_len) == 0 || parse_unnamed_type_name(p) == 0) {
		*comp_len = p->out.len - *comp_off;
		return 0;
	}

	mark_restore(p, &m);
	return -1;
}

static int parse_nested_name(dm_itanium_parser_t *p) {
	int rc = -1;
	size_t name_off;
	size_t last_off;
	size_t last_len;
	int first;

	if(p->cur[0] != 'N') {
		return -1;
	}

	if(parser_enter(p) != 0) {
		return -1;
	}

	p->cur++;
	name_off = p->out.len;
	first = 1;
	last_off = 0u;
	last_len = 0u;

	while(p->cur[0] == 'K' || p->cur[0] == 'V' || p->cur[0] == 'r') {
		if(p->cur[0] == 'K') {
			p->member_quals |= DM_MEMBER_CONST;
		} else if(p->cur[0] == 'V') {
			p->member_quals |= DM_MEMBER_VOLATILE;
		} else {
			p->member_quals |= DM_MEMBER_RESTRICT;
		}
		p->cur++;
	}

	if(p->cur[0] == 'R') {
		p->member_quals |= DM_MEMBER_REF_LVALUE;
		p->cur++;
	} else if(p->cur[0] == 'O') {
		p->member_quals |= DM_MEMBER_REF_RVALUE;
		p->cur++;
	}

	while(p->cur[0] != '\0' && p->cur[0] != 'E') {
		int from_sub;
		int had_args;
		size_t comp_off, comp_len;
		size_t name_only_off, name_only_len;
		dm_parse_mark_t comp_mark;

		if(!first && demangle_buf_append(&p->out, "::", 2u) != 0) {
			goto out;
		}

		from_sub = 0;
		had_args = 0;
		if(parse_unqualified_name(p, last_off, last_len, &comp_off, &comp_len) != 0) {
			int sub_rc;

			mark_save(p, &comp_mark);
			comp_off = p->out.len;
			p->sub_is_prefix = 1;
			sub_rc = parse_substitution(p);
			p->sub_is_prefix = 0;
			if(sub_rc != 0) {
				mark_restore(p, &comp_mark);
				goto out;
			}
			comp_len = p->out.len - comp_off;
			from_sub = 1;
		}

		name_only_off = comp_off;
		name_only_len = comp_len;
		/* a constructor of std::basic_string<...> is basic_string(): the
		 * name after "std::" (a short std::string is never one's) */
		if(from_sub && p->std_last_len != 0u && comp_len >= 5u + p->std_last_len) {
			name_only_off = comp_off + 5u;
			name_only_len = p->std_last_len;
		}

		if(p->cur[0] == 'I') {
			/* the template's name, before its arguments: a substitution
			 * is one already */
			if(!from_sub) {
				if(parser_add_substitution(p, name_off, p->out.len - name_off) != 0) {
					goto out;
				}
			}

			if(parse_template_args(p) != 0) {
				goto out;
			}
			comp_len = p->out.len - comp_off;
			had_args = 1;
			p->has_return_type = 1;
		} else {
			p->has_return_type = 0;
		}

		last_off = name_only_off;
		last_len = name_only_len;

		/*
		 * The prefix so far is a candidate (Itanium ABI 5.1.9) -- but not a
		 * substitution that came as it was, which is in the table already
		 * (NS_3FooE entered S_ again, and every later index was one off),
		 * and not a function's own name.  A substitution given template
		 * arguments is a new template-id: Sa<char>, std::allocator<char>,
		 * was left out with St's std::, which alone is no candidate.
		 */
		if((!from_sub || had_args) && (p->cur[0] != 'E' || p->in_type_context)) {
			if(parser_add_substitution(p, name_off, p->out.len - name_off) != 0) {
				goto out;
			}
		}

		first = 0;
	}

	if(first || p->cur[0] != 'E') {
		goto out;
	}

	p->cur++;

	rc = 0;
out:
	parser_leave(p);
	return rc;
}

static int parse_unscoped_name(dm_itanium_parser_t *p) {
	size_t name_off;
	size_t comp_off;
	size_t comp_len;
	int has_std_prefix;
	int from_sub;
	int had_args;
	dm_parse_mark_t m;

	name_off = p->out.len;
	has_std_prefix = 0;
	from_sub = 0;
	had_args = 0;

	if(p->cur[0] == 'S' && p->cur[1] == 't') {
		p->cur += 2;
		if(demangle_buf_append(&p->out, "std::", 5u) != 0) {
			return -1;
		}
		has_std_prefix = 1;
	}

	if(parse_unqualified_name(p, 0u, 0u, &comp_off, &comp_len) != 0) {
		mark_save(p, &m);
		comp_off = p->out.len;
		if(parse_substitution(p) != 0) {
			mark_restore(p, &m);
			if(has_std_prefix) {
				p->out.len = name_off;
				p->out.data[p->out.len] = '\0';
				p->cur -= 2;
			}
			return -1;
		}
		comp_len = p->out.len - comp_off;
		from_sub = 1;
	}

	if(p->cur[0] == 'I') {
		if(!from_sub) {
			if(parser_add_substitution(p, name_off, p->out.len - name_off) != 0) {
				return -1;
			}
		}

		if(parse_template_args(p) != 0) {
			return -1;
		}
		p->has_return_type = 1;
		had_args = 1;
	} else {
		p->has_return_type = 0;
	}

	/*
	 * The name is a candidate in a type -- a class, or a template-id such
	 * as Sa's std::allocator<char> -- and not otherwise: a function's own
	 * name is none (the template name before its arguments is entered
	 * above), nor a substitution that came as it was.  _Z1fSaIcES_ read
	 * S_ as f.
	 */
	if(p->in_type_context && (had_args || !from_sub) && parser_add_substitution(p, name_off, p->out.len - name_off) != 0) {
		return -1;
	}

	return 0;
}

/*
 * <local-name> = Z <encoding> E <entity name> [<discriminator>]
 *              | Z <encoding> E s [<discriminator>]
 *              | Z <encoding> E d [<number>] _ <entity name>
 * main()::x, main()::{lambda()#1}, main()::string literal,
 * f()::{default arg#1}::x, as GNU c++filt prints them.  The encoding ends
 * at the E (local_depth), where the top-level one ends at the end of the
 * symbol.  A discriminator, _<digit> or __<number>_, is not printed.
 */
static int parse_local_name(dm_itanium_parser_t *p) {
	int rc;

	if(p->cur[0] != 'Z') {
		return -1;
	}

	p->cur++;

	if(parser_enter(p) != 0) {
		return -1;
	}

	p->local_depth++;
	p->next_encoding_is_local = 1;
	rc = parse_encoding(p);
	p->local_depth--;
	parser_leave(p);
	if(rc != 0 || p->cur[0] != 'E') {
		return -1;
	}

	p->cur++;

	if(demangle_buf_append(&p->out, "::", 2u) != 0) {
		return -1;
	}

	if(p->cur[0] == 's') {
		p->cur++;
		if(demangle_buf_append(&p->out, "string literal", 14u) != 0) {
			return -1;
		}
	} else {
		if(p->cur[0] == 'd') {
			size_t n = 0u;

			p->cur++;
			if(isdigit((unsigned char)p->cur[0]) && parse_number(p, &n) != 0) {
				return -1;
			}
			if(p->cur[0] != '_' || n == SIZE_MAX) {
				return -1;
			}
			p->cur++;
			/* d_ is the last parameter's, numbered from the end: #1 */
			if(demangle_buf_printf(&p->out, "{default arg#%zu}::", isdigit((unsigned char)p->cur[-2]) ? n + 2u : 1u) != 0) {
				return -1;
			}
		}
		if(parse_name(p) != 0) {
			return -1;
		}
	}

	if(p->cur[0] == '_') {
		if(p->cur[1] == '_') {
			p->cur += 2;
			while(isdigit((unsigned char)p->cur[0])) {
				p->cur++;
			}
			if(p->cur[0] != '_') {
				return -1;
			}
			p->cur++;
		} else if(isdigit((unsigned char)p->cur[1])) {
			p->cur += 2;
		}
	}

	return 0;
}

static int parse_name(dm_itanium_parser_t *p) {
	if(p->cur[0] == 'N') {
		return parse_nested_name(p);
	}

	if(p->cur[0] == 'Z') {
		return parse_local_name(p);
	}

	return parse_unscoped_name(p);
}

static int append_member_qualifiers(dm_itanium_parser_t *p) {
	if((p->options & DEMANGLE_NO_VERBOSE) != 0) {
		p->member_quals = 0u;
		return 0;
	}

	if((p->member_quals & DM_MEMBER_CONST) != 0 && demangle_buf_append(&p->out, " const", 6u) != 0) {
		return -1;
	}
	if((p->member_quals & DM_MEMBER_VOLATILE) != 0 && demangle_buf_append(&p->out, " volatile", 9u) != 0) {
		return -1;
	}
	if((p->member_quals & DM_MEMBER_RESTRICT) != 0 && demangle_buf_append(&p->out, " restrict", 9u) != 0) {
		return -1;
	}
	if((p->member_quals & DM_MEMBER_REF_LVALUE) != 0 && demangle_buf_append(&p->out, " &", 2u) != 0) {
		return -1;
	}
	if((p->member_quals & DM_MEMBER_REF_RVALUE) != 0 && demangle_buf_append(&p->out, " &&", 3u) != 0) {
		return -1;
	}

	p->member_quals = 0u;
	return 0;
}

/* The encoding being parsed ends here: at the end of the symbol, or in a
 * local name, Z <encoding> E, at its E -- which was read as a parameter
 * type, and every local name refused. */
static int at_encoding_end(const dm_itanium_parser_t *p) {
	return p->cur[0] == '\0' || (p->local_depth > 0 && p->cur[0] == 'E');
}

static int is_void_text(const dm_buf_t *buf, size_t off, size_t len) {
	return len == 4u && off + len <= buf->len && memcmp(buf->data + off, "void", 4u) == 0;
}

static int parse_bare_function_type(dm_itanium_parser_t *p) {
	int suppress_params;
	int first;
	int drop;

	suppress_params = ((p->options & DEMANGLE_NO_PARAMS) != 0);

	drop = p->drop_return_type;
	p->drop_return_type = 0;

	/* Template functions have an explicit return type as the first type */
	if(p->has_return_type && !at_encoding_end(p)) {
		size_t ret_off = p->out.len;

		if(parse_type(p) != 0) {
			return -1;
		}
		size_t ret_len = p->out.len - ret_off;
		/*
		 * The function a local name is in is printed without its return
		 * type, as GNU c++filt prints it: A::f<int>()::x, not
		 * void A::f<int>()::x.  Its entries keep their text.
		 */
		if(drop && ret_len > 0) {
			if(parser_freeze_from(p, ret_off) != 0) {
				return -1;
			}
			p->out.len = ret_off;
			p->out.data[p->out.len] = '\0';
			ret_len = 0;
		}
		if(ret_len > 0) {
			/*
			 * The return type, written after the name, goes before it: at
			 * the start of this encoding, not of the whole output, which
			 * put it ahead of a thunk's "non-virtual thunk to " or a local
			 * name's scope.  The offsets the tables hold move with their
			 * text: those inside the return type to its new place, those
			 * between the encoding's start and it along by its length;
			 * all of them were shifted alike.
			 */
			size_t at = p->encoding_off;
			size_t shift = ret_len + 1u; /* the return type and a space */
			char *ret_text = (char *)malloc(ret_len + 1);
			size_t si;

			if(!ret_text)
				return -1;
			memcpy(ret_text, p->out.data + ret_off, ret_len);
			p->out.len = ret_off;
			p->out.data[p->out.len] = '\0';
			if(at > ret_off || demangle_buf_insert(&p->out, at, " ", 1u) != 0 || demangle_buf_insert(&p->out, at, ret_text, ret_len) != 0) {
				free(ret_text);
				return -1;
			}
			free(ret_text);
#define DM_MOVE_OFF(o) ((o) >= ret_off ? at + ((o) - ret_off) : (o) >= at ? (o) + shift : (o))
			for(si = 0; si < p->substitution_count; si++) {
				p->substitutions[si].off = DM_MOVE_OFF(p->substitutions[si].off);
			}
			for(si = 0; si < p->last_template_count; si++) {
				p->last_template_off[si] = DM_MOVE_OFF(p->last_template_off[si]);
			}
			for(si = 0; si < p->func_tmpl_count; si++) {
				p->func_tmpl_off[si] = DM_MOVE_OFF(p->func_tmpl_off[si]);
			}
#undef DM_MOVE_OFF
		}
		p->has_return_type = 0;
	}

	if(!suppress_params && demangle_buf_appendc(&p->out, '(') != 0) {
		return -1;
	}

	first = 1;
	while(!at_encoding_end(p)) {
		size_t sep;
		size_t before;
		size_t parsed_len;

		/* the separator first: inserted after the parameter, it moved
		 * the parameter's text and not the substitutions entered in it,
		 * which then read two bytes early (", ll" for llvm) */
		sep = p->out.len;
		if(!first && !suppress_params && demangle_buf_append(&p->out, ", ", 2u) != 0) {
			return -1;
		}
		before = p->out.len;
		if(parse_type(p) != 0) {
			return -1;
		}

		parsed_len = p->out.len - before;
		if(first && at_encoding_end(p) && is_void_text(&p->out, before, parsed_len)) {
			p->out.len = before;
			p->out.data[p->out.len] = '\0';
			break;
		}

		if(suppress_params || parsed_len == 0u) {
			/* an empty pack expansion is no parameter: f<>() for DpT_ */
			p->out.len = sep;
			p->out.data[p->out.len] = '\0';
			if(parsed_len == 0u) {
				continue;
			}
		}

		first = 0;
	}

	if(!suppress_params && demangle_buf_appendc(&p->out, ')') != 0) {
		return -1;
	}

	return append_member_qualifiers(p);
}

static int parse_template_args(dm_itanium_parser_t *p) {
	int first;
	int pushed;

	if(p->cur[0] != 'I') {
		return -1;
	}

	p->cur++;
	pushed = 0;

	if(parser_push_template_frame(p) != 0) {
		return -1;
	}
	pushed = 1;

	/* after operator< or operator<<, a space: operator<< <char>, as GNU
	 * c++filt writes it, not operator<<<char> */
	if(p->out.len > 0u && p->out.data[p->out.len - 1u] == '<' && demangle_buf_appendc(&p->out, ' ') != 0) {
		goto fail;
	}
	if(demangle_buf_appendc(&p->out, '<') != 0) {
		goto fail;
	}

	first = 1;
	while(p->cur[0] != '\0' && p->cur[0] != 'E') {
		size_t arg_off;
		size_t elem_first;
		int arg_rc;

		if(!first && demangle_buf_append(&p->out, ", ", 2u) != 0) {
			goto fail;
		}

		arg_off = p->out.len;
		elem_first = p->template_frames[p->template_depth - 1u].arg_count;
		arg_rc = parse_template_arg(p);
		if(arg_rc < 0) {
			goto fail;
		}

		/* arg_rc == 1 means pack (elements already recorded individually) */
		if(arg_rc == 0) {
			if(parser_record_template_arg(p, arg_off, p->out.len - arg_off) != 0) {
				goto fail;
			}
		}
		/* the argument T_ names: the element, or the pack's run of them */
		if(parser_record_template_map(p, elem_first, p->template_frames[p->template_depth - 1u].arg_count - elem_first, arg_rc == 1) != 0) {
			goto fail;
		}
		if(arg_rc == 1 && p->out.len == arg_off) {
			/* an empty pack leaves no ", " behind it: f<int> for IiJEE */
			if(!first) {
				p->out.len -= 2u;
				p->out.data[p->out.len] = '\0';
			}
			continue;
		}

		first = 0;
	}

	if(p->cur[0] != 'E') {
		goto fail;
	}

	p->cur++;

	if(demangle_buf_appendc(&p->out, '>') != 0) {
		goto fail;
	}

	if(parser_capture_template_frame(p) != 0) {
		goto fail;
	}

	if(parser_pop_template_frame(p) != 0) {
		goto fail;
	}

	return 0;

fail:
	if(pushed) {
		(void)parser_pop_template_frame(p);
	}
	return -1;
}

static int parse_template_arg(dm_itanium_parser_t *p) {
	dm_parse_mark_t m;
	int ret;

	/* J<args>E pack expansion recurses into parse_template_arg per
	 * element, and each element can itself be J<...>E.  Bound the
	 * nesting via parser_enter; without it a deeply-nested pack
	 * input could blow the stack. */
	if(parser_enter(p) != 0) {
		return -1;
	}

	if(p->cur[0] == 'X') {
		p->cur++;
		if(parse_expression(p) != 0 || p->cur[0] != 'E') {
			parser_leave(p);
			return -1;
		}
		p->cur++;
		parser_leave(p);
		return 0;
	}

	if(p->cur[0] == 'J') {
		int first;
		int is_in_template_args;

		p->cur++;
		/* Check if we're being called from parse_template_args (has a pushed frame) */
		is_in_template_args = (p->template_depth > 0);

		first = 1;
		while(p->cur[0] != '\0' && p->cur[0] != 'E') {
			size_t elem_off;
			if(!first && demangle_buf_append(&p->out, ", ", 2u) != 0) {
				parser_leave(p);
				return -1;
			}
			elem_off = p->out.len;
			if(parse_template_arg(p) != 0) {
				parser_leave(p);
				return -1;
			}
			/* Record each pack element as a separate template arg */
			if(is_in_template_args) {
				if(parser_record_template_arg(p, elem_off, p->out.len - elem_off) != 0) {
					parser_leave(p);
					return -1;
				}
			}
			first = 0;
		}

		if(p->cur[0] != 'E') {
			parser_leave(p);
			return -1;
		}

		p->cur++;
		parser_leave(p);
		return 1; /* Return 1 to indicate pack (elements already recorded) */
	}

	mark_save(p, &m);
	if(parse_type(p) == 0) {
		parser_leave(p);
		return 0;
	}

	mark_restore(p, &m);
	ret = parse_expr_primary(p);
	parser_leave(p);
	return ret;
}

static int parse_expr_primary(dm_itanium_parser_t *p) {
	dm_parse_mark_t m;

	if(p->cur[0] == 'L') {
		size_t num_len;
		char tcode;
		int builtin, is_float, is_bool;
		const char *suffix;

		p->cur++;
		tcode = p->cur[0];

		if(p->cur[0] == 'D' && p->cur[1] == 'n' && p->cur[2] == 'E') {
			p->cur += 3;
			return demangle_buf_append(&p->out, "nullptr", 7u);
		}

		/* L _Z <encoding> E, an entity's address or reference as a
		 * template argument: f<llvm::APFloatBase::semBFloat>; refused
		 * before.  Its encoding ends at the E, as a local name's does. */
		if(p->cur[0] == '_' && p->cur[1] == 'Z') {
			int rc;

			p->cur += 2;
			if(parser_enter(p) != 0) {
				return -1;
			}
			p->local_depth++;
			rc = parse_encoding(p);
			p->local_depth--;
			parser_leave(p);
			if(rc != 0 || p->cur[0] != 'E') {
				return -1;
			}
			p->cur++;
			return 0;
		}

		/*
		 * The literal is printed as GNU c++filt prints it, by its type.
		 * int is the value alone; unsigned, long, unsigned long, long
		 * long and unsigned long long the value with its suffix, 8u, 4096ul;
		 * a bool false or true.  Any other type -- a class or enum, a
		 * char, a short -- keeps its type, (unsigned short)7, and a
		 * floating one its encoding in hex too, (double)[3ff0000000000000].
		 * Every builtin type but int and bool was printed bare, 8 for 8u.
		 * The type's text stays where it has substitutions, whose offsets
		 * point at it; a type that fails to parse undoes all it did.
		 */
		mark_save(p, &m);
		if(demangle_buf_appendc(&p->out, '(') != 0 || parse_type(p) != 0) {
			mark_restore(p, &m);
			return -1;
		}
		builtin = (p->substitution_count == m.substitution_count);
		suffix = NULL;
		if(builtin) {
			switch(tcode) {
			case 'i': suffix = ""; break;
			case 'j': suffix = "u"; break;
			case 'l': suffix = "l"; break;
			case 'm': suffix = "ul"; break;
			case 'x': suffix = "ll"; break;
			case 'y': suffix = "ull"; break;
			default: break;
			}
		}
		is_float = builtin && (tcode == 'f' || tcode == 'd' || tcode == 'e' || tcode == 'g');
		is_bool = builtin && tcode == 'b';
		if(suffix != NULL || (is_bool && (p->cur[0] == '0' || p->cur[0] == '1') && p->cur[1] == 'E')) {
			p->out.len = m.out_len;     /* the value alone */
			p->out.data[p->out.len] = '\0';
		} else if(demangle_buf_appendc(&p->out, ')') != 0) {
			return -1;
		}
		if(is_bool && p->out.len == m.out_len) {
			p->cur += 2u;
			return p->cur[-2] == '1' ? demangle_buf_append(&p->out, "true", 4u) : demangle_buf_append(&p->out, "false", 5u);
		}

		if(p->cur[0] == 'n') {
			if(demangle_buf_appendc(&p->out, '-') != 0) {
				return -1;
			}
			p->cur++;
		}

		/* a float's value is its encoding, in lower-case hex */
		num_len = 0u;
		while(isdigit((unsigned char)p->cur[num_len]) || (is_float && p->cur[num_len] >= 'a' && p->cur[num_len] <= 'f')) {
			num_len++;
		}

		if(num_len == 0u || p->cur[num_len] != 'E') {
			return -1;
		}

		if((is_float && demangle_buf_appendc(&p->out, '[') != 0) || demangle_buf_append(&p->out, p->cur, num_len) != 0 ||
		   (is_float && demangle_buf_appendc(&p->out, ']') != 0) || (suffix != NULL && demangle_buf_append(&p->out, suffix, strlen(suffix)) != 0)) {
			return -1;
		}

		p->cur += num_len + 1u;
		return 0;
	}

	return -1;
}

static int parse_cast_expression(dm_itanium_parser_t *p) {
	if(!(p->cur[0] == 'c' && p->cur[1] == 'v')) {
		return -1;
	}

	p->cur += 2;
	if(demangle_buf_appendc(&p->out, '(') != 0) {
		return -1;
	}
	if(parse_type(p) != 0) {
		return -1;
	}
	if(demangle_buf_appendc(&p->out, ')') != 0) {
		return -1;
	}
	return parse_expression(p);
}

static int parse_fold_expression(dm_itanium_parser_t *p) {
	if(p->cur[0] != 'f' || (p->cur[1] != 'L' && p->cur[1] != 'R' && p->cur[1] != 'l' && p->cur[1] != 'r')) {
		return -1;
	}

	if(demangle_buf_printf(&p->out, "fold[%c%c](", p->cur[0], p->cur[1]) != 0) {
		return -1;
	}
	p->cur += 2;

	if(parse_expression(p) != 0) {
		return -1;
	}

	if(p->cur[0] != 'E') {
		if(demangle_buf_append(&p->out, ", ", 2u) != 0) {
			return -1;
		}
		if(parse_expression(p) != 0) {
			return -1;
		}
	}

	return demangle_buf_appendc(&p->out, ')');
}

static int parse_pack_expansion(dm_itanium_parser_t *p) {
	if(!(p->cur[0] == 's' && p->cur[1] == 'p')) {
		return -1;
	}

	p->cur += 2;
	if(demangle_buf_append(&p->out, "pack_expand(", 12u) != 0) {
		return -1;
	}
	if(parse_expression(p) != 0) {
		return -1;
	}
	return demangle_buf_appendc(&p->out, ')');
}

static int parse_expression_operator(dm_itanium_parser_t *p) {
	static const struct {
		const char code[3];
		const char *name;
		int arity;
	} ops[] = {{"cl", "call", 2},      {"ix", "index", 2},   {"qu", "cond", 3},      {"st", "sizeof", 1},
	           {"sz", "sizeof...", 1}, {"at", "alignof", 1}, {"az", "alignof...", 1}};
	size_t i;
	int argi;

	/*
	 * Expression opcodes are a fixed two-byte token stream from the ABI.
	 * Keep this table flat and exhaustive so transition behavior is explicit.
	 */
	for(i = 0u; i < sizeof(ops) / sizeof(ops[0]); i++) {
		if(p->cur[0] == ops[i].code[0] && p->cur[1] == ops[i].code[1]) {
			p->cur += 2;
			if(demangle_buf_append(&p->out, ops[i].name, strlen(ops[i].name)) != 0 || demangle_buf_appendc(&p->out, '(') != 0) {
				return -1;
			}

			for(argi = 0; argi < ops[i].arity; argi++) {
				if(argi > 0 && demangle_buf_append(&p->out, ", ", 2u) != 0) {
					return -1;
				}
				if(parse_expression(p) != 0) {
					return -1;
				}
			}

			return demangle_buf_appendc(&p->out, ')');
		}
	}

	return -1;
}

static int parse_expression(dm_itanium_parser_t *p) {
	dm_parse_mark_t m;
	int ok;

	if(parser_enter(p) != 0) {
		return -1;
	}

	mark_save(p, &m);
	ok = parse_expr_primary(p);
	if(ok == 0) {
		parser_leave(p);
		return 0;
	}

	mark_restore(p, &m);
	ok = parse_cast_expression(p);
	if(ok == 0) {
		parser_leave(p);
		return 0;
	}

	mark_restore(p, &m);
	ok = parse_fold_expression(p);
	if(ok == 0) {
		parser_leave(p);
		return 0;
	}

	mark_restore(p, &m);
	ok = parse_pack_expansion(p);
	if(ok == 0) {
		parser_leave(p);
		return 0;
	}

	mark_restore(p, &m);
	ok = parse_expression_operator(p);
	if(ok == 0) {
		parser_leave(p);
		return 0;
	}

	mark_restore(p, &m);
	ok = parse_name(p);
	parser_leave(p);
	return ok;
}

static int parse_template_param(dm_itanium_parser_t *p) {
	size_t idx;

	if(p->cur[0] != 'T') {
		return -1;
	}

	p->cur++;
	if(p->cur[0] == '_') {
		idx = 0u;
		p->cur++;
	} else {
		size_t seq;

		if(parse_seq_id(p, &seq) != 0 || p->cur[0] != '_') {
			return -1;
		}

		p->cur++;
		idx = seq + 1u;
	}

	/* Prefer function-level template args when available */
	if(p->func_map.count > 0) {
		return append_template_arg(p, &p->func_map, p->func_tmpl_off, p->func_tmpl_len, p->func_tmpl_count, idx);
	}
	return append_template_arg(p, &p->last_map, p->last_template_off, p->last_template_len, p->last_template_count, idx);
}

/*
 * Append the template argument @p idx of @p map, whose elements' text
 * @p off and @p len give.  A pack in a pack expansion gives its element
 * pack_index, and its length to pack_seen; elsewhere all its elements.
 * T_ named an element, not an argument: a pack's elements took every
 * index after it, and an empty one none.
 */
static int append_template_arg(dm_itanium_parser_t *p, const dm_targ_map_t *map, const size_t *off, const size_t *len, size_t elems, size_t idx) {
	size_t i;

	if(idx >= map->count || map->first[idx] + map->n[idx] > elems) {
		return -1;
	}
	if(!map->pack[idx]) {
		i = map->first[idx];
		return parser_append_output_slice(p, off[i], len[i]);
	}
	if(p->pack_expanding) {
		p->pack_seen = map->n[idx];
		if(p->pack_index >= map->n[idx]) {
			return 0;
		}
		i = map->first[idx] + p->pack_index;
		return parser_append_output_slice(p, off[i], len[i]);
	}
	for(i = 0u; i < map->n[idx]; i++) {
		if(i > 0u && demangle_buf_append(&p->out, ", ", 2u) != 0) {
			return -1;
		}
		if(parser_append_output_slice(p, off[map->first[idx] + i], len[map->first[idx] + i]) != 0) {
			return -1;
		}
	}
	return 0;
}

static int parse_function_type(dm_itanium_parser_t *p) {
	int first;
	int extern_c;
	size_t params_off;

	if(p->cur[0] != 'F') {
		return -1;
	}

	p->cur++;
	extern_c = 0;

	if(p->cur[0] == 'Y') {
		extern_c = 1;
		p->cur++;
	}

	if(extern_c && demangle_buf_append(&p->out, "extern \"C\" ", 11u) != 0) {
		return -1;
	}

	if(parse_type(p) != 0) {
		return -1;
	}

	if(demangle_buf_append(&p->out, " (", 2u) != 0) {
		return -1;
	}
	params_off = p->out.len - 1u;

	first = 1;
	while(p->cur[0] != '\0' && p->cur[0] != 'E') {
		size_t before;
		size_t parsed_len;

		/* the separator before the parameter (see parse_bare_function_type) */
		if(!first && demangle_buf_append(&p->out, ", ", 2u) != 0) {
			return -1;
		}
		before = p->out.len;
		if(parse_type(p) != 0) {
			return -1;
		}

		parsed_len = p->out.len - before;
		if(first && p->cur[0] == 'E' && is_void_text(&p->out, before, parsed_len)) {
			p->out.len = before;
			p->out.data[p->out.len] = '\0';
			break;
		}
		if(parsed_len == 0u) {
			/* an empty pack expansion, and its separator, go */
			p->out.len = first ? before : before - 2u;
			p->out.data[p->out.len] = '\0';
			continue;
		}

		first = 0;
	}

	if(p->cur[0] != 'E') {
		return -1;
	}

	p->cur++;

	/* where a pointer's or reference's declarator goes, (*): before the
	 * parameters, as for an array before its bounds */
	p->array_dims_off = params_off;
	p->decl_close = SIZE_MAX;
	return demangle_buf_appendc(&p->out, ')');
}

static int parse_array_type(dm_itanium_parser_t *p) {
	size_t dim;
	int has_dim;

	if(p->cur[0] != 'A') {
		return -1;
	}

	p->cur++;

	has_dim = 0;
	dim = 0u;
	if(isdigit((unsigned char)p->cur[0])) {
		has_dim = 1;
		if(parse_number(p, &dim) != 0) {
			return -1;
		}
	}

	if(p->cur[0] != '_') {
		return -1;
	}

	p->cur++;

	{
		size_t elem_off = p->out.len;
		int elem_is_array = (p->cur[0] == 'A');
		char dims[32];
		int n;

		if(parse_type(p) != 0) {
			return -1;
		}
		n = has_dim ? snprintf(dims, sizeof(dims), "[%zu]", dim) : snprintf(dims, sizeof(dims), "[]");
		if(n < 0 || (size_t)n >= sizeof(dims)) {
			return -1;
		}
		/*
		 * An array of arrays: this bound is the outer one, and goes before
		 * the element's, int[2][3] for A2_A3_i -- appended, it came out
		 * int[3][2].
		 */
		if(elem_is_array) {
			size_t at = p->array_dims_off;

			if(parser_insert_in_type(p, elem_off, at, dims, (size_t)n) != 0) {
				return -1;
			}
			p->array_dims_off = at;
		} else {
			p->array_dims_off = p->out.len;
			if(demangle_buf_append(&p->out, dims, (size_t)n) != 0) {
				return -1;
			}
		}
		p->decl_close = SIZE_MAX;
		return 0;
	}
}

/*
 * M <class type> <member type>: as GNU c++filt prints it, int A::* to a
 * data member, void (A::*)(int) const to a member function -- the class
 * in the declarator, before the parameters -- and int (A::*)[4] to an
 * array.  This printed the class first, A::*void (int) const.  The class's
 * text moves, so the substitutions entered in it keep their text.
 */
static int parse_pointer_to_member(dm_itanium_parser_t *p) {
	size_t class_off;
	size_t class_len;
	size_t member_off;
	const char *q;
	char *decl;
	int rc;

	if(p->cur[0] != 'M') {
		return -1;
	}

	p->cur++;

	class_off = p->out.len;
	if(parse_type(p) != 0) {
		return -1;
	}
	class_len = p->out.len - class_off;
	decl = (char *)malloc(class_len + 6u);
	if(decl == NULL) {
		return -1;
	}
	decl[0] = ' ';
	decl[1] = '(';
	memcpy(decl + 2, p->out.data + class_off, class_len);
	memcpy(decl + 2 + class_len, "::*)", 4u);
	if(parser_freeze_from(p, class_off) != 0) {
		free(decl);
		return -1;
	}
	p->out.len = class_off;
	p->out.data[p->out.len] = '\0';

	/* what the member is, past its cv-qualifiers */
	for(q = p->cur; *q == 'K' || *q == 'V' || *q == 'r'; q++) {
	}
	member_off = p->out.len;
	if(parse_type(p) != 0) {
		free(decl);
		return -1;
	}
	if(*q == 'F') {
		/* void (A::*)(int): no space before it, as for (*) */
		rc = parser_insert_in_type(p, member_off, p->array_dims_off, decl + 1, class_len + 5u);
	} else if(*q == 'A') {
		rc = parser_insert_in_type(p, member_off, p->array_dims_off, decl, class_len + 6u);
	} else {
		decl[1] = ' ';
		rc = demangle_buf_append(&p->out, decl + 1, class_len + 4u);    /* " A::*" */
		p->decl_close = SIZE_MAX;
	}
	free(decl);
	return rc;
}

static int parse_decltype_type(dm_itanium_parser_t *p) {
	if(p->cur[0] != 'D' || (p->cur[1] != 't' && p->cur[1] != 'T')) {
		return -1;
	}

	p->cur += 2;

	if(demangle_buf_append(&p->out, "decltype(", 9u) != 0) {
		return -1;
	}

	if(parse_expression(p) != 0) {
		return -1;
	}

	if(p->cur[0] != 'E') {
		return -1;
	}

	p->cur++;
	return demangle_buf_appendc(&p->out, ')');
}

static int parse_vendor_type(dm_itanium_parser_t *p) {
	if(p->cur[0] != 'u') {
		return -1;
	}

	p->cur++;
	if(demangle_buf_append(&p->out, "vendor(", 7u) != 0) {
		return -1;
	}

	if(parse_source_name(p) != 0) {
		return -1;
	}

	return demangle_buf_appendc(&p->out, ')');
}

static int parse_type_inner(dm_itanium_parser_t *p);

static int parse_type(dm_itanium_parser_t *p) {
	size_t type_off;
	int rc;
	char ch;
	int needs_sub;
	int saved_type_ctx;

	/* Type production recurses through parse_type_inner into
	 * pointer/array/template/function-type sub-productions, which
	 * themselves recurse back into parse_type.  Without an entry
	 * count here the only protection was at parse_nested_name /
	 * parse_local_name / parse_expression / lambda — leaving
	 * P-PP-PPPi or deeply-nested templates free to blow the kernel
	 * stack.  Bound it. */
	if(parser_enter(p) != 0) {
		return -1;
	}

	type_off = p->out.len;
	ch = p->cur[0];

	saved_type_ctx = p->in_type_context;
	p->in_type_context = 1;

	rc = parse_type_inner(p);
	p->in_type_context = saved_type_ctx;
	if(rc != 0) {
		parser_leave(p);
		return rc;
	}

	needs_sub = 0;
	switch(ch) {
	case 'P':
	case 'R':
	case 'O':
	case 'K':
	case 'V':
	case 'r':
	case 'A':
	case 'F':
	case 'M':
	case 'C':
	case 'G':
	case 'T': /* a template parameter used as a type (ABI 5.1.9) */
		needs_sub = 1;
		break;
	default:
		break;
	}

	if(needs_sub && p->out.len > type_off) {
		if(parser_add_substitution(p, type_off, p->out.len - type_off) != 0) {
			parser_leave(p);
			return -1;
		}
	}

	parser_leave(p);
	return 0;
}

/*
 * Dp <type>: a pack expansion, the pattern <type> once for each element of
 * the pack a template parameter in it names, joined by ", ": DpRKT0_ is
 * "int const&, char const&" for a pack of int and char, and nothing for an
 * empty one.  The pattern's substitution candidates are the mangled
 * name's, entered once, with the text of the last element.  This took T_
 * to be the pack, whichever parameter the pattern named, and parsed the
 * pattern once for a pack of none.
 */
static int parse_pack_expansion_type(dm_itanium_parser_t *p) {
	const char *pattern;
	const char *end;
	size_t start;
	size_t first_sub;
	size_t subs;
	size_t n;
	size_t i;
	size_t j;
	int saved_expanding = p->pack_expanding;
	size_t saved_index = p->pack_index;
	size_t saved_seen = p->pack_seen;
	int rc = -1;

	p->cur += 2;
	pattern = p->cur;
	start = p->out.len;

	p->pack_expanding = 1;
	p->pack_index = 0u;
	p->pack_seen = SIZE_MAX;
	first_sub = p->substitution_count;
	if(parse_type(p) != 0) {
		goto out;
	}
	end = p->cur;
	subs = p->substitution_count;
	n = p->pack_seen;

	if(n == 0u) {
		/* the entries made keep the text they had */
		if(parser_freeze_from(p, start) != 0) {
			goto out;
		}
		p->out.len = start;
		p->out.data[p->out.len] = '\0';
	}
	/* SIZE_MAX: no pack named -- a pack of the enclosing class's, which
	 * this parser keeps no elements of -- and the pattern stands alone */
	for(i = 1u; n != SIZE_MAX && i < n; i++) {
		if(demangle_buf_append(&p->out, ", ", 2u) != 0) {
			goto out;
		}
		p->pack_index = i;
		p->cur = pattern;
		if(parse_type(p) != 0) {
			goto out;
		}
		if(p->cur != end || p->substitution_count - subs != subs - first_sub) {
			goto out;
		}
		/* the entries are what the last element's parse made, as GNU's:
		 * an S_ after DpPT_ for a pack of int and char is char* */
		for(j = 0u; j < subs - first_sub; j++) {
			dm_subst_t *to = &p->substitutions[first_sub + j];
			dm_subst_t *from = &p->substitutions[subs + j];

			free(to->text);
			*to = *from;
			from->text = NULL;
		}
		p->substitution_count = subs;
	}
	rc = 0;

out:
	p->pack_expanding = saved_expanding;
	p->pack_index = saved_index;
	p->pack_seen = saved_seen;
	return rc;
}

static int parse_type_inner(dm_itanium_parser_t *p) {
	static const struct {
		char code;
		const char *name;
	} builtin[] = {{'v', "void"},        {'w', "wchar_t"},
	               {'b', "bool"},        {'c', "char"},
	               {'a', "signed char"}, {'h', "unsigned char"},
	               {'s', "short"},       {'t', "unsigned short"},
	               {'i', "int"},         {'j', "unsigned int"},
	               {'l', "long"},        {'m', "unsigned long"},
	               {'x', "long long"},   {'y', "unsigned long long"},
	               {'n', "__int128"},    {'o', "unsigned __int128"},
	               {'f', "float"},       {'d', "double"},
	               {'e', "long double"}, {'g', "__float128"},
	               {'z', "..."}};
	size_t i;

	if(p->cur[0] == 'P' || p->cur[0] == 'R' || p->cur[0] == 'O') {
		char kind = p->cur[0];
		char pointee;
		const char *suffix;
		size_t suffix_len;
		size_t before;
		size_t bracket;
		size_t decl_at;
		int is_func;

		p->cur++;
		pointee = p->cur[0];
		/* past qualifiers, to a const array or function under them */
		if(pointee == 'K' || pointee == 'V' || pointee == 'r') {
			const char *q = p->cur;

			while(*q == 'K' || *q == 'V' || *q == 'r') {
				q++;
			}
			if(*q == 'A' || *q == 'F' || *q == 'S' || *q == 'T') {
				pointee = *q;
			}
		}
		before = p->out.len;
		if(parse_type(p) != 0) {
			return -1;
		}

		/* Reference collapsing: & + & = &, & + && = &, && + & = &, && + && = && */
		if((kind == 'R' || kind == 'O') && p->out.len > before) {
			if(p->out.data[p->out.len - 1] == '&') {
				/* Inner type already has a reference — collapse */
				if(kind == 'R') {
					/* R + R or R + O → & (already there) */
					return 0;
				}
				/* O + R → & (already there), O + O → && (already there if &&) */
				if(p->out.len >= before + 2 && p->out.data[p->out.len - 2] == '&') {
					/* Inner is && → O + O = && (keep) */
					return 0;
				}
				/* Inner is & → O + R = & (keep) */
				return 0;
			}
			/* Check for array ref: (&) or (&&) before [] */
			/* If inner has (&)[...] or (&&)[...], it's already a ref */
			{
				size_t i;
				for(i = before; i < p->out.len; i++) {
					if(p->out.data[i] == '(' && i + 2 < p->out.len && p->out.data[i + 1] == '&') {
						/* Already has reference in array context — collapse */
						return 0;
					}
				}
			}
		}

		if(kind == 'P') {
			suffix = "*";
			suffix_len = 1;
		} else if(kind == 'R') {
			suffix = "&";
			suffix_len = 1;
		} else {
			suffix = "&&";
			suffix_len = 2;
		}
		/*
		 * To an array the declarator goes before its bounds, int (*)[4];
		 * to a pointer to one, inside that declarator, int (**)[4].  Only
		 * then: the first [ of the pointee's text was taken for it, so
		 * Foo<int[4]>* came out Foo<int (*)[4]> and Foo[abi:cxx11]*
		 * Foo (*)[abi:cxx11].
		 */
		/* and to a function before its parameters, void (*)(int): it was
		 * void (int)*, as if the pointer were the return type's */
		if(pointee == 'A' || pointee == 'F') {
			char decl[8];
			size_t n = 0u;

			bracket = p->array_dims_off;
			if(pointee == 'A') {
				decl[n++] = ' ';
			}
			decl[n++] = '(';
			memcpy(decl + n, suffix, suffix_len);
			n += suffix_len;
			decl[n++] = ')';
			if(parser_insert_in_type(p, before, bracket, decl, n) != 0)
				return -1;
			p->decl_close = bracket + n - 1u;
		} else if((pointee == 'P' || pointee == 'R' || pointee == 'O') && p->decl_close != SIZE_MAX) {
			bracket = p->decl_close;
			if(parser_insert_in_type(p, before, bracket, suffix, suffix_len) != 0)
				return -1;
			p->decl_close = bracket + suffix_len;
		} else if((pointee == 'S' || pointee == 'T') && find_array_declarator(&p->out, before, &bracket, &decl_at, &is_func) == 0) {
			/* an array or function, or a pointer to one, by substitution:
			 * its text says where the bounds or the declarator are */
			if(decl_at != SIZE_MAX) {
				if(parser_insert_in_type(p, before, decl_at, suffix, suffix_len) != 0)
					return -1;
				p->decl_close = decl_at + suffix_len;
			} else {
				char decl[8];
				size_t n = 0u;

				if(!is_func) {
					decl[n++] = ' ';
				}
				decl[n++] = '(';
				memcpy(decl + n, suffix, suffix_len);
				n += suffix_len;
				decl[n++] = ')';
				if(parser_insert_in_type(p, before, bracket, decl, n) != 0)
					return -1;
				p->decl_close = bracket + n - 1u;
			}
		} else {
			if(demangle_buf_append(&p->out, suffix, suffix_len) != 0)
				return -1;
			p->decl_close = SIZE_MAX;
		}
		return 0;
	}

	if(p->cur[0] == 'K' || p->cur[0] == 'V' || p->cur[0] == 'r') {
		char qual = p->cur[0];
		const char *q;
		const char *text = (qual == 'K') ? " const" : (qual == 'V') ? " volatile" : " restrict";
		size_t before, at, decl_at;
		int is_func;

		p->cur++;
		for(q = p->cur; *q == 'K' || *q == 'V' || *q == 'r'; q++) {
		}
		before = p->out.len;
		if(parse_type(p) != 0) {
			return -1;
		}
		/*
		 * An array's qualifier is its elements', and goes before its
		 * bounds, char const[22] -- as GNU c++filt prints it, and so a
		 * reference to it char const (&)[22], where this had
		 * char[22] const&.
		 */
		if(*q == 'A' || ((*q == 'S' || *q == 'T') && find_array_declarator(&p->out, before, &at, &decl_at, &is_func) == 0 && !is_func &&
		                 decl_at == SIZE_MAX)) {
			if(*q == 'A') {
				at = p->array_dims_off;
			}
			if(parser_insert_in_type(p, before, at, text, strlen(text)) != 0) {
				return -1;
			}
			p->array_dims_off = at + strlen(text);
			return 0;
		}
		return demangle_buf_append(&p->out, text, strlen(text));
	}

	if(p->cur[0] == 'C') {
		p->cur++;
		if(parse_type(p) != 0) {
			return -1;
		}
		return demangle_buf_append(&p->out, " _Complex", 9u);
	}

	if(p->cur[0] == 'G') {
		p->cur++;
		if(parse_type(p) != 0) {
			return -1;
		}
		return demangle_buf_append(&p->out, " _Imaginary", 11u);
	}

	if(p->cur[0] == 'D' && p->cur[1] == 'p') {
		return parse_pack_expansion_type(p);
	}

	if(parse_function_type(p) == 0 || parse_array_type(p) == 0 || parse_pointer_to_member(p) == 0 || parse_template_param(p) == 0 ||
	   parse_decltype_type(p) == 0 || parse_vendor_type(p) == 0) {
		return 0;
	}

	if(p->cur[0] == 'D') {
		const char *name = NULL;

		switch(p->cur[1]) {
		case 'd':
			name = "decimal64";
			break;
		case 'e':
			name = "decimal128";
			break;
		case 'f':
			name = "decimal32";
			break;
		case 'h':
			name = "half";
			break;
		case 'i':
			name = "char32_t";
			break;
		case 's':
			name = "char16_t";
			break;
		case 'a':
			name = "auto";
			break;
		case 'c':
			name = "decltype(auto)";
			break;
		case 'n':
			/* as GNU c++filt prints it (it was std::nullptr_t) */
			name = "decltype(nullptr)";
			break;
		case 'u':
			name = "char8_t";
			break;
		default:
			break;
		}

		if(name != NULL) {
			p->cur += 2;
			return demangle_buf_append(&p->out, name, strlen(name));
		}

		/* DF <bits> _ is _Float<bits>, DF <bits> x _Float<bits>x, and
		 * DF16b std::bfloat16_t; char8_t and these were refused */
		if(p->cur[1] == 'F' && isdigit((unsigned char)p->cur[2])) {
			const char *save = p->cur;
			size_t bits;

			p->cur += 2;
			if(parse_number(p, &bits) == 0) {
				if(p->cur[0] == 'b' && bits == 16u) {
					p->cur++;
					return demangle_buf_append(&p->out, "std::bfloat16_t", 15u);
				}
				if(p->cur[0] == '_' || p->cur[0] == 'x') {
					int ext = (p->cur[0] == 'x');

					p->cur++;
					return demangle_buf_printf(&p->out, ext ? "_Float%zux" : "_Float%zu", bits);
				}
			}
			p->cur = save;
			return -1;
		}
	}

	for(i = 0u; i < sizeof(builtin) / sizeof(builtin[0]); i++) {
		if(p->cur[0] == builtin[i].code) {
			p->cur++;
			return demangle_buf_append(&p->out, builtin[i].name, strlen(builtin[i].name));
		}
	}

	return parse_name(p);
}

static int parse_special_name(dm_itanium_parser_t *p) {
	if(p->cur[0] == 'T' && p->cur[1] == 'V') {
		p->cur += 2;
		if(demangle_buf_append(&p->out, "vtable for ", 11u) != 0) {
			return -1;
		}
		return parse_type(p);
	}

	if(p->cur[0] == 'T' && p->cur[1] == 'T') {
		p->cur += 2;
		if(demangle_buf_append(&p->out, "VTT for ", 8u) != 0) {
			return -1;
		}
		return parse_type(p);
	}

	if(p->cur[0] == 'T' && p->cur[1] == 'I') {
		p->cur += 2;
		if(demangle_buf_append(&p->out, "typeinfo for ", 13u) != 0) {
			return -1;
		}
		return parse_type(p);
	}

	if(p->cur[0] == 'T' && p->cur[1] == 'S') {
		p->cur += 2;
		if(demangle_buf_append(&p->out, "typeinfo name for ", 18u) != 0) {
			return -1;
		}
		return parse_type(p);
	}

	if(p->cur[0] == 'G' && p->cur[1] == 'V') {
		p->cur += 2;
		if(demangle_buf_append(&p->out, "guard variable for ", 19u) != 0) {
			return -1;
		}
		return parse_name(p);
	}

	if(p->cur[0] == 'T' && (p->cur[1] == 'c' || p->cur[1] == 'h' || p->cur[1] == 'v')) {
		char kind = p->cur[1];
		long long off1 = 0;
		long long off2 = 0;
		int have1 = 0;
		int have2 = 0;

		p->cur += 2;

		if(parse_signed_number_token(p, &off1) == 0) {
			have1 = 1;
			if(kind != 'h' && parse_signed_number_token(p, &off2) == 0) {
				have2 = 1;
			}
		}

		if(kind == 'c') {
			if(demangle_buf_append(&p->out, "covariant return thunk to ", 26u) != 0) {
				return -1;
			}
		} else if(kind == 'h') {
			if(demangle_buf_append(&p->out, "non-virtual thunk to ", 21u) != 0) {
				return -1;
			}
		} else {
			if(demangle_buf_append(&p->out, "virtual thunk to ", 17u) != 0) {
				return -1;
			}
		}

		if(parser_enter(p) != 0) {
			return -1;
		}
		if(parse_encoding(p) != 0) {
			parser_leave(p);
			return -1;
		}
		parser_leave(p);

		if(have1) {
			if(have2) {
				return demangle_buf_printf(&p->out, " [offsets %lld, %lld]", off1, off2);
			}
			return demangle_buf_printf(&p->out, " [offset %lld]", off1);
		}

		return 0;
	}

	/*
	 * GTt and GTn <encoding>, a transaction clone and its other; GA
	 * <encoding>, a hidden alias.  This took any T it had not matched for
	 * a transaction clone, of whatever followed: _ZGTt... came out
	 * "transaction clone for t..." and refused, and TH, TW too.
	 */
	if((p->cur[0] == 'G' && p->cur[1] == 'T' && (p->cur[2] == 't' || p->cur[2] == 'n')) || (p->cur[0] == 'G' && p->cur[1] == 'A')) {
		const char *what = p->cur[1] == 'A' ? "hidden alias for " : p->cur[2] == 't' ? "transaction clone for " : "non-transaction clone for ";
		int rc;

		p->cur += (p->cur[1] == 'A') ? 2 : 3;
		if(demangle_buf_append(&p->out, what, strlen(what)) != 0 || parser_enter(p) != 0) {
			return -1;
		}
		rc = parse_encoding(p);
		parser_leave(p);
		return rc;
	}

	/* TH and TW <name>: a thread_local's init and wrapper functions */
	if(p->cur[0] == 'T' && (p->cur[1] == 'H' || p->cur[1] == 'W')) {
		const char *what = p->cur[1] == 'H' ? "TLS init function for " : "TLS wrapper function for ";

		p->cur += 2;
		if(demangle_buf_append(&p->out, what, strlen(what)) != 0) {
			return -1;
		}
		return parse_name(p);
	}

	/* TC <derived type> <offset> _ <base type>: "A-in-B", the base first */
	if(p->cur[0] == 'T' && p->cur[1] == 'C') {
		size_t derived_off, derived_len, off;
		char *derived;
		int rc;

		p->cur += 2;
		if(demangle_buf_append(&p->out, "construction vtable for ", 24u) != 0) {
			return -1;
		}
		derived_off = p->out.len;
		if(parse_type(p) != 0 || parse_number(p, &off) != 0 || p->cur[0] != '_') {
			return -1;
		}
		p->cur++;
		derived_len = p->out.len - derived_off;
		derived = (char *)malloc(derived_len + 1u);
		if(derived == NULL) {
			return -1;
		}
		memcpy(derived, p->out.data + derived_off, derived_len);
		derived[derived_len] = '\0';
		p->out.len = derived_off;
		p->out.data[p->out.len] = '\0';
		rc = (parse_type(p) != 0 || demangle_buf_append(&p->out, "-in-", 4u) != 0 || demangle_buf_append(&p->out, derived, derived_len) != 0) ? -1 : 0;
		free(derived);
		return rc;
	}

	/* GI <module name>: W <source-name> for each component, std.compat */
	if(p->cur[0] == 'G' && p->cur[1] == 'I' && p->cur[2] == 'W') {
		int first = 1;

		p->cur += 2;
		if(demangle_buf_append(&p->out, "initializer for module ", 23u) != 0) {
			return -1;
		}
		while(p->cur[0] == 'W') {
			size_t len;

			p->cur++;
			if(parse_number(p, &len) != 0 || len == 0u || !has_n_bytes(p->cur, len) || (!first && demangle_buf_appendc(&p->out, '.') != 0) ||
			   demangle_buf_append(&p->out, p->cur, len) != 0) {
				return -1;
			}
			p->cur += len;
			first = 0;
		}
		return 0;
	}

	return -1;
}

static int parse_encoding(dm_itanium_parser_t *p) {
	dm_parse_mark_t m;
	size_t outer_off = p->encoding_off;
	int rc = -1;
	/* this encoding, not one nested in its name, is a local name's */
	int is_local = p->next_encoding_is_local;

	p->next_encoding_is_local = 0;
	p->encoding_off = p->out.len;
	mark_save(p, &m);
	if(parse_special_name(p) == 0) {
		rc = 0;
		goto out;
	}
	mark_restore(p, &m);

	if(parse_name(p) != 0) {
		goto out;
	}

	if(!at_encoding_end(p)) {
		/* Save function's template args for T_ references in the signature */
		p->func_tmpl_count = p->last_template_count;
		if(p->func_tmpl_count > 0) {
			memcpy(p->func_tmpl_off, p->last_template_off, p->func_tmpl_count * sizeof(size_t));
			memcpy(p->func_tmpl_len, p->last_template_len, p->func_tmpl_count * sizeof(size_t));
		}
		targ_map_copy(&p->func_map, &p->last_map);
		p->drop_return_type = is_local;
		if(parse_bare_function_type(p) != 0) {
			goto out;
		}
		p->func_tmpl_count = 0;
		p->func_map.count = 0;
	}
	rc = 0;

out:
	p->encoding_off = outer_off;
	return rc;
}

char *demangle_itanium(const char *mangled, int options) {
	dm_itanium_parser_t parser;
	char *ret;

	if(mangled == NULL || mangled[0] == '\0') {
		return NULL;
	}

	if((options & DEMANGLE_TYPES) == 0 && !(mangled[0] == '_' && mangled[1] == 'Z')) {
		return NULL;
	}

	if(parser_begin(&parser) != 0) {
		return NULL;
	}

	parser.input = mangled;
	parser.cur = mangled;
	parser.options = options;
	if((options & DEMANGLE_NO_RECURSE_LIMIT) != 0) {
		parser.recursion_limit = INT_MAX;
	}

	if(mangled[0] == '_' && mangled[1] == 'Z') {
		parser.cur += 2;
	}

	if(parser_enter(&parser) != 0) {
		parser_destroy(&parser);
		return NULL;
	}

	if(mangled[0] == '_' && mangled[1] == 'Z') {
		if(parse_encoding(&parser) != 0 || parser.cur[0] != '\0') {
			parser_leave(&parser);
			parser_destroy(&parser);
			return NULL;
		}
	} else {
		if((options & DEMANGLE_TYPES) == 0 || parse_type(&parser) != 0 || parser.cur[0] != '\0') {
			parser_leave(&parser);
			parser_destroy(&parser);
			return NULL;
		}
	}

	parser_leave(&parser);

	ret = demangle_buf_take(&parser.out);
	parser_destroy(&parser);
	return ret;
}
