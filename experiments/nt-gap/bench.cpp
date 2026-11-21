// Copyright (c) 2026 Peter Fors
// SPDX-License-Identifier: MIT

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <linux/perf_event.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" {
size_t ffbase64_decode_mime76(uint8_t *, size_t, uint8_t *, size_t);
size_t ffbase64_forced(uint8_t *, size_t, uint8_t *, size_t);
size_t ffbase64_shared_dispatch(uint8_t *, size_t, uint8_t *, size_t);
size_t ffbase64_shared_force(uint8_t *, size_t, uint8_t *, size_t);
}

struct fixture {
	uint8_t *input;
	uint8_t *output;
	uint8_t *expected;
	size_t encoded;
	size_t decoded;
};

struct counts {
	uint64_t nr;
	uint64_t enabled;
	uint64_t running;
	uint64_t cycles;
	uint64_t dram;
	uint64_t l2;
};

struct sample {
	double speed;
	double cycles;
	double dram;
	double l2;
};

static int counters[3];
static uint64_t enabled;
static uint64_t running;

// [=]===^=[ open_counter ]====================================================[=]
static int open_counter(uint32_t type, uint64_t config, int group) {
	struct perf_event_attr attr = {};
	attr.type = type;
	attr.size = sizeof(attr);
	attr.config = config;
	attr.disabled = group < 0;
	attr.exclude_kernel = 1;
	attr.exclude_hv = 1;
	attr.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
	int fd = syscall(SYS_perf_event_open, &attr, 0, -1, group, PERF_FLAG_FD_CLOEXEC);

	if(fd < 0) {
		std::perror("perf_event_open");
		std::exit(1);

	}

	return fd;

}

// [=]===^=[ measure ]========================================================[=]
template<auto decode>
__attribute__((noinline))
static struct sample measure(struct fixture &data, uint32_t passes, bool timed) {
	uint8_t *input = data.input;
	uint8_t *output = data.output;
	size_t encoded = data.encoded;
	size_t decoded = data.decoded;
	size_t result = 0;
	int fd = counters[0];

	if(timed && (ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP) || ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP))) {
		std::perror("enable counter group");
		std::exit(2);

	}

	auto start = std::chrono::steady_clock::now();

	for(uint32_t i = 0; i < passes; ++i) {
		__asm__ volatile("" : "+m"(*(uint8_t (*)[encoded])input));
		result = decode(input, encoded, output, decoded);
		__asm__ volatile("" : : "r"(result), "m"(*(uint8_t (*)[decoded])output));

	}

	auto end = std::chrono::steady_clock::now();

	if(result != decoded) {
		std::fprintf(stderr, "Decoded length mismatch\n");
		std::exit(3);

	}

	if(!timed) {
		return {};

	}

	struct counts counts;

	if(ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP) || read(fd, &counts, sizeof(counts)) != sizeof(counts)) {
		std::perror("read counter group");
		std::exit(4);

	}

	if(counts.nr != 3 || !counts.cycles || counts.running - running < 0.999 * (counts.enabled - enabled)) {
		std::fprintf(stderr, "Invalid or multiplexed counters\n");
		std::exit(5);

	}

	enabled = counts.enabled;
	running = counts.running;
	double total = double(encoded) * passes;
	double elapsed = std::chrono::duration<double, std::nano>(end - start).count();

	return { total / elapsed, counts.cycles / total, counts.dram * 64.0 / total, counts.l2 * 64.0 / total };

}

struct implementation {
	const char *name;
	struct sample (*measure)(struct fixture &, uint32_t, bool);
};

static constexpr struct implementation implementations[] = {
	{ "dispatch", measure<&ffbase64_decode_mime76> },
	{ "separate-forced", measure<&ffbase64_forced> },
	{ "shared-dispatch", measure<&ffbase64_shared_dispatch> },
	{ "shared-forced", measure<&ffbase64_shared_force> },
};

// [=]===^=[ generate ]=======================================================[=]
static void generate(struct fixture &data) {
	static char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	uint64_t state = 0x123456789abcdef;
	size_t cursor = 0;
	uint32_t column = 0;

	for(size_t i = 0; i < data.decoded; i += sizeof(state)) {
		state ^= state >> 12;
		state ^= state << 25;
		state ^= state >> 27;
		uint64_t word = state * UINT64_C(0x2545f4914f6cdd1d);
		std::memcpy(data.expected + i, &word, sizeof(word));

	}

	for(size_t i = 0; i < data.decoded; i += 3) {
		size_t rem = data.decoded - i;
		uint32_t word = uint32_t(data.expected[i]) << 16;

		if(rem > 1) {
			word |= uint32_t(data.expected[i + 1]) << 8;

		}

		if(rem > 2) {
			word |= data.expected[i + 2];

		}

		data.input[cursor++] = alphabet[word >> 18];
		data.input[cursor++] = alphabet[(word >> 12) & 63];
		data.input[cursor++] = rem > 1 ? alphabet[(word >> 6) & 63] : '=';
		data.input[cursor++] = rem > 2 ? alphabet[word & 63] : '=';
		column += 4;

		if(column == 76 || rem <= 3) {
			data.input[cursor++] = '\r';
			data.input[cursor++] = '\n';
			column = 0;

		}
	}

	if(cursor != data.encoded) {
		std::exit(6);

	}
}

// [=]===^=[ main ]===========================================================[=]
int main(int argc, char **argv) {
	uint32_t batches = argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 7;
	uint32_t offset = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 0;
	size_t decoded = size_t(256) << 20;
	size_t alphabet = (decoded + 2) / 3 * 4;
	size_t encoded = alphabet + (alphabet + 75) / 76 * 2;
	void *in = nullptr, *out = nullptr, *raw = nullptr;
	std::array<uint8_t, 64> guard;

	if(!batches || offset >= 64 || posix_memalign(&in, 4096, encoded + 64) || posix_memalign(&out, 4096, decoded + 128) || posix_memalign(&raw, 4096, decoded)) {
		return 1;

	}

	struct fixture data = { static_cast<uint8_t *>(in) + (offset * 3 & 63), static_cast<uint8_t *>(out) + offset, static_cast<uint8_t *>(raw), encoded, decoded };

	counters[0] = open_counter(PERF_TYPE_HARDWARE, PERF_COUNT_HW_CPU_CYCLES, -1);
	counters[1] = open_counter(PERF_TYPE_RAW, UINT64_C(0x100004865), counters[0]);
	counters[2] = open_counter(PERF_TYPE_RAW, UINT64_C(0x10000de65), counters[0]);
	guard.fill(0xa5);
	generate(data);
	std::puts("batch,mode,encoded_GB_s,cycles_per_encoded_byte,DRAM_bytes_per_encoded_byte,L2_fill_bytes_per_encoded_byte");

	for(uint32_t i = 0; i < batches; ++i) {
		for(uint32_t j = 0; j < std::size(implementations); ++j) {
			uint32_t index = (i + j) % std::size(implementations);
			auto &impl = implementations[index];

			std::memset(data.output, 0xa5, decoded + guard.size());
			impl.measure(data, 2, false);
			auto result = impl.measure(data, 3, true);

			if(std::memcmp(data.output, data.expected, decoded) || std::memcmp(data.output + decoded, guard.data(), guard.size())) {
				std::fprintf(stderr, "%s output or guard mismatch\n", impl.name);
				return 7;

			}

			std::printf("%u,%s,%.6f,%.7f,%.6f,%.6f\n", i, impl.name, result.speed, result.cycles, result.dram, result.l2);

		}
	}

	for(int fd : counters) {
		close(fd);

	}

	std::free(raw);
	std::free(out);
	std::free(in);
	return 0;

}
