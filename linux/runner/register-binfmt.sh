#!/bin/sh
set -eu

runner="/usr/local/bin/substrate-run"
register="/proc/sys/fs/binfmt_misc/register"
mountpoint="/proc/sys/fs/binfmt_misc"

while [ "$#" -gt 0 ]; do
	case "$1" in
	--runner)
		shift
		if [ "$#" -eq 0 ]; then
			echo "register-binfmt.sh: --runner requires a path" >&2
			exit 2
		fi
		runner=$1
		;;
	--help|-h)
		echo "usage: register-binfmt.sh [--runner /path/to/substrate-run]"
		exit 0
		;;
	*)
		echo "register-binfmt.sh: unknown option: $1" >&2
		exit 2
		;;
	esac
	shift
done

if [ ! -x "$runner" ]; then
	echo "register-binfmt.sh: runner is not executable: $runner" >&2
	exit 1
fi

if [ ! -d "$mountpoint" ]; then
	echo "register-binfmt.sh: binfmt_misc filesystem is unavailable" >&2
	exit 1
fi

if [ ! -e "$register" ]; then
	mount -t binfmt_misc binfmt_misc "$mountpoint"
fi

disable_entry() {
	name=$1
	if [ -e "$mountpoint/$name" ]; then
		printf '%s\n' -1 > "$mountpoint/$name"
	fi
}

register_entry() {
	name=$1
	type=$2
	entry=":$name:M::\\x7fELF\\x01\\x01\\x01\\x40\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x$type\\x00\\x03\\x00:\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\xff\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\x00\\xff\\xff\\xff\\xff:$runner:"
	printf '%s\n' "$entry" > "$register"
}

disable_entry substrate-i386-exec
disable_entry substrate-i386-dyn
register_entry substrate-i386-exec 02
register_entry substrate-i386-dyn 03

echo "registered Substrate i386 binfmt_misc handlers for $runner"
