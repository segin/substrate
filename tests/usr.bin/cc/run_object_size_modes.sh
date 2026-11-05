#!/bin/sh
# __builtin_object_size's four modes, as gcc has them: 0 and 2 the bytes
# left in the whole object -- a declared one; not known through a pointer
# (-1 and 0) -- and 1 and 3 in the closest subobject, the member or array,
# but an array last in the object a pointer reaches counting as the whole,
# since it may be a flexible array.
#
# Every mode was the subobject's: Linux's fortified memset() of the rest
# of a struct sock from &sk->sk_node.pprev (an 8-byte member, through a
# pointer) compared its length with 8 rather than nothing, and panicked at
# boot ("__fortify_panic" in sk_prot_alloc).  Results are gcc's.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_object_size_modes: skip (no gcc)"
	exit 0
fi

cat >"$CC_TMP/os.c" <<'EOF2'
#include <stdio.h>

struct in { int x; char arr[6]; };
struct s { long a; struct in in; char mid[4]; struct in last; char tail[20]; };
struct pad { int x; char a[2]; char b; };
struct pad2 { int x; char a[2]; };
union u { int i; char c[8]; };
struct node { void *next; void **pprev; };
struct sock { struct node sk_node; int a; char tail[20]; };
struct holder { struct pad2 p; };

#define P(e) printf("%-22s %3ld %3ld %3ld %3ld\n", #e, (long)__builtin_object_size(e, 0), \
		    (long)__builtin_object_size(e, 1), (long)__builtin_object_size(e, 2), \
		    (long)__builtin_object_size(e, 3))

struct s g, ga[3];
struct pad gpad;
struct pad2 gpad2;
union u gu;
struct sock gsk;
struct holder gh;

__attribute__((noinline)) void declared(void)
{
	struct s l;

	P(&g); P(&g.a); P(&g.in.arr[2]); P(g.mid); P(g.tail); P(&g.tail[5]); P(&g.last.arr[1]);
	P(&ga[1].mid[1]); P((char *)&g + 3); P(&g.in); P(&l.mid[1]); P(l.tail);
	(void)l;
}

__attribute__((noinline)) void pointers(struct s *p, char *q, struct in *ip, struct pad *a, struct pad2 *b,
					union u *c, const struct pad2 *d, struct sock *sk, struct holder *h)
{
	P(p); P(&p->a); P(p->mid); P(p->tail); P(&p->tail[3]); P(p->last.arr); P(&p->in.arr[1]);
	P(&p[1].mid[0]); P(&p->in); P(q); P(q + 4); P(ip->arr);
	P(a->a); P(&a->b); P(b->a); P(c->c); P(d->a); P(&sk->sk_node.pprev); P(sk->tail); P(h->p.a); P(&h->p);
	P((char *)sk + 8); P((char *)&sk->a); P(&sk->tail[2] - 1);
}

int main(void)
{
	declared();
	pointers(&g, (char *)&g, &g.in, &gpad, &gpad2, &gu, &gpad2, &gsk, &gh);
	return 0;
}
EOF2
for m in -m64 -m32; do
	if ! printf 'int main(void) { return 0; }\n' | gcc $m -x c - -o "$CC_TMP/probe" 2>/dev/null; then
		echo "run_object_size_modes: $m: skip (no gcc $m)"
		continue
	fi
	gcc $m -O2 -o "$CC_TMP/g" "$CC_TMP/os.c"
	"$CC_TMP/g" >"$CC_TMP/g.out"
	"$CC_BIN" $m -O2 -o "$CC_TMP/c" "$CC_TMP/os.c"
	"$CC_TMP/c" >"$CC_TMP/c.out"
	if ! cmp -s "$CC_TMP/g.out" "$CC_TMP/c.out"; then
		echo "run_object_size_modes: $m: differs from gcc (expression, modes 0 1 2 3; gcc first):"
		diff "$CC_TMP/g.out" "$CC_TMP/c.out" || true
		exit 1
	fi
done
echo "run_object_size_modes: ok"
