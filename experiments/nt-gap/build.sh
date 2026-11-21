#!/bin/sh
# Copyright (c) 2026 Peter Fors
# SPDX-License-Identifier: MIT
set -eu

source_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$source_dir/../.." && pwd)
build_dir=${1:?usage: build.sh BUILD_DIR}
mkdir -p -- "$build_dir"
cd -- "$build_dir"
python3 "$source_dir/make_modes.py" "$repo_dir/ffbase64.h"

for mode in installed forced shared; do
	gcc -I"$repo_dir" -std=c99 -O2 -Wall -Wextra -Werror -fPIC -c "$mode.c" -o "$mode.o"
done

g++ -std=gnu++17 -O2 -Wall -Wextra -Werror -c "$source_dir/bench.cpp" -o bench.o
g++ bench.o installed.o forced.o shared.o -o bench
g++ bench.o forced.o shared.o installed.o -o bench-reordered

python3 "$source_dir/make_roof.py"
gcc -std=c99 -O3 -mavx512f -Wall -Wextra -Werror roof.c -o roof
