#!/bin/sh
set -eu

TOP=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CHMOD_SAN_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
HOSTCFLAGS='-O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'

make -C "$TOP/bin/chmod" clean
make -C "$TOP/bin/chmod" NATIVE_BUILD=1 HOSTCFLAGS="$HOSTCFLAGS" USER_LDFLAGS='-fsanitize=address,undefined'

make -C "$TOP/tests/chmod" clean
make -C "$TOP/tests/chmod" all SAN_FLAGS="$CHMOD_SAN_FLAGS" CHMOD_BIN=../../bin/chmod/chmod
make -C "$TOP/tests/chmod" ci CHMOD_BIN=../../bin/chmod/chmod
