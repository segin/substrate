#!/bin/sh
# -g describes the types a function's body casts to or makes compound
# literals of, though no object has them, as gcc does: wherever the
# function is described, written or inlined.  cc described only the types
# of objects, so Linux's BTF_TYPE_EMIT(), a cast of a null pointer to the
# type register_bpf_struct_ops() declares in a statement expression, left
# no struct bpf_struct_ops_* for pahole, and at boot BPF struct_ops found
# none ("Cannot find struct bpf_struct_ops_tcp_congestion_ops in
# vmlinux").  gcc is the judge: the structures and typedefs each object
# describes, -m64 and -m32, -O0 and -O2.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1 || ! command -v readelf >/dev/null 2>&1; then
	echo "run_debug_named_types: skip (no gcc or readelf)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

cat >"$CC_TMP/t.c" <<'EOF2'
struct reached { int a; };
struct fs_cast { int a; };
struct fs_unused { int a; };
int reg(void *p);
void *p;

int register_ops(void *ops)
{
	return ({
		struct ops_value { int common; struct reached data; };
		(void)(struct ops_value *)0;
		reg(ops);
	});
}

int named(void)
{
	struct b_cast { int a; };
	struct b_sizeof { int a; };
	struct b_complit { int a; };
	struct b_unused { int a; };
	struct b_tdef_cast { int a; };
	typedef struct b_tdef_cast b_tdef_cast_t;
	struct b_tdef_unused { int a; };
	typedef struct b_tdef_unused b_tdef_unused_t;
	struct b_in_sizeof { int a; };
	struct b_dead_branch { int a; };
	int n;

	(void)(struct fs_cast *)0;
	(void)(b_tdef_cast_t *)0;
	(void)(struct b_cast *)p;
	n = sizeof(struct b_sizeof) + sizeof((struct b_in_sizeof *)0);
	n += (struct b_complit){1}.a;
	if (0)
		(void)(struct b_dead_branch *)0;
	return n;
}

static inline int unused_inline(void)
{
	struct in_unused_inline { int a; };
	(void)(struct in_unused_inline *)0;
	return 0;
}

static inline __attribute__((always_inline)) int inl(void)
{
	struct in_inlined { int a; };
	(void)(struct in_inlined *)0;
	return 1;
}

int calls_inl(void)
{
	return inl();
}
EOF2

# OBJ: the names of the structures and typedefs it describes
names() {
	readelf --debug-dump=info "$1" | grep -A1 'DW_TAG_\(structure_type\|typedef\)' |
		grep DW_AT_name | sed 's/.*: //' | sort -u
}

fail=0
for m in 64 32; do
	for o in O0 O2; do
		gcc -m$m -std=gnu17 -g -$o -c -o "$CC_TMP/g.o" "$CC_TMP/t.c" 2>/dev/null || continue
		names "$CC_TMP/g.o" >"$CC_TMP/want"
		if ! "$CC_BIN" -m$m -std=gnu17 -g -$o -c -o "$CC_TMP/c.o" "$CC_TMP/t.c" 2>"$CC_TMP/c.err"; then
			echo "run_debug_named_types: -m$m -$o refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		names "$CC_TMP/c.o" >"$CC_TMP/got"
		if ! cmp -s "$CC_TMP/got" "$CC_TMP/want"; then
			echo "run_debug_named_types: -m$m -$o: the types described differ from gcc's (< gcc, > cc):"
			diff "$CC_TMP/want" "$CC_TMP/got" | grep '^[<>]'
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_debug_named_types: ok"
