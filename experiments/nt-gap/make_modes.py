# Copyright (c) 2026 Peter Fors
# SPDX-License-Identifier: MIT

import sys
from pathlib import Path

src = Path(sys.argv[1]).read_text()
start = src.index('\t\tif(decoded >= FFBASE64__PREFETCH_MIN')
end = src.index('\n\t\tif(policy == FFBASE64__STREAM)', start)

forced = src[:start] + '\t\tpolicy = decoded >= 12288 ? FFBASE64__STREAM : FFBASE64__CACHED;\n' + src[end:]
Path('forced.h').write_text(forced.replace('ffbase64_decode_mime76(', 'ffbase64_forced('))
shared = src[:start] + '\t\tif(force) {\n\t\t\tpolicy = decoded >= FFBASE64__NT_MIN ? FFBASE64__STREAM : FFBASE64__CACHED;\n\n\t\t} else '+src[start+2:]
pos = shared.index('\nsize_t ffbase64_decode_mime76(', shared.index('// [=]===^=[ ffbase64_decode_mime76 ]'))
shared = shared[:pos] + shared[pos:].replace(
    '\nsize_t ffbase64_decode_mime76(uint8_t *input, size_t size, uint8_t *output, size_t capacity) {',
    '\n__attribute__((noipa))\nstatic size_t ffbase64__shared(uint8_t *input, size_t size, uint8_t *output, size_t capacity, uint32_t force) {', 1)
pos = shared.rindex('#endif')
shared = shared[:pos] + '''// [=]===^=[ ffbase64_shared_dispatch ]========================================[=]
size_t ffbase64_shared_dispatch(uint8_t *input, size_t size, uint8_t *output, size_t capacity) {
	return ffbase64__shared(input, size, output, capacity, 0);

}

// [=]===^=[ ffbase64_shared_force ]===========================================[=]
size_t ffbase64_shared_force(uint8_t *input, size_t size, uint8_t *output, size_t capacity) {
	return ffbase64__shared(input, size, output, capacity, 1);

}

''' + shared[pos:]
Path('shared.h').write_text(shared)
for name, header in [('installed','ffbase64'), ('forced','forced'), ('shared','shared')]:
    Path(name+'.c').write_text(f'#define FFBASE64_IMPLEMENTATION\n#include "{header}.h"\n')
