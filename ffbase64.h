// Copyright (c) 2026 Peter Fors
// SPDX-License-Identifier: MIT

#ifndef FFBASE64_H_INCLUDED
#define FFBASE64_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

// Decodes standard padded Base64 with 76 characters per CRLF-terminated line.
// The final line can be shorter and its CRLF is optional. Empty input is valid.
// Input and output buffers must be disjoint. Returns bytes written on success
// or SIZE_MAX for malformed input or insufficient capacity. Output may be
// modified on failure.
size_t ffbase64_decode_mime76(uint8_t *input, size_t size, uint8_t *output, size_t capacity);

#endif

#if defined(FFBASE64_IMPLEMENTATION) && !defined(FFBASE64_IMPLEMENTATION_INCLUDED)
#define FFBASE64_IMPLEMENTATION_INCLUDED

#include <cpuid.h>
#include <immintrin.h>
#include <string.h>

#define FFBASE64__LINE 76
#define FFBASE64__STRIDE 78
#define FFBASE64__DECODED_LINE 57
#define FFBASE64__BLOCK_LINES 4
#define FFBASE64__NT_LINES 64
#define FFBASE64__PREFETCH_DISTANCE 4096
#define FFBASE64__PREFETCH_MIN (16u << 20)
#define FFBASE64__NT_MIN (128u << 20)

enum ffbase64__store_policy {
	FFBASE64__CACHED,
	FFBASE64__PREFETCH,
	FFBASE64__STREAM
};

// CR and LF carry distinct tags in the top three bits and fail an alphabet check.
static uint8_t ffbase64__alphabet[256] = {
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0xa0, 0x80, 0x80, 0xc0, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 62, 0x80, 0x80, 0x80, 63,
	52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
	15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
	41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
	0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80
};

static uint8_t ffbase64__pack_index[64] = {
	2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, 18, 17, 16, 22,
	21, 20, 26, 25, 24, 30, 29, 28, 34, 33, 32, 38, 37, 36, 42, 41,
	40, 46, 45, 44, 50, 49, 48, 54, 53, 52, 58, 57, 56, 62, 61, 60,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static uint8_t ffbase64__seam_index[4][64] = {
	{
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 14, 15, 16, 17,
		18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
		34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
		50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0, 0, 12, 13
	},
	{
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 30, 31, 32, 33,
		34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
		50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0, 0, 28, 29
	},
	{
		0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
		16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
		32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 46, 47, 48, 49,
		50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 0, 0, 44, 45
	},
	{
		2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17,
		18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33,
		34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49,
		50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 2, 2, 62, 63
	}
};

// [=]===^=[ ffbase64__words ]==================================================[=]
__attribute__((target("avx512vbmi")))
static inline __m512i ffbase64__words(__m512i values) {
	__m512i pairs = _mm512_maddubs_epi16(values, _mm512_set1_epi32(0x01400140));

	return _mm512_madd_epi16(pairs, _mm512_set1_epi32(0x00011000));

}

// [=]===^=[ ffbase64__pack ]====================================================[=]
__attribute__((target("avx512vbmi")))
static inline __m512i ffbase64__pack(__m512i values, __m512i pack) {
	return _mm512_permutexvar_epi8(pack, ffbase64__words(values));

}

// [=]===^=[ ffbase64__is_crlf ]=================================================[=]
static inline uint32_t ffbase64__is_crlf(uint8_t *input) {
	uint16_t word;

	memcpy(&word, input, sizeof(word));
	return word == 0x0a0d;

}

// [=]===^=[ ffbase64__translate ]===============================================[=]
// Four lines fit in a 64-character vector and four 60-character vectors.
__attribute__((target("avx512vbmi"), always_inline))
static inline uint32_t ffbase64__translate(uint8_t *input, __m512i values[5]) {
	__m512i low = _mm512_loadu_si512(ffbase64__alphabet);
	__m512i high = _mm512_loadu_si512(ffbase64__alphabet + 64);
	__m512i seam1 = _mm512_loadu_si512(ffbase64__seam_index[0]);
	__m512i seam2 = _mm512_loadu_si512(ffbase64__seam_index[1]);
	__m512i seam3 = _mm512_loadu_si512(ffbase64__seam_index[2]);
	__m512i seam4 = _mm512_loadu_si512(ffbase64__seam_index[3]);
	__m512i crlf_tags = _mm512_set_epi64(UINT64_C(0xa0c0000000000000), 0, 0, 0, 0, 0, 0, 0);
	__m512i sign = _mm512_set1_epi8(-128);
	__m512i check = _mm512_mask_set1_epi8(sign, UINT64_C(3) << 62, -32);
	__m512i text0 = _mm512_loadu_si512(input);
	__m512i text1 = _mm512_permutexvar_epi8(seam1, _mm512_loadu_si512(input + 64));
	__m512i text2 = _mm512_permutexvar_epi8(seam2, _mm512_loadu_si512(input + 126));
	__m512i text3 = _mm512_permutexvar_epi8(seam3, _mm512_loadu_si512(input + 188));
	__m512i text4 = _mm512_permutexvar_epi8(seam4, _mm512_loadu_si512(input + 248));
	__m512i text012 = _mm512_ternarylogic_epi32(text0, text1, text2, 0xfe);
	__m512i text_bits = _mm512_ternarylogic_epi32(text012, text3, text4, 0xfe);
	__m512i values0 = _mm512_permutex2var_epi8(low, text0, high);
	__m512i values1 = _mm512_permutex2var_epi8(low, text1, high);
	__m512i values2 = _mm512_permutex2var_epi8(low, text2, high);
	__m512i values3 = _mm512_permutex2var_epi8(low, text3, high);
	__m512i values4 = _mm512_permutex2var_epi8(low, text4, high);
	__m512i invalid0 = _mm512_and_si512(_mm512_or_si512(text_bits, values0), sign);
	__m512i invalid1 = _mm512_ternarylogic_epi32(values1, values2, crlf_tags, 0x7e); // (values1 ^ crlf_tags) | (values2 ^ crlf_tags)
	__m512i invalid2 = _mm512_ternarylogic_epi32(values3, values4, crlf_tags, 0x7e);
	__m512i bad = _mm512_ternarylogic_epi32(invalid0, invalid1, invalid2, 0xfe);

	if(_mm512_test_epi8_mask(bad, check)) {
		return 0;

	}
	values[0] = values0;
	values[1] = values1;
	values[2] = values2;
	values[3] = values3;
	values[4] = values4;
	return 1;

}

// [=]===^=[ ffbase64__decode_cached ]===========================================[=]
__attribute__((target("avx512vbmi"), always_inline))
static inline uint32_t ffbase64__decode_cached(uint8_t *input, size_t blocks, uint8_t *output, uint32_t prefetch) {
	__m512i pack = _mm512_loadu_si512(ffbase64__pack_index);
	__mmask64 store48 = (UINT64_C(1) << 48) - 1;
	__mmask64 store45 = (UINT64_C(1) << 45) - 1;

	for(size_t i = 0; i < blocks; ++i) {
		__m512i values[5];

		if(prefetch) {
			__asm__("prefetchw %0" : : "m"(output[FFBASE64__PREFETCH_DISTANCE]));
			__asm__("prefetchw %0" : : "m"(output[FFBASE64__PREFETCH_DISTANCE + 64]));
			__asm__("prefetchw %0" : : "m"(output[FFBASE64__PREFETCH_DISTANCE + 128]));
			__asm__("prefetchw %0" : : "m"(output[FFBASE64__PREFETCH_DISTANCE + 192]));

		}

		if(!ffbase64__translate(input, values)) {
			return 0;

		}

		_mm512_mask_storeu_epi8(output, store48, ffbase64__pack(values[0], pack));
		_mm512_mask_storeu_epi8(output + 48, store45, ffbase64__pack(values[1], pack));
		_mm512_mask_storeu_epi8(output + 93, store45, ffbase64__pack(values[2], pack));
		_mm512_mask_storeu_epi8(output + 138, store45, ffbase64__pack(values[3], pack));
		_mm512_mask_storeu_epi8(output + 183, store45, ffbase64__pack(values[4], pack));
		input += FFBASE64__BLOCK_LINES * FFBASE64__STRIDE;
		output += FFBASE64__BLOCK_LINES * FFBASE64__DECODED_LINE;

	}

	return 1;

}

// [=]===^=[ ffbase64__decode_blocks ]===========================================[=]
__attribute__((target("avx512vbmi")))
static uint32_t ffbase64__decode_blocks(uint8_t *input, size_t blocks, uint8_t *output, uint32_t prefetch) {
	if(prefetch) {
		return ffbase64__decode_cached(input, blocks, output, 1);

	}

	return ffbase64__decode_cached(input, blocks, output, 0);

}

// [=]===^=[ ffbase64__decode_vector ]===========================================[=]
__attribute__((target("avx512vbmi"), always_inline))
static inline uint32_t ffbase64__decode_vector(uint8_t *input, uint8_t *output, uint32_t width) {
	__mmask64 read = UINT64_MAX >> (64 - width);
	__m512i text = _mm512_mask_loadu_epi8(_mm512_set1_epi8('A'), read, input);
	__m512i values = _mm512_permutex2var_epi8(_mm512_loadu_si512(ffbase64__alphabet), text, _mm512_loadu_si512(ffbase64__alphabet + 64));

	if(_mm512_movepi8_mask(_mm512_or_si512(text, values))) {
		return 0;

	}

	__mmask64 write = UINT64_MAX >> (64 - width / 4 * 3);
	_mm512_mask_storeu_epi8(output, write, ffbase64__pack(values, _mm512_loadu_si512(ffbase64__pack_index)));
	return 1;

}

// [=]===^=[ ffbase64__decode_simd_tail ]=========================================[=]
__attribute__((target("avx512vbmi")))
static uint32_t ffbase64__decode_simd_tail(uint8_t *input, uint8_t *output, size_t lines, uint32_t width) {
	for(size_t i = 0; i < lines; ++i) {
		if(!ffbase64__is_crlf(input + FFBASE64__LINE) || !ffbase64__decode_vector(input, output, 64) || !ffbase64__decode_vector(input + 64, output + 48, 12)) {
			return 0;

		}

		input += FFBASE64__STRIDE;
		output += FFBASE64__DECODED_LINE;

	}

	if(width > 64) {
		if(!ffbase64__decode_vector(input, output, 64)) {
			return 0;

		}

		input += 64;
		output += 48;
		width -= 64;

	}

	return !width || ffbase64__decode_vector(input, output, width);

}

#define FFBASE64__PACK_BYTE(i) ((i) / 3 * 4 + 2 - (i) % 3)
#define FFBASE64__MIX_BYTE(i, skip, first) ((i) < (first) ? FFBASE64__PACK_BYTE((i) + (skip)) : 64 + FFBASE64__PACK_BYTE((i) - (first)))
#define FFBASE64__LAST_BYTE(i, start) ((i) < (start) ? 0 : FFBASE64__PACK_BYTE((i) - (start)))
#define FFBASE64__INDEX_ROW(map, ...) \
	map(0, __VA_ARGS__), map(1, __VA_ARGS__), map(2, __VA_ARGS__), map(3, __VA_ARGS__), map(4, __VA_ARGS__), map(5, __VA_ARGS__), map(6, __VA_ARGS__), map(7, __VA_ARGS__),\
	map(8, __VA_ARGS__), map(9, __VA_ARGS__), map(10, __VA_ARGS__), map(11, __VA_ARGS__), map(12, __VA_ARGS__), map(13, __VA_ARGS__), map(14, __VA_ARGS__), map(15, __VA_ARGS__),\
	map(16, __VA_ARGS__), map(17, __VA_ARGS__), map(18, __VA_ARGS__), map(19, __VA_ARGS__), map(20, __VA_ARGS__), map(21, __VA_ARGS__), map(22, __VA_ARGS__), map(23, __VA_ARGS__),\
	map(24, __VA_ARGS__), map(25, __VA_ARGS__), map(26, __VA_ARGS__), map(27, __VA_ARGS__), map(28, __VA_ARGS__), map(29, __VA_ARGS__), map(30, __VA_ARGS__), map(31, __VA_ARGS__),\
	map(32, __VA_ARGS__), map(33, __VA_ARGS__), map(34, __VA_ARGS__), map(35, __VA_ARGS__), map(36, __VA_ARGS__), map(37, __VA_ARGS__), map(38, __VA_ARGS__), map(39, __VA_ARGS__),\
	map(40, __VA_ARGS__), map(41, __VA_ARGS__), map(42, __VA_ARGS__), map(43, __VA_ARGS__), map(44, __VA_ARGS__), map(45, __VA_ARGS__), map(46, __VA_ARGS__), map(47, __VA_ARGS__),\
	map(48, __VA_ARGS__), map(49, __VA_ARGS__), map(50, __VA_ARGS__), map(51, __VA_ARGS__), map(52, __VA_ARGS__), map(53, __VA_ARGS__), map(54, __VA_ARGS__), map(55, __VA_ARGS__),\
	map(56, __VA_ARGS__), map(57, __VA_ARGS__), map(58, __VA_ARGS__), map(59, __VA_ARGS__), map(60, __VA_ARGS__), map(61, __VA_ARGS__), map(62, __VA_ARGS__), map(63, __VA_ARGS__)

#define FFBASE64__NT_DECODE(group) \
	__builtin_prefetch(input + (FFBASE64__NT_LINES + (group) * FFBASE64__BLOCK_LINES) * FFBASE64__STRIDE, 0, 3); \
	__builtin_prefetch(input + (FFBASE64__NT_LINES + (group) * FFBASE64__BLOCK_LINES) * FFBASE64__STRIDE + 128, 0, 3); \
	__builtin_prefetch(input + (FFBASE64__NT_LINES + (group) * FFBASE64__BLOCK_LINES) * FFBASE64__STRIDE + 256, 0, 3); \
	__m512i words_##group[5]; \
\
	if(!ffbase64__translate(input + (group) * FFBASE64__BLOCK_LINES * FFBASE64__STRIDE, words_##group)) { \
		return 0; \
\
	} \
\
	words_##group[0] = ffbase64__words(words_##group[0]); \
	words_##group[1] = ffbase64__words(words_##group[1]); \
	words_##group[2] = ffbase64__words(words_##group[2]); \
	words_##group[3] = ffbase64__words(words_##group[3]); \
	words_##group[4] = ffbase64__words(words_##group[4])

#define FFBASE64__NT_PAIR(line, a, b, skip, first) do { \
	static uint8_t indexes[64] = { FFBASE64__INDEX_ROW(FFBASE64__MIX_BYTE, skip, first) }; \
	__m512i bytes = _mm512_permutex2var_epi8(a, _mm512_loadu_si512(indexes), b); \
	_mm512_stream_si512((__m512i *)(output + (line) * 64), bytes); \
\
} while(0)

#define FFBASE64__NT_TRIPLE(line, a, b, c, skip, first, start) do { \
	static uint8_t indexes[64] = { FFBASE64__INDEX_ROW(FFBASE64__MIX_BYTE, skip, first) }; \
	static uint8_t last[64] = { FFBASE64__INDEX_ROW(FFBASE64__LAST_BYTE, start) }; \
	__m512i bytes = _mm512_permutex2var_epi8(a, _mm512_loadu_si512(indexes), b); \
	bytes = _mm512_mask_permutexvar_epi8(bytes, UINT64_MAX << (start), _mm512_loadu_si512(last), c); \
	_mm512_stream_si512((__m512i *)(output + (line) * 64), bytes); \
\
} while(0)

// [=]===^=[ ffbase64__decode_nt ]===============================================[=]
// Sixty-four MIME lines produce 57 aligned stores. Each store takes bytes from two or three adjacent vectors.
__attribute__((target("avx512vbmi")))
static uint32_t ffbase64__decode_nt(uint8_t *input, size_t tiles, uint8_t *output) {
	for(size_t i = 0; i < tiles; ++i) {
		FFBASE64__NT_DECODE(0);
		FFBASE64__NT_PAIR(0, words_0[0], words_0[1], 0, 48);
		FFBASE64__NT_PAIR(1, words_0[1], words_0[2], 16, 29);
		FFBASE64__NT_TRIPLE(2, words_0[2], words_0[3], words_0[4], 35, 10, 55);

		FFBASE64__NT_DECODE(1);
		FFBASE64__NT_PAIR(3, words_0[4], words_1[0], 9, 36);
		FFBASE64__NT_PAIR(4, words_1[0], words_1[1], 28, 20);
		FFBASE64__NT_TRIPLE(5, words_1[1], words_1[2], words_1[3], 44, 1, 46);
		FFBASE64__NT_PAIR(6, words_1[3], words_1[4], 18, 27);

		FFBASE64__NT_DECODE(2);
		FFBASE64__NT_TRIPLE(7, words_1[4], words_2[0], words_2[1], 37, 8, 56);
		FFBASE64__NT_PAIR(8, words_2[1], words_2[2], 8, 37);
		FFBASE64__NT_TRIPLE(9, words_2[2], words_2[3], words_2[4], 27, 18, 63);

		FFBASE64__NT_DECODE(3);
		FFBASE64__NT_PAIR(10, words_2[4], words_3[0], 1, 44);
		FFBASE64__NT_PAIR(11, words_3[0], words_3[1], 20, 28);
		FFBASE64__NT_TRIPLE(12, words_3[1], words_3[2], words_3[3], 36, 9, 54);
		FFBASE64__NT_PAIR(13, words_3[3], words_3[4], 10, 35);

		FFBASE64__NT_DECODE(4);
		FFBASE64__NT_PAIR(14, words_3[4], words_4[0], 29, 16);
		FFBASE64__NT_PAIR(15, words_4[1], words_4[2], 0, 45);
		FFBASE64__NT_PAIR(16, words_4[2], words_4[3], 19, 26);

		FFBASE64__NT_DECODE(5);
		FFBASE64__NT_TRIPLE(17, words_4[3], words_4[4], words_5[0], 38, 7, 52);
		FFBASE64__NT_PAIR(18, words_5[0], words_5[1], 12, 36);
		FFBASE64__NT_TRIPLE(19, words_5[1], words_5[2], words_5[3], 28, 17, 62);
		FFBASE64__NT_PAIR(20, words_5[3], words_5[4], 2, 43);

		FFBASE64__NT_DECODE(6);
		FFBASE64__NT_PAIR(21, words_5[4], words_6[0], 21, 24);
		FFBASE64__NT_TRIPLE(22, words_6[0], words_6[1], words_6[2], 40, 8, 53);
		FFBASE64__NT_PAIR(23, words_6[2], words_6[3], 11, 34);

		FFBASE64__NT_DECODE(7);
		FFBASE64__NT_TRIPLE(24, words_6[3], words_6[4], words_7[0], 30, 15, 60);
		FFBASE64__NT_PAIR(25, words_7[0], words_7[1], 4, 44);
		FFBASE64__NT_PAIR(26, words_7[1], words_7[2], 20, 25);
		FFBASE64__NT_TRIPLE(27, words_7[2], words_7[3], words_7[4], 39, 6, 51);

		FFBASE64__NT_DECODE(8);
		FFBASE64__NT_PAIR(28, words_7[4], words_8[0], 13, 32);
		FFBASE64__NT_TRIPLE(29, words_8[0], words_8[1], words_8[2], 32, 16, 61);
		FFBASE64__NT_PAIR(30, words_8[2], words_8[3], 3, 42);
		FFBASE64__NT_PAIR(31, words_8[3], words_8[4], 22, 23);

		FFBASE64__NT_DECODE(9);
		FFBASE64__NT_TRIPLE(32, words_8[4], words_9[0], words_9[1], 41, 4, 52);
		FFBASE64__NT_PAIR(33, words_9[1], words_9[2], 12, 33);
		FFBASE64__NT_TRIPLE(34, words_9[2], words_9[3], words_9[4], 31, 14, 59);

		FFBASE64__NT_DECODE(10);
		FFBASE64__NT_PAIR(35, words_9[4], words_10[0], 5, 40);
		FFBASE64__NT_PAIR(36, words_10[0], words_10[1], 24, 24);
		FFBASE64__NT_TRIPLE(37, words_10[1], words_10[2], words_10[3], 40, 5, 50);
		FFBASE64__NT_PAIR(38, words_10[3], words_10[4], 14, 31);

		FFBASE64__NT_DECODE(11);
		FFBASE64__NT_TRIPLE(39, words_10[4], words_11[0], words_11[1], 33, 12, 60);
		FFBASE64__NT_PAIR(40, words_11[1], words_11[2], 4, 41);
		FFBASE64__NT_PAIR(41, words_11[2], words_11[3], 23, 22);

		FFBASE64__NT_DECODE(12);
		FFBASE64__NT_TRIPLE(42, words_11[3], words_11[4], words_12[0], 42, 3, 48);
		FFBASE64__NT_PAIR(43, words_12[0], words_12[1], 16, 32);
		FFBASE64__NT_TRIPLE(44, words_12[1], words_12[2], words_12[3], 32, 13, 58);
		FFBASE64__NT_PAIR(45, words_12[3], words_12[4], 6, 39);

		FFBASE64__NT_DECODE(13);
		FFBASE64__NT_PAIR(46, words_12[4], words_13[0], 25, 20);
		FFBASE64__NT_TRIPLE(47, words_13[0], words_13[1], words_13[2], 44, 4, 49);
		FFBASE64__NT_PAIR(48, words_13[2], words_13[3], 15, 30);

		FFBASE64__NT_DECODE(14);
		FFBASE64__NT_TRIPLE(49, words_13[3], words_13[4], words_14[0], 34, 11, 56);
		FFBASE64__NT_PAIR(50, words_14[0], words_14[1], 8, 40);
		FFBASE64__NT_PAIR(51, words_14[1], words_14[2], 24, 21);
		FFBASE64__NT_TRIPLE(52, words_14[2], words_14[3], words_14[4], 43, 2, 47);

		FFBASE64__NT_DECODE(15);
		FFBASE64__NT_PAIR(53, words_14[4], words_15[0], 17, 28);
		FFBASE64__NT_TRIPLE(54, words_15[0], words_15[1], words_15[2], 36, 12, 57);
		FFBASE64__NT_PAIR(55, words_15[2], words_15[3], 7, 38);
		FFBASE64__NT_PAIR(56, words_15[3], words_15[4], 26, 19);

		input += FFBASE64__NT_LINES * FFBASE64__STRIDE;
		output += FFBASE64__NT_LINES * FFBASE64__DECODED_LINE;

	}

	return 1;

}

#undef FFBASE64__NT_TRIPLE
#undef FFBASE64__NT_PAIR
#undef FFBASE64__NT_DECODE
#undef FFBASE64__INDEX_ROW
#undef FFBASE64__LAST_BYTE
#undef FFBASE64__MIX_BYTE
#undef FFBASE64__PACK_BYTE

// [=]===^=[ ffbase64_decode_mime76 ]============================================[=]
size_t ffbase64_decode_mime76(uint8_t *input, size_t size, uint8_t *output, size_t capacity) {
	if(!size) {
		return 0;

	}

	if(!input || !output) {
		return SIZE_MAX;

	}

	if(size > 2 && ffbase64__is_crlf(input + size - 2)) {
		size -= 2;

	}

	size_t tail = (size - 1) % FFBASE64__STRIDE + 1;
	size_t lines = (size - tail) / FFBASE64__STRIDE;

	if(tail > FFBASE64__LINE || (tail & 3)) {
		return SIZE_MAX;

	}

	size_t padding = (input[size - 1] == '=') + (input[size - 1] == '=' && input[size - 2] == '=');
	size_t decoded = lines * FFBASE64__DECODED_LINE + tail / 4 * 3 - padding;

	if(capacity < decoded) {
		return SIZE_MAX;

	}

	uint32_t full_tail = tail - (padding ? 4 : 0);

	if(size >= 16 && __builtin_cpu_supports("avx512vbmi")) {
		enum ffbase64__store_policy policy = FFBASE64__CACHED;

		if(decoded >= FFBASE64__PREFETCH_MIN && __builtin_cpu_is("amd")) {
			uint32_t eax, ebx, ecx, edx;

			__cpuid(1, eax, ebx, ecx, edx);

			if((eax & 0x0fff0ff0) == 0x00b40f40) { // AMD family 1ah, model 44h.
				policy = decoded >= FFBASE64__NT_MIN ? FFBASE64__STREAM : FFBASE64__PREFETCH;

			}
		}

		if(policy == FFBASE64__STREAM) {
			size_t peel = (-(uintptr_t)output * 9) & 63; // 9 is the inverse of 57 modulo 64.

			if(peel && !ffbase64__decode_simd_tail(input, output, peel, 0)) {
				return SIZE_MAX;

			}

			input += peel * FFBASE64__STRIDE;
			output += peel * FFBASE64__DECODED_LINE;
			lines -= peel;
			size_t tiles = lines / FFBASE64__NT_LINES - 1; // Keep a complete input tile for the lookahead.
			uint32_t valid = ffbase64__decode_nt(input, tiles, output);

			_mm_sfence();

			if(!valid) {
				return SIZE_MAX;

			}

			input += tiles * FFBASE64__NT_LINES * FFBASE64__STRIDE;
			output += tiles * FFBASE64__NT_LINES * FFBASE64__DECODED_LINE;
			lines -= tiles * FFBASE64__NT_LINES;

		}

		size_t blocks = lines / FFBASE64__BLOCK_LINES;
		size_t guard = (FFBASE64__PREFETCH_DISTANCE + 192) / (FFBASE64__BLOCK_LINES * FFBASE64__DECODED_LINE) + 1;

		if(policy == FFBASE64__PREFETCH && blocks > guard) {
			size_t prefetched = blocks - guard;

			if(!ffbase64__decode_blocks(input, prefetched, output, 1)) {
				return SIZE_MAX;

			}

			input += prefetched * FFBASE64__BLOCK_LINES * FFBASE64__STRIDE;
			output += prefetched * FFBASE64__BLOCK_LINES * FFBASE64__DECODED_LINE;
			lines -= prefetched * FFBASE64__BLOCK_LINES;
			blocks = guard;

		}

		if(blocks && !ffbase64__decode_blocks(input, blocks, output, 0)) {
			return SIZE_MAX;

		}

		input += blocks * FFBASE64__BLOCK_LINES * FFBASE64__STRIDE;
		output += blocks * FFBASE64__BLOCK_LINES * FFBASE64__DECODED_LINE;
		lines -= blocks * FFBASE64__BLOCK_LINES;

		if((lines || full_tail) && !ffbase64__decode_simd_tail(input, output, lines, full_tail)) {
			return SIZE_MAX;

		}

		input += lines * FFBASE64__STRIDE + full_tail;
		output += lines * FFBASE64__DECODED_LINE + full_tail / 4 * 3;

	} else {
		for(size_t line = 0; line <= lines; ++line) {
			size_t width = line < lines ? FFBASE64__LINE : full_tail;

			for(size_t i = 0; i < width; i += 4) {
				uint32_t a = ffbase64__alphabet[input[i]];
				uint32_t b = ffbase64__alphabet[input[i + 1]];
				uint32_t c = ffbase64__alphabet[input[i + 2]];
				uint32_t d = ffbase64__alphabet[input[i + 3]];

				if((a | b | c | d) & 0x80) {
					return SIZE_MAX;

				}

				*output++ = (a << 2) | (b >> 4);
				*output++ = (b << 4) | (c >> 2);
				*output++ = (c << 6) | d;

			}

			input += width;

			if(line < lines) {
				if(!ffbase64__is_crlf(input)) {
					return SIZE_MAX;

				}

				input += 2;

			}
		}
	}

	if(padding) {
		uint32_t a = ffbase64__alphabet[input[0]];
		uint32_t b = ffbase64__alphabet[input[1]];
		uint32_t c = padding == 1 ? ffbase64__alphabet[input[2]] : 0;

		if(((a | b | c) & 0x80) || (padding == 1 ? c & 3 : b & 15)) {
			return SIZE_MAX;

		}

		*output++ = (a << 2) | (b >> 4);

		if(padding == 1) {
			*output = (b << 4) | (c >> 2);

		}
	}

	return decoded;

}

#endif
