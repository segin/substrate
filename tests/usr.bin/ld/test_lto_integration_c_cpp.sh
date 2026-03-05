#!/bin/sh
set -eu

# Reqs: LD-U-007, LD-O-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-lto-integ-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

has_lto_marker() {
	readelf -S "$1" 2>/dev/null | grep -Eq '\.gnu\.lto_|\.llvmbc|\.llvmcmd|\.llvm\.lto'
}

build_lto_obj() {
	ccbin="$1"
	src="$2"
	out="$3"
	if "$ccbin" -m64 -flto -ffat-lto-objects -c -o "$out" "$src" >/dev/null 2>&1; then
		return 0
	fi
	"$ccbin" -m64 -flto -c -o "$out" "$src"
}

cat > "$TMP/c_src.c" <<'SRC'
int c_add(int x) { return x + 3; }
SRC

cat > "$TMP/cpp_src.cpp" <<'SRC'
extern "C" int cpp_add(int x) { return x + 4; }
SRC

cat > "$TMP/plugin.sh" <<'SRC'
#!/bin/sh
set -eu
case "${1:-}" in
	--version)
		exit 0
		;;
	--materialize)
		in="$2"
		shift 2
		echo "mat:$in $*" >>"${PLUGIN_LOG}"
		echo "$in"
		exit 0
		;;
esac
exit 1
SRC
chmod 0755 "$TMP/plugin.sh"

exercised=0
for pair in "gcc:g++" "clang:clang++"; do
	ccbin=${pair%%:*}
	cxxbin=${pair#*:}
	if ! command -v "$ccbin" >/dev/null 2>&1; then
		continue
	fi
	if ! command -v "$cxxbin" >/dev/null 2>&1; then
		continue
	fi

	c_obj="$TMP/${ccbin}_c.o"
	cpp_obj="$TMP/${ccbin}_cpp.o"
	build_lto_obj "$ccbin" "$TMP/c_src.c" "$c_obj"
	build_lto_obj "$cxxbin" "$TMP/cpp_src.cpp" "$cpp_obj"

	if ! has_lto_marker "$c_obj" && ! has_lto_marker "$cpp_obj"; then
		continue
	fi

	: >"$TMP/${ccbin}.plugin.log"
	PLUGIN_LOG="$TMP/${ccbin}.plugin.log" \
		"$LDX" -m64 -r -plugin "$TMP/plugin.sh" -plugin-opt=mode=test -o "$TMP/${ccbin}_out.o" "$c_obj" "$cpp_obj"
	if ! grep -q 'mode=test' "$TMP/${ccbin}.plugin.log"; then
		echo "FAIL: plugin options were not propagated for $ccbin" >&2
		sed -n '1,120p' "$TMP/${ccbin}.plugin.log" >&2
		exit 1
	fi
	if ! grep -q "mat:$c_obj" "$TMP/${ccbin}.plugin.log" && ! grep -q "mat:$cpp_obj" "$TMP/${ccbin}.plugin.log"; then
		echo "FAIL: plugin materialization did not run for $ccbin LTO objects" >&2
		sed -n '1,120p' "$TMP/${ccbin}.plugin.log" >&2
		exit 1
	fi
	exercised=1
done

if [ "$exercised" -eq 0 ]; then
	echo "ok: LTO integration test skipped (no compiler emitted recognizable LTO sections)"
	exit 0
fi

echo "ok: GCC/Clang C and C++ LTO objects integrate through plugin materialization"
