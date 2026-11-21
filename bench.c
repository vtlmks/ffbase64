// Copyright (c) 2026 Peter Fors
// SPDX-License-Identifier: MIT

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <time.h>

#define FFBASE64_IMPLEMENTATION
#include "ffbase64.h"

#define BENCH_DECODED 1000000
#define BENCH_LINE_WIDTH 76
#define BENCH_BASE64 (4 * ((BENCH_DECODED + 2) / 3))
#define BENCH_ENCODED (BENCH_BASE64 + 2 * ((BENCH_BASE64 + BENCH_LINE_WIDTH - 1) / BENCH_LINE_WIDTH))
#define BENCH_RUNS 3
#define BENCH_BATCHES 10
#define BENCH_PASSES 200

__attribute__((aligned(64)))
static uint8_t encoded[BENCH_ENCODED];
__attribute__((aligned(64)))
static uint8_t decoded[BENCH_DECODED];
static uint8_t expected[BENCH_DECODED];
static uint8_t alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// [=]===^=[ encode_fixture ]====================================================[=]
static size_t encode_fixture(size_t size) {
	size_t cursor = 0;
	uint32_t column = 0;

	for(size_t i = 0; i < size; i += 3) {
		size_t remaining = size - i;
		uint32_t word = (uint32_t)expected[i] << 16;

		if(remaining > 1) {
			word |= (uint32_t)expected[i + 1] << 8;

		}

		if(remaining > 2) {
			word |= expected[i + 2];

		}

		encoded[cursor++] = alphabet[word >> 18];
		encoded[cursor++] = alphabet[(word >> 12) & 63];
		encoded[cursor++] = remaining > 1 ? alphabet[(word >> 6) & 63] : '=';
		encoded[cursor++] = remaining > 2 ? alphabet[word & 63] : '=';
		column += 4;

		if(column == BENCH_LINE_WIDTH || remaining <= 3) {
			encoded[cursor++] = '\r';
			encoded[cursor++] = '\n';
			column = 0;

		}
	}

	return cursor;

}

// [=]===^=[ output_matches ]====================================================[=]
static uint32_t output_matches(size_t written, size_t size) {
	return written == size && !memcmp(decoded, expected, size);

}

// [=]===^=[ main ]==============================================================[=]
int main(int argc, char **argv) {
	uint64_t state = UINT64_C(0x123456789abcdef);

	if(argc != 1 && (argc != 2 || strcmp(argv[1], "--check"))) {
		fprintf(stderr, "usage: %s [--check]\n", argv[0]);
		return 1;

	}

	for(size_t i = 0; i < sizeof(expected); i += sizeof(state)) {
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		uint64_t word = state * UINT64_C(0x2545f4914f6cdd1d);
		memcpy(expected + i, &word, sizeof(word));

	}

	if(argc == 2) {
		for(size_t size = 0; size <= 512; ++size) {
			size_t length = encode_fixture(size);

			for(uint32_t trim = 0; trim <= (size != 0); ++trim) {
				memset(decoded, 0xa5, size + 1);
				size_t written = ffbase64_decode_mime76(encoded, length - trim * 2, decoded, size);

				if(!output_matches(written, size) || decoded[size] != 0xa5) {
					fprintf(stderr, "decoded output mismatch at %zu bytes\n", size);
					return 1;

				}
			}
		}

		size_t length = encode_fixture(512);
		uint8_t bad_padding[] = "Zh==";
		uint8_t one_byte[] = "Zg==";
		encoded[BENCH_LINE_WIDTH] = ' ';
		size_t invalid_break = ffbase64_decode_mime76(encoded, length, decoded, sizeof(decoded));
		size_t invalid_padding = ffbase64_decode_mime76(bad_padding, sizeof(bad_padding) - 1, decoded, sizeof(decoded));
		size_t insufficient_capacity = ffbase64_decode_mime76(one_byte, sizeof(one_byte) - 1, decoded, 0);

		if(invalid_break != SIZE_MAX || invalid_padding != SIZE_MAX || insufficient_capacity != SIZE_MAX) {
			fprintf(stderr, "decoder accepted invalid input or insufficient capacity\n");
			return 1;

		}

		puts("Correctness checks passed.");
		return 0;

	}

	size_t length = encode_fixture(sizeof(expected));
	double rates[BENCH_RUNS];

	for(uint32_t run = 0; run < BENCH_RUNS; ++run) {
		double best = 0;

		for(uint32_t batch = 0; batch <= BENCH_BATCHES; ++batch) {
			struct timespec start, end;
			size_t written = 0;
			memset(decoded, 0xa5, sizeof(decoded));

			if(clock_gettime(CLOCK_MONOTONIC, &start)) {
				perror("clock_gettime");
				return 1;

			}

			for(uint32_t pass = 0; pass < BENCH_PASSES; ++pass) {
				__asm__ volatile("" : "+m"(encoded));
				written = ffbase64_decode_mime76(encoded, length, decoded, sizeof(decoded));
				__asm__ volatile("" : : "r"(written), "m"(decoded));

			}

			if(clock_gettime(CLOCK_MONOTONIC, &end)) {
				perror("clock_gettime");
				return 1;

			}

			if(!output_matches(written, sizeof(expected))) {
				fprintf(stderr, "decoded output mismatch in run %u, batch %u\n", run + 1, batch);
				return 1;

			}

			int64_t elapsed = (int64_t)(end.tv_sec - start.tv_sec) * INT64_C(1000000000) + end.tv_nsec - start.tv_nsec;

			if(elapsed <= 0) {
				fprintf(stderr, "expected a positive elapsed time\n");
				return 1;

			}

			double rate = (double)length * BENCH_PASSES / elapsed;

			if(batch && rate > best) {
				best = rate;

			}
		}

		rates[run] = best;

	}

	double low = rates[0];
	double high = rates[0];
	double total = 0;

	for(uint32_t run = 0; run < BENCH_RUNS; ++run) {
		double rate = rates[run];
		low = rate < low ? rate : low;
		high = rate > high ? rate : high;
		total += rate;

	}

	printf("%zu decoded bytes, %zu encoded bytes\n", sizeof(expected), length);
	printf("%.2f encoded GB/s (median, range %.2f to %.2f)\n", total - low - high, low, high);
	return 0;

}
