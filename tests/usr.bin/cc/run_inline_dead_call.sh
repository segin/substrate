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

for m in 32 64; do
	"$CC_BIN" -m$m -O2 -c -o "$CC_TMP/dead_call.o" "$CC_TMP/dead_call.c"
	if nm -u "$CC_TMP/dead_call.o" | grep -q 'undefined_'; then
		echo "run_inline_dead_call: -m$m -O2 keeps a guarded call:"
		nm -u "$CC_TMP/dead_call.o" | grep 'undefined_'
		exit 1
	fi
done
echo "run_inline_dead_call: ok"
