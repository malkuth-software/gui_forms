// Deterministic malformed-input sweeps over stx_zlib inflate (owner revision
// of decision 6A, 2026-10-20: systematic sweeps in the normal suite, run under
// the sanitizers, instead of sustained fuzz campaigns).
//
// Seeds are produced by the owned encoder across formats, levels and
// strategies. For each seed:
//   - every truncation point is decoded with final_input set: a zlib prefix
//     must never reach stream_end, and no decode may exceed its limits;
//   - every single-bit flip of the header and table region (the whole stream
//     when it is at most 512 bytes, else its first 384 and last 8 bytes) is
//     decoded: it must end in stream_end or a named error within its limits,
//     and an accepted zlib stream must still satisfy its Adler-32 trailer;
//   - the seed decodes identically with one-byte input and output schedules.
// The sanitizers turn any out-of-bounds access or undefined behaviour into
// a failure.
#include "stx_zlib/deflate.hpp"
#include "stx_zlib/inflate.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
using Bytes = std::vector<std::byte>;
using namespace stx_zlib;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Seed final {
    const char* name = "";
    DeflateFormat format = DeflateFormat::zlib;
    DeflateStrategy strategy = DeflateStrategy::normal;
    int level = 6;
    Bytes plain{};
    Bytes encoded{};
};

Bytes encode(const Bytes& plain, DeflateFormat format, DeflateStrategy strategy, int level,
             DeflateStream& stream) {
    DeflateOptions options{};
    options.format = format;
    options.strategy = strategy;
    options.level = level;
    options.input_limit.value = plain.size();
    options.output_limit.value = plain.size() * 2U + 1024U;
    options.work_limit.value = 1ULL << 40U;
    require(stream.reset(options) == DeflateError::none, "deflate reset");
    Bytes output(static_cast<std::size_t>(options.output_limit.value));
    std::size_t consumed = 0;
    std::size_t produced = 0;
    for (;;) {
        const std::span<const std::byte> input(plain.data() + consumed, plain.size() - consumed);
        const std::span<std::byte> space(output.data() + produced, output.size() - produced);
        const DeflateResult step = stream.process(input, space, DeflateFlush::finish);
        require(step.status != DeflateStatus::error, "deflate failed");
        consumed += step.input_consumed;
        produced += step.output_produced;
        if (step.status == DeflateStatus::stream_end) break;
    }
    output.resize(produced);
    return output;
}

// Reusable decode storage: one stream, one output buffer sized for the
// largest seed plus slack, so a corrupted stream that expands is caught by
// the output limit rather than by the buffer.
struct Decoder final {
    std::unique_ptr<InflateStream> stream = std::make_unique<InflateStream>();
    Bytes output = Bytes(1U << 17U);
};

struct Outcome final {
    InflateResult result{};
    std::size_t produced = 0;
};

Outcome decode_whole(Decoder& decoder, std::span<const std::byte> input, InflateFormat format,
                     std::uint64_t output_limit) {
    InflateOptions options{};
    options.format = format;
    options.limits.input_bytes.value = input.size() + 1U;
    options.limits.output_bytes.value = output_limit;
    options.limits.work.value = 1ULL << 32U;
    require((*decoder.stream).reset(options) == InflateError::none, "inflate reset");
    Outcome outcome{};
    // The output span covers the whole buffer; the stream's limit is smaller.
    outcome.result = (*decoder.stream).process(input, decoder.output, true);
    outcome.produced = outcome.result.output_produced;
    // Bounded drain: a need_output with room left cannot recur indefinitely.
    for (unsigned round = 0; round < 4U && outcome.result.status == InflateStatus::need_output; ++round) {
        const std::span<const std::byte> rest = input.subspan(static_cast<std::size_t>(outcome.result.total_input.value));
        const std::span<std::byte> space(decoder.output.data() + outcome.produced,
                                         decoder.output.size() - outcome.produced);
        outcome.result = (*decoder.stream).process(rest, space, true);
        outcome.produced += outcome.result.output_produced;
    }
    return outcome;
}

void check_bounded(const Outcome& outcome, std::uint64_t output_limit, const char* message) {
    const InflateStatus status = outcome.result.status;
    require(status == InflateStatus::stream_end || status == InflateStatus::error ||
            status == InflateStatus::need_input || status == InflateStatus::need_output, message);
    require(outcome.result.total_output.value <= output_limit, message);
    require(outcome.produced == outcome.result.total_output.value, message);
    if (status == InflateStatus::error) require(outcome.result.error != InflateError::none, message);
}

std::uint32_t adler32(std::span<const std::byte> bytes) {
    std::uint32_t low = 1;
    std::uint32_t high = 0;
    for (const std::byte item : bytes) {
        low = (low + std::to_integer<std::uint32_t>(item)) % 65521U;
        high = (high + low) % 65521U;
    }
    const std::uint32_t result = (high << 16U) | low;
    return result;
}

std::uint32_t trailer(std::span<const std::byte> stream, std::size_t at) {
    const std::uint32_t value = (std::to_integer<std::uint32_t>(stream[at]) << 24U) |
        (std::to_integer<std::uint32_t>(stream[at + 1U]) << 16U) |
        (std::to_integer<std::uint32_t>(stream[at + 2U]) << 8U) |
        std::to_integer<std::uint32_t>(stream[at + 3U]);
    return value;
}

std::vector<Seed> make_seeds() {
    std::unique_ptr<DeflateStream> stream = std::make_unique<DeflateStream>();
    std::vector<Seed> seeds{};
    Bytes text{};
    const char* line = "GUI.Forms sweeps every truncation and bit flip; ";
    for (unsigned repeat = 0; repeat < 14; ++repeat) {
        for (const char* cursor = line; *cursor != '\0'; ++cursor) text.push_back(static_cast<std::byte>(*cursor));
    }
    Bytes noise(300);
    std::uint32_t state = 0x20261021U;
    for (std::byte& item : noise) {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        item = static_cast<std::byte>(state & 255U);
    }
    const Bytes zeros(2000, std::byte{0});
    const Bytes empty{};
    seeds.push_back({"text zlib level 6", DeflateFormat::zlib, DeflateStrategy::normal, 6, text});
    seeds.push_back({"text raw level 9", DeflateFormat::raw_deflate, DeflateStrategy::normal, 9, text});
    seeds.push_back({"noise zlib level 1", DeflateFormat::zlib, DeflateStrategy::normal, 1, noise});
    seeds.push_back({"noise zlib stored", DeflateFormat::zlib, DeflateStrategy::normal, 0, noise});
    seeds.push_back({"text zlib fixed", DeflateFormat::zlib, DeflateStrategy::fixed, 6, text});
    seeds.push_back({"text raw huffman-only", DeflateFormat::raw_deflate, DeflateStrategy::huffman_only, 6, text});
    seeds.push_back({"zeros zlib rle", DeflateFormat::zlib, DeflateStrategy::rle, 6, zeros});
    seeds.push_back({"empty zlib", DeflateFormat::zlib, DeflateStrategy::normal, 6, empty});
    for (Seed& seed : seeds) seed.encoded = encode(seed.plain, seed.format, seed.strategy, seed.level, *stream);
    return seeds;
}

InflateFormat inflate_format(DeflateFormat format) {
    const InflateFormat result = format == DeflateFormat::zlib ? InflateFormat::zlib : InflateFormat::raw_deflate;
    return result;
}

void sweep(const Seed& seed, Decoder& decoder, std::uint64_t& decodes) {
    const InflateFormat format = inflate_format(seed.format);
    const std::uint64_t limit = std::max<std::uint64_t>(seed.plain.size() * 4U, 4096U);
    const bool zlib = seed.format == DeflateFormat::zlib;

    // The seed itself, whole and with one-byte schedules.
    const Outcome whole = decode_whole(decoder, seed.encoded, format, limit);
    ++decodes;
    require(whole.result.status == InflateStatus::stream_end, seed.name);
    require(whole.produced == seed.plain.size() &&
            std::equal(seed.plain.begin(), seed.plain.end(), decoder.output.begin()), seed.name);
    {
        InflateOptions options{};
        options.format = format;
        options.limits.input_bytes.value = seed.encoded.size();
        options.limits.output_bytes.value = limit;
        options.limits.work.value = 1ULL << 32U;
        require((*decoder.stream).reset(options) == InflateError::none, "inflate reset");
        Bytes output{};
        std::size_t consumed = 0;
        std::array<std::byte, 1> cell{};
        InflateResult step{};
        // Each call moves at most one byte each way; the stream cannot stall
        // for more calls than input plus output plus a trailer allowance.
        const std::size_t bound = (seed.encoded.size() + seed.plain.size()) * 4U + 64U;
        for (std::size_t call = 0; call < bound; ++call) {
            const std::size_t available = std::min<std::size_t>(1U, seed.encoded.size() - consumed);
            const std::span<const std::byte> input(seed.encoded.data() + consumed, available);
            const bool final_input = consumed + available == seed.encoded.size();
            step = (*decoder.stream).process(input, cell, final_input);
            consumed += step.input_consumed;
            if (step.output_produced == 1U) output.push_back(cell[0]);
            if (step.status == InflateStatus::stream_end || step.status == InflateStatus::error) break;
        }
        require(step.status == InflateStatus::stream_end && output == seed.plain, "one-byte schedule differs");
    }

    // Every truncation point, final input set.
    for (std::size_t length = 0; length < seed.encoded.size(); ++length) {
        const std::span<const std::byte> prefix(seed.encoded.data(), length);
        const Outcome outcome = decode_whole(decoder, prefix, format, limit);
        ++decodes;
        check_bounded(outcome, limit, "truncated stream escaped its limits");
        // No format may end on a strict prefix: a zlib stream lacks part of
        // its trailer, and an encoder's raw stream lacks at least one bit of
        // its final block (the last byte holds the end-of-block code or data
        // before it), so stream_end there means the decoder read past its
        // input (B-gf-tests-02-F2).
        require(outcome.result.status != InflateStatus::stream_end, "truncated stream accepted");
    }

    // Every single-bit flip of the header and table region.
    Bytes flipped = seed.encoded;
    std::vector<std::size_t> positions{};
    if (seed.encoded.size() <= 512U) {
        for (std::size_t at = 0; at < seed.encoded.size(); ++at) positions.push_back(at);
    } else {
        for (std::size_t at = 0; at < 384U; ++at) positions.push_back(at);
        for (std::size_t at = seed.encoded.size() - 8U; at < seed.encoded.size(); ++at) positions.push_back(at);
    }
    for (const std::size_t at : positions) {
        for (unsigned bit = 0; bit < 8U; ++bit) {
            const std::byte mask = static_cast<std::byte>(1U << bit);
            flipped[at] ^= mask;
            const Outcome outcome = decode_whole(decoder, flipped, format, limit);
            ++decodes;
            check_bounded(outcome, limit, "bit flip escaped its limits");
            if (zlib && outcome.result.status == InflateStatus::stream_end) {
                // An accepted zlib stream still satisfies its own trailer.
                const std::size_t end = static_cast<std::size_t>(outcome.result.total_input.value);
                require(end >= 6U && trailer(flipped, end - 4U) ==
                        adler32(std::span<const std::byte>(decoder.output.data(), outcome.produced)),
                        "accepted flipped stream fails its Adler-32");
            }
            flipped[at] ^= mask;
        }
    }
}

} // namespace

int main() {
    try {
        const std::vector<Seed> seeds = make_seeds();
        Decoder decoder{};
        std::uint64_t decodes = 0;
        for (const Seed& seed : seeds) sweep(seed, decoder, decodes);
        std::printf("malformed_sweep: %zu seeds, %llu decodes\n", seeds.size(),
                    static_cast<unsigned long long>(decodes));
    } catch (const std::exception& error) {
        std::cerr << "malformed_sweep_tests: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
