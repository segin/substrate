#!/bin/sh
set -eu

# Reqs: LD-O-004, LD-U-004

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
LDX="$ROOT/usr.bin/ld/ld"
TMP=${TMPDIR:-/tmp}/ldx86-plugin-handshake-$$
mkdir -p "$TMP"
trap 'rm -rf "$TMP"' EXIT INT TERM

cat > "$TMP/base.c" <<'SRC'
int base(void) { return 1; }
SRC
gcc -m64 -c -o "$TMP/base.o" "$TMP/base.c"

cat > "$TMP/plugin_bad.sh" <<'SRC'
#!/bin/sh
exit 0
SRC
chmod 0644 "$TMP/plugin_bad.sh"
if "$LDX" -m64 -r -plugin "$TMP/plugin_bad.sh" -o "$TMP/out_bad.o" "$TMP/base.o" >"$TMP/bad.log" 2>&1; then
	echo "FAIL: ld accepted non-executable plugin" >&2
	exit 1
fi
if ! grep -q "plugin not executable" "$TMP/bad.log"; then
	echo "FAIL: missing non-executable plugin diagnostic" >&2
	sed -n '1,80p' "$TMP/bad.log" >&2
	exit 1
fi

cat > "$TMP/plugin_ok.sh" <<'SRC'
#!/bin/sh
set -eu
case "${1:-}" in
	--version)
		echo "ok" >>"${PLUGIN_LOG}"
		exit 0
		;;
	--materialize)
		echo "mat:$2" >>"${PLUGIN_LOG}"
		echo "$2"
		exit 0
		;;
esac
exit 1
SRC
chmod 0755 "$TMP/plugin_ok.sh"
: >"$TMP/plugin.log"
PLUGIN_LOG="$TMP/plugin.log" \
	"$LDX" -m64 -r -plugin "$TMP/plugin_ok.sh" -o "$TMP/out_ok.o" "$TMP/base.o"

if ! grep -q '^ok$' "$TMP/plugin.log"; then
	echo "FAIL: plugin handshake (--version) was not executed" >&2
	sed -n '1,80p' "$TMP/plugin.log" >&2
	exit 1
fi

echo "ok: plugin handshake validates plugin executability and runs --version"
