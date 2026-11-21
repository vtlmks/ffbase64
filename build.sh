#!/bin/sh
# Copyright (c) 2026 Peter Fors
# SPDX-License-Identifier: MIT
set -eu

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CC=${CC:-gcc}
CFLAGS=${CFLAGS:--O2 -Wall -Wextra}
OUTPUT=${OUTPUT:-bench}

"$CC" -std=c99 $CFLAGS "$source_dir/bench.c" -o "$OUTPUT"
