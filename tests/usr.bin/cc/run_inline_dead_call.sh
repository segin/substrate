#!/bin/sh
# At -O2 a call that a static inline helper, constant in the build,
# guards goes with its dead arm, as with gcc, which inlines the helper and
# folds the branch.  Linux relies on it, leaving the function called
# undefined: `if (efi_enabled(EFI_BOOT)) efi_init();` with efi_enabled()
# returning false, `if (pmd_is_huge(*pmd)) zap_huge_pmd(...)` through a
# chain of helpers, and dozens more were undefined at the vmlinux link.
# codegen/inline_static.c checks the values inlined bodies give.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if [ -z "${CC_TMP:-}" ]; then
	CC_TMP=$(mktemp -d "${TMPDIR:-/tmp}/cc_suite.XXXXXX")
	trap 'rm -rf "$CC_TMP"' EXIT
fi

cat > "$CC_TMP/dead_call.c" <<'EOF'
typedef struct { unsigned long pmd; } pmd_t;
struct ksig { int flags; };
void undefined_efi_init(void);
void undefined_zap_huge(void);
void undefined_x32_frame(void);
void undefined_slow_copy(unsigned long);

/* constant helpers, as a configuration leaves them */
static inline _Bool efi_enabled(int feature) { return 0; }
static inline int pmd_present(pmd_t p) { return p.pmd & 1; }
static inline int pmd_trans_huge(pmd_t p) { return 0; }
static inline int pmd_is_huge(pmd_t p) {
	if (pmd_present(p))
		return pmd_trans_huge(p);
	return 0;
}
static inline int is_ia32_frame(struct ksig *k) { return 1 || (k->flags & 4); }
static inline int movsl_is_ok(unsigned long a, unsigned long b, unsigned long n) { return 1; }

void setup(void) { if (efi_enabled(3)) undefined_efi_init(); }
void zap(pmd_t *pmd) { if (pmd_is_huge(*pmd)) undefined_zap_huge(); }
int frame(struct ksig *k) { if (is_ia32_frame(k)) return 1; undefined_x32_frame(); return 0; }
unsigned long copy(unsigned long n) { if (movsl_is_ok(1, 2, n)) return n; undefined_slow_copy(n); return 0; }
EOF

# -Os is how Linux is built: gcc's named levels optimize, -Os as -O2
for m in 32 64; do
	for o in O2 Os Oz Og; do
		"$CC_BIN" -m$m -$o -c -o "$CC_TMP/dead_call.o" "$CC_TMP/dead_call.c"
		if nm -u "$CC_TMP/dead_call.o" | grep -q 'undefined_'; then
			echo "run_inline_dead_call: -m$m -$o keeps a guarded call:"
			nm -u "$CC_TMP/dead_call.o" | grep 'undefined_'
			exit 1
		fi
	done
done
# A call guarded by a local stored once with a constant goes: Linux's
# `bool set_uncached = IS_ENABLED(...) && ...; if (set_uncached) ...` in
# dma_direct_alloc(), and a switch on what an inlined function returns
# through a local.
cat > "$CC_TMP/const_local.c" <<'EOF'
void *undefined_set_uncached(void *p);
void undefined_managed(int);
int coherent(int d);
enum { NORMAL, MANAGED };
static inline int startup_kind(int force) { int r = NORMAL; return r; }
void *alloc(void *p, int dev) {
	_Bool set_uncached = 0 && !coherent(dev);
	if (set_uncached)
		p = undefined_set_uncached(p);
	return p;
}
int startup(int force) {
	switch (startup_kind(force)) {
	case NORMAL: return 1;
	case MANAGED: undefined_managed(force); return 2;
	}
	return 0;
}
EOF
for m in 32 64; do
	"$CC_BIN" -m$m -Os -c -o "$CC_TMP/const_local.o" "$CC_TMP/const_local.c"
	if nm -u "$CC_TMP/const_local.o" | grep -q 'undefined_'; then
		echo "run_inline_dead_call: -m$m -Os keeps a call a constant local guards:"
		nm -u "$CC_TMP/const_local.o" | grep 'undefined_'
		exit 1
	fi
done

# A call guarded by x & 0 (Linux's IS_DAX(inode), S_DAX 0 without DAX)
# goes, and one guarded by `a || b` with b constant true, whose result
# is set on two paths that agree (folio_unqueue_deferred_split()'s
# `folio_order(folio) <= 1 || !folio_test_large_rmappable(folio)`, the
# test false without THP).
cat > "$CC_TMP/join.c" <<'EOF'
struct inode { unsigned i_flags; };
struct folio { unsigned long flags; };
void undefined_dax(void);
_Bool undefined_unqueue(struct folio *);
static inline _Bool test_large(const struct folio *f) { return 0; }
static inline unsigned order(const struct folio *f) { return f->flags & 0xff; }
static inline _Bool unqueue(struct folio *f) {
	if (order(f) <= 1 || !test_large(f))
		return 0;
	return undefined_unqueue(f);
}
void dax(struct inode *inode) { if (inode->i_flags & 0) undefined_dax(); }
int put(struct folio *f) { return unqueue(f); }
EOF
for m in 32 64; do
	"$CC_BIN" -m$m -Os -c -o "$CC_TMP/join.o" "$CC_TMP/join.c"
	if nm -u "$CC_TMP/join.o" | grep -q 'undefined_'; then
		echo "run_inline_dead_call: -m$m -Os keeps a call x & 0 or a join guards:"
		nm -u "$CC_TMP/join.o" | grep 'undefined_'
		exit 1
	fi
done

# A call with no effect but its unused result goes too, inlined or not:
# Linux's noinline apply_retpolines(__retpoline_sites, ...) is empty
# without retpolines, and the linker script then defines neither symbol.
# A call that reads memory (a device register), may loop forever, or
# whose result is used stays, and under -fPIC so does a call of an
# external function, which another object may interpose.
cat > "$CC_TMP/no_effect.c" <<'EOF'
typedef int s32;
extern s32 undefined_sites[], undefined_sites_end[];
extern volatile int mmio;
void __attribute__((noinline)) apply_retpolines(s32 *start, s32 *end) { }
int __attribute__((noinline)) reads_mmio(s32 *start) { return mmio; }
void __attribute__((noinline)) spins(int x) { while (x) { } }
static int __attribute__((noinline)) used_result(int x) { return x + 1; }
int setup(void) {
	apply_retpolines(undefined_sites, undefined_sites_end);
	reads_mmio(0);
	spins(0);
	return used_result(4);
}
EOF
for m in 32 64; do
	"$CC_BIN" -m$m -Os -S -o "$CC_TMP/no_effect.s" "$CC_TMP/no_effect.c"
	calls=$(grep -Eo 'call[lq]?[[:space:]]+[a-z_]+' "$CC_TMP/no_effect.s" | awk '{print $2}' | sort | tr '\n' ' ')
	if [ "$calls" != "reads_mmio spins used_result " ]; then
		echo "run_inline_dead_call: -m$m -Os calls: $calls"
		exit 1
	fi
	"$CC_BIN" -m$m -Os -fPIC -S -o "$CC_TMP/no_effect.s" "$CC_TMP/no_effect.c"
	if ! grep -Eq 'call[lq]?[[:space:]]+apply_retpolines' "$CC_TMP/no_effect.s"; then
		echo "run_inline_dead_call: -m$m -fPIC dropped a call of an interposable function"
		exit 1
	fi
done
echo "run_inline_dead_call: ok"
