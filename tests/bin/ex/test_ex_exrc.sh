#!/bin/bash
# Startup files are read as POSIX ex's Initialization orders them.  ./.exrc
# was run whenever EXINIT was unset, with no exrc option to ask for it: an
# editor started in a cloned repository or an unpacked tarball ran its
# .exrc, `!` commands and all.  And -s read EXINIT and both .exrc files.
#
# Each .exrc here sets an option whose effect `1p` shows: number (a
# numbered line) or list (a `$` at the end).
#
# Usage: test_ex_exrc.sh path/to/ex
set -u

EX_BIN="${1:?usage: $0 path/to/ex}"
T=$(mktemp -d "${TMPDIR:-/tmp}/ex_exrc.XXXXXX")
trap 'rm -rf "$T"' EXIT
fails=0

fail() {
	echo "FAIL: $1"
	fails=$((fails + 1))
}

mkdir "$T/home" "$T/proj"
chmod 755 "$T/home" "$T/proj"
echo one >"$T/proj/f"

# run ex in proj with HOME=home and the given EXINIT ("-" for unset)
# and options; print what `1p` printed
run() {
	local exinit="$1"
	shift
	(
		cd "$T/proj" || exit 1
		if [ "$exinit" = - ]; then
			unset EXINIT
		else
			export EXINIT="$exinit"
		fi
		printf '1p\nq!\n' | HOME="$T/home" "$EX_BIN" "$@" f 2>&1
	)
}
numbered() { grep -Eq '^ *1[[:space:]]+one' <<<"$1"; }
listed() { grep -q 'one\$' <<<"$1"; }

printf 'set nu\n' >"$T/proj/.exrc"
chmod 644 "$T/proj/.exrc"

out=$(run -)
if numbered "$out"; then
	fail "./.exrc was read without the exrc option: [$out]"
fi

printf 'set exrc\n' >"$T/home/.exrc"
chmod 644 "$T/home/.exrc"
out=$(run -)
if ! numbered "$out"; then
	fail "./.exrc was not read after ~/.exrc set exrc: [$out]"
fi

printf 'set list\n' >"$T/home/.exrc"
out=$(run 'set exrc')
if ! numbered "$out"; then
	fail "./.exrc was not read after EXINIT set exrc: [$out]"
fi
if listed "$out"; then
	fail "~/.exrc was read although EXINIT was set: [$out]"
fi

out=$(run - -s)
if listed "$out" || numbered "$out"; then
	fail "-s read ~/.exrc: [$out]"
fi
out=$(run 'set nu' -s)
if numbered "$out"; then
	fail "-s read EXINIT: [$out]"
fi
# nor ./.exrc, whatever would have set exrc: EXINIT, or ~/.exrc
out=$(run 'set exrc' -s)
if numbered "$out"; then
	fail "-s read ./.exrc after EXINIT set exrc: [$out]"
fi
printf 'set exrc\n' >"$T/home/.exrc"
out=$(run - -s)
if numbered "$out"; then
	fail "-s read ./.exrc after ~/.exrc set exrc: [$out]"
fi

out=$(printf 'set exrc?\nset ex\nset exrc?\nset noex\nset exrc?\nq!\n' \
	| HOME="$T/home" "$EX_BIN" -s 2>&1)
if [ "$out" != "$(printf 'noexrc\nexrc\nnoexrc')" ]; then
	fail "set exrc / ex / noex: [$out]"
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi
echo "PASS: test_ex_exrc"
