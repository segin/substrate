#!/bin/sh
# __STDC_HOSTED__ is 1, as C99 has a hosted implementation define it,
# and 0 under -ffreestanding (Linux's EFI stub and kexec purgatory build
# with it), as gcc has them; -fhosted undoes -ffreestanding.  cc defined
# none, and took -ffreestanding and ignored it.
set -eu

ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
CC_BIN="$ROOT/usr.bin/cc/cc"
if ! command -v gcc >/dev/null 2>&1; then
	echo "run_stdc_hosted: skip (no gcc)"
	exit 0
fi

for f in "" -ffreestanding -fhosted "-ffreestanding -fhosted" "-fhosted -ffreestanding"; do
	for m in -m64 -m32; do
		# shellcheck disable=SC2086
		want=$(gcc $m $f -dM -E - </dev/null 2>/dev/null | grep '__STDC_HOSTED__' || true)
		# shellcheck disable=SC2086
		got=$("$CC_BIN" $m $f -dM -E - </dev/null | grep '__STDC_HOSTED__' || true)
		if [ "$got" != "$want" ]; then
			echo "run_stdc_hosted: [$m $f] defines [$got], gcc [$want]"
			exit 1
		fi
	done
done
echo "run_stdc_hosted: ok"
