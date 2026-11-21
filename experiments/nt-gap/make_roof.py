# Copyright (c) 2026 Peter Fors
# SPDX-License-Identifier: MIT

from pathlib import Path

parts = ['''// Copyright (c) 2026 Peter Fors
// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L
#include <immintrin.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define BLOCKS ((size_t)(256u << 20) / 3648)

// [=]===^=[ stream ]==========================================================[=]
__attribute__((noinline))
static void stream(uint8_t *in, uint8_t *out) {
	for(size_t i = 0; i < BLOCKS; ++i) {
''']

for base in range(0, 78, 6):
    for j in range(base, base + 6):
        parts.append(f'\t\t__m512i v{j} = _mm512_load_si512(in + {j * 64});\n')
    for j in range(base, base + 6):
        lo = j * 57 // 78
        hi = (j + 1) * 57 // 78
        if hi > lo:
            parts.append(f'\t\t_mm512_stream_si512((__m512i *)(out + {lo * 64}), v{j});\n')
        else:
            parts.append(f'\t\t__asm__ volatile("" : : "v"(v{j}));\n')

parts.append('''
		in += 4992;
		out += 3648;

	}

	_mm_sfence();

}

// [=]===^=[ now ]============================================================[=]
static uint64_t now(void) {
	struct timespec ts;

	if(clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) {
		exit(1);

	}

	return UINT64_C(1000000000) * ts.tv_sec + ts.tv_nsec;

}

// [=]===^=[ compare ]========================================================[=]
static int compare(const void *a, const void *b) {
	double x = *(const double *)a;
	double y = *(const double *)b;

	return (x > y) - (x < y);

}

// [=]===^=[ main ]===========================================================[=]
int main(void) {
	size_t length = BLOCKS * 4992;
	size_t decoded = BLOCKS * 3648;
	void *input = NULL;
	void *output = NULL;

	if(posix_memalign(&input, 4096, length) || posix_memalign(&output, 4096, decoded)) {
		return 1;

	}

	memset(input, 0x5a, length);
	memset(output, 0x55, decoded);
	double rates[7];

	stream(input, output);
	stream(input, output);

	for(uint32_t i = 0; i < 7; ++i) {
		uint64_t start = now();

		for(uint32_t pass = 0; pass < 3; ++pass) {
			__asm__ volatile("" : "+m"(*(uint8_t (*)[length])input));
			stream(input, output);
			__asm__ volatile("" : : "m"(*(uint8_t (*)[decoded])output));

		}

		uint64_t elapsed = now() - start;

		if(memcmp(input, output, decoded)) {
			return 2;

		}

		rates[i] = (double)length * 3 / elapsed;

	}

	qsort(rates, 7, sizeof(rates[0]), compare);
	printf("%.4f GB/s input, %.4f GB/s output, range %.4f..%.4f GB/s input\\n", rates[3], rates[3] * 57 / 78, rates[0], rates[6]);
	free(output);
	free(input);
	return 0;

}
''')

Path('roof.c').write_text(''.join(parts))
