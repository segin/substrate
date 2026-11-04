#!/bin/sh
# What a static function only direct calls reach passes on of its own
# parameter to another such function is known there too, as gcc's
# interprocedural range propagation carries a range through the calls
# that pass it along.  Linux's hid-steam has steam_send_report_id(),
# too large to inline, with a max(size + 1, len) whose BUILD_BUG_ON asks
# that size + 1 is not negative; its callers steam_send_report() and
# steam_exchange_report_id() pass their own size on, and their callers
# constants: cc knew the range only one call deep and left a call of an
# undefined __compiletime_assert_N.  What must fold leaves no reference
# gcc's code does not; a parameter some caller passes anything, or a
# negative value, through however many calls, and a function that calls
# itself with a negative value, keep their checks as gcc keeps them.
# -O2, -m64 and -m32, both code paths, every function through MIR; and
# the program, run, prints what gcc's prints.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_fold_param_ranges_chain: skip (no gcc)"
	exit 0
fi
: "${AS:=as}"
: "${LD:=ld}"
export AS LD

# what must fold calls a *_bad function, what must not a *_kept one
cat >"$CC_TMP/u.c" <<'EOF2'
#define statically_true(x) (__builtin_constant_p(x) && (x))
#define is_nonneg(x) statically_true((long long)(x) >= 0)
#define sign_use(x) (((typeof(x))(-1)) < (typeof(x))1 ? (2 + is_nonneg(x)) : (1 + 2 * (sizeof(x) < 4)))
#define max(a, b, bad) ({ __auto_type ua = (a); __auto_type ub = (b); \
	do { if (!(sign_use(ua) & sign_use(ub))) bad(); } while (0); ua > ub ? ua : ub; })
extern void max_bad(void), deep_kept(void), self_kept(void);
extern int raw(unsigned char *buf, unsigned int n);
extern void sink(long);
extern unsigned int report_len;

static int send_id(unsigned char *cmd, int size, unsigned char id)
{
	unsigned int retries = 50;
	unsigned int len = report_len;
	int ret;
	int i;

	if (len < 64)
		return -22;
	for (i = 0; i < size; i++)
		sink(cmd[i] ^ id);
	if (!(id & 1))
		len += 1;
	do {
		ret = raw(cmd, max(size + 1, len, max_bad));
		if (ret != -32)
			break;
		sink(ret * 3 + id);
		sink((long)len - size);
	} while (--retries);
	return ret;
}
static int send(unsigned char *cmd, int size, int quirks)
{
	unsigned char id = quirks & 4 ? 3 : 0;

	sink(quirks * 7 + size);
	sink(quirks ^ size);
	return send_id(cmd, size, id);
}
static int exchange_id(unsigned char *cmd, int csize, unsigned char *reply, int rsize, unsigned char id)
{
	unsigned int retries = 5;
	int ret;

	do {
		ret = send_id(cmd, csize, id);
		if (ret < 0)
			return ret;
		ret = raw(reply, (unsigned int)rsize);
		sink(ret + rsize);
		sink(ret ^ csize);
	} while (ret < 0 && --retries);
	return ret;
}
static int exchange(unsigned char *cmd, int csize, unsigned char *reply, int rsize, int quirks)
{
	unsigned char id = quirks & 4 ? 3 : 0;

	sink(quirks + csize);
	sink(quirks * rsize);
	return exchange_id(cmd, csize, reply, rsize, id);
}
int write_settings(int quirks, unsigned char *cmd)
{
	return send(cmd, 2 + cmd[1], quirks) + send(cmd, 1, quirks);
}
int get_serial(int quirks, unsigned char *cmd, unsigned char *reply)
{
	return exchange(cmd, 3, reply, 64, quirks) + exchange(cmd, 2, reply, 21, quirks + 1);
}
int get_info(unsigned char *cmd, unsigned char *reply, unsigned char id)
{
	return exchange_id(cmd, 10, reply, 32, id) + send(cmd, 9, 1);
}

/* passed on twice from where anything is passed: the check stays */
static int deep(int x, int k)
{
	int i;
	int s = 0;

	if (!is_nonneg(x))
		deep_kept();
	for (i = 0; i < 4; i++) {
		sink(x * i + k);
		sink(x ^ i);
		s += x - i;
	}
	return s;
}
static int mid(int x, int k)
{
	sink(x + k);
	sink(x * k);
	return deep(x, k) + deep(x, k + 1);
}
int outer1(int x) { return mid(x, 1) + mid(3, 2); }
int outer2(int k) { return deep(4, k) + mid(5, k); }

/* calls itself with what may be negative: the check stays */
static int self(int x, int k)
{
	int i;
	int s = 0;

	if (!is_nonneg(x))
		self_kept();
	for (i = 0; i < 3; i++) {
		sink(x * i + k);
		s += x ^ i;
	}
	if (k > 0)
		s += self(x - 9, k - 1);
	return s;
}
int self1(int k) { return self(5, k); }
int self2(int k) { return self(7, k + 1); }
EOF2

# the same code, run (what __builtin_constant_p answers is each compiler's own, and not printed)
cat >"$CC_TMP/defs.c" <<'EOF2'
#include <stdio.h>
static long sum;
unsigned int report_len = 65;
void max_bad(void) { }
void deep_kept(void) { }
void self_kept(void) { }
void sink(long v) { sum = sum * 31 + v; }
int raw(unsigned char *buf, unsigned int n) { sum = sum * 7 + n + buf[0]; return n & 1 ? -32 : (int)n; }
EOF2
cat >"$CC_TMP/main.c" <<'EOF2'
int main(void)
{
	unsigned char cmd[64] = { 0x87, 6, 1, 2, 3, 4, 5, 6 };
	unsigned char reply[64] = { 9 };
	int a = write_settings(4, cmd);
	int b = write_settings(1, cmd);
	int c = get_serial(0, cmd, reply);
	int d = get_info(cmd, reply, 3);
	int e = outer1(-3) + outer2(6);
	int f = self1(2) + self2(1);

	printf("%d %d %d %d %d %d sum %ld\n", a, b, c, d, e, f, sum);
	return 0;
}
EOF2
cat "$CC_TMP/defs.c" "$CC_TMP/u.c" "$CC_TMP/main.c" >"$CC_TMP/p.c"

undefined() {
	nm -u "$1" | awk '{ print $NF }' | grep -E '_(bad|kept)$' | sort || true
}

fail=0
for m in 64 32; do
	gcc -m$m -O2 -fno-pie -c -o "$CC_TMP/g.o" "$CC_TMP/u.c" 2>/dev/null || continue
	want=$(undefined "$CC_TMP/g.o")
	gcc -m$m -O2 -fno-pie -no-pie -o "$CC_TMP/g" "$CC_TMP/p.c"
	want_run=$("$CC_TMP/g")
	for path in mir legacy; do
		run="env CC_MIR_STATS=1"
		if [ $path = legacy ]; then run="env CC_NO_MIR=1"; fi
		what="-m$m -O2 $path"
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/c.o" "$CC_TMP/u.c" 2>"$CC_TMP/c.err"; then
			echo "run_fold_param_ranges_chain: $what refused: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_fold_param_ranges_chain: $what: a function fell back from MIR"
			fail=1
		fi
		got=$(undefined "$CC_TMP/c.o")
		if [ "$got" != "$want" ]; then
			echo "run_fold_param_ranges_chain: $what leaves [$(echo $got)] undefined, gcc [$(echo $want)]"
			fail=1
		fi
		if ! $run "$CC_BIN" -m$m -O2 -c -o "$CC_TMP/p.o" "$CC_TMP/p.c" 2>"$CC_TMP/c.err"; then
			echo "run_fold_param_ranges_chain: $what refused the program: $(grep error "$CC_TMP/c.err" | head -1)"
			exit 1
		fi
		if grep 'not taken' "$CC_TMP/c.err"; then
			echo "run_fold_param_ranges_chain: $what: a function of the program fell back from MIR"
			fail=1
		fi
		gcc -m$m -no-pie -o "$CC_TMP/c" "$CC_TMP/p.o"
		got_run=$("$CC_TMP/c")
		if [ "$got_run" != "$want_run" ]; then
			echo "run_fold_param_ranges_chain: $what prints"
			echo "$got_run"
			echo "gcc's:"
			echo "$want_run"
			fail=1
		fi
	done
done
if [ $fail -ne 0 ]; then
	exit 1
fi
echo "run_fold_param_ranges_chain: ok"
