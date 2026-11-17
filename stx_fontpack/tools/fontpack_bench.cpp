// stx_fontpack: timings of the shipped loader, for GUI.Forms' factory bundle
// page (docs/user/factory-bundle.md). tools/bench_loader.py drives it.
//
//   stx_fontpack_bench rounds <in.fpk> [--rounds N] [--threads N]
//       decodes every group N times (default 25) with pack_load_group, then
//       the eager groups together (set_open) and every group in turn
//       (set_open, then set_need_group for each lazy group), N times each,
//       and prints one line per case: median, fastest and slowest in
//       milliseconds, and the largest scratch peak as a share of the
//       group's font bytes
//   stx_fontpack_bench fresh <in.fpk> [--threads N]
//       one set_open, timed: what an application pays for the eager groups
//       in a new process
//
// --threads 1 (the default) runs on serial_executor; any other count runs
// on the tools' std::thread executor (0: every hardware thread). That
// executor starts its threads for each fork-join step, so its timings
// include thread start; a program that lends a running pool pays less.
//
// What is timed: pack bytes in memory to font bytes in memory, with every
// font's CRC-32 checked, and the storage given back after each round
// (group_release, set_release; not timed). Reading the pack file is not
// timed. Every printed line starts with a fixed keyword so the driver can
// parse it. Exit status 0 on success, 1 when a group is refused or the pack
// cannot be read, 2 on bad usage.
#include "stx_fontpack/loader.hpp"
#include "thread_executor.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

typedef std::vector<std::uint8_t> Bytes;
typedef std::chrono::steady_clock Clock;

bool read_file(const char* path, Bytes& bytes) {
    std::FILE* file = std::fopen(path, "rb");
    if (file == nullptr) {
        return false;
    }
    bool ok = std::fseek(file, 0, SEEK_END) == 0;
    const long size = ok ? std::ftell(file) : -1;
    ok = ok && size >= 0 && std::fseek(file, 0, SEEK_SET) == 0;
    if (ok) {
        bytes.resize(static_cast<std::size_t>(size));
        ok = std::fread(bytes.data(), 1, bytes.size(), file) == bytes.size();
    }
    std::fclose(file);
    return ok;
}

double milliseconds_between(Clock::time_point start, Clock::time_point end) {
    const std::chrono::duration<double, std::milli> elapsed = end - start;
    const double milliseconds = elapsed.count();
    return milliseconds;
}

// The timings of one case, in milliseconds; sorted by summarize.
struct Sample {
    std::vector<double> milliseconds;
    std::size_t scratch_peak = 0;
    std::uint64_t font_bytes = 0;
};

void summarize(const char* name, Sample& sample) {
    std::sort(sample.milliseconds.begin(), sample.milliseconds.end());
    const std::size_t count = sample.milliseconds.size();
    // The upper median for an even count, as the experiment's evaluate.sh took it.
    const double median = sample.milliseconds[count / 2];
    const double fastest = sample.milliseconds[0];
    const double slowest = sample.milliseconds[count - 1];
    double scratch_share = 0.0;
    if (sample.font_bytes != 0) {
        scratch_share = 100.0 * static_cast<double>(sample.scratch_peak) / static_cast<double>(sample.font_bytes);
    }
    std::printf("case %-12s rounds %zu median %.2f ms fastest %.2f ms slowest %.2f ms scratch %.1f%%\n", name, count,
                median, fastest, slowest, scratch_share);
}

stx_fontpack::Executor executor_for(std::uint32_t threads) {
    if (threads == 1) {
        const stx_fontpack::Executor serial = stx_fontpack::serial_executor();
        return serial;
    }
    const stx_fontpack::Executor workers = stx_fontpack_tools::thread_executor(threads);
    return workers;
}

void print_setup(const char* pack_path, const Bytes& pack, const stx_fontpack::Executor& executor) {
#if defined(__clang__)
    std::printf("compiler clang %s\n", __clang_version__);
#elif defined(__GNUC__)
    std::printf("compiler gcc %s\n", __VERSION__);
#elif defined(_MSC_VER)
    std::printf("compiler msvc %d\n", _MSC_VER);
#endif
#if defined(NDEBUG)
    std::printf("assertions off (NDEBUG)\n");
#else
    std::printf("assertions on (no NDEBUG): not a release build\n");
#endif
    std::printf("pack %s %zu bytes\n", pack_path, pack.size());
    std::printf("workers %u\n", executor.worker_count);
}

std::string group_name(const stx_fontpack::PackGroup& group) {
    const std::string name(group.name, group.name_length);
    return name;
}

int command_rounds(const char* pack_path, std::uint32_t rounds, std::uint32_t threads) {
    Bytes pack;
    if (!read_file(pack_path, pack)) {
        std::printf("error cannot read %s\n", pack_path);
        return 1;
    }
    const stx_fontpack::Allocator allocator = stx_fontpack::standard_allocator();
    const stx_fontpack::Executor executor = executor_for(threads);
    print_setup(pack_path, pack, executor);
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(pack.data()), pack.size());
    // PackIndex and LoadedSet hold kMaxFonts records: on the heap, not the stack.
    std::unique_ptr<stx_fontpack::PackIndex> index(new stx_fontpack::PackIndex);
    if (!stx_fontpack::pack_open(bytes, *index)) {
        std::printf("error %s does not open as a pack\n", pack_path);
        return 1;
    }

    std::uint64_t eager_bytes = 0;
    std::uint64_t all_bytes = 0;
    for (std::uint32_t g = 0; g < (*index).group_count; g += 1) {
        const stx_fontpack::PackGroup& group = (*index).groups[g];
        all_bytes += group.font_bytes;
        if (group.eager) {
            eager_bytes += group.font_bytes;
        }
        std::printf("group %s %s fonts %u font_bytes %llu body_bytes %zu\n", group_name(group).c_str(),
                    group.eager ? "eager" : "lazy", group.font_count,
                    static_cast<unsigned long long>(group.font_bytes), group.body_size);
    }

    for (std::uint32_t g = 0; g < (*index).group_count; g += 1) {
        Sample sample;
        sample.milliseconds.reserve(rounds);
        sample.font_bytes = (*index).groups[g].font_bytes;
        for (std::uint32_t round = 0; round < rounds; round += 1) {
            stx_fontpack::LoadedGroup loaded{};
            const Clock::time_point start = Clock::now();
            const bool ok = stx_fontpack::pack_load_group(*index, g, allocator, executor, loaded);
            const Clock::time_point end = Clock::now();
            if (!ok) {
                std::printf("error group %s refused: %s\n", group_name((*index).groups[g]).c_str(),
                            stx_fontpack::refusal_name(loaded.refusal));
                return 1;
            }
            sample.milliseconds.push_back(milliseconds_between(start, end));
            sample.scratch_peak = std::max(sample.scratch_peak, loaded.scratch_peak);
            stx_fontpack::group_release(loaded);
        }
        const std::string name = group_name((*index).groups[g]);
        summarize(name.c_str(), sample);
    }

    std::unique_ptr<stx_fontpack::LoadedSet> set(new stx_fontpack::LoadedSet);
    Sample eager;
    eager.milliseconds.reserve(rounds);
    eager.font_bytes = eager_bytes;
    Sample all;
    all.milliseconds.reserve(rounds);
    all.font_bytes = all_bytes;
    for (std::uint32_t round = 0; round < rounds; round += 1) {
        const Clock::time_point start = Clock::now();
        bool ok = stx_fontpack::set_open(bytes, allocator, executor, *set);
        const Clock::time_point opened = Clock::now();
        for (std::uint32_t g = 0; ok && g < (*set).index.group_count; g += 1) {
            ok = stx_fontpack::set_need_group(*set, g);
        }
        const Clock::time_point end = Clock::now();
        if (!ok) {
            std::printf("error the set refused a group\n");
            stx_fontpack::set_release(*set);
            return 1;
        }
        eager.milliseconds.push_back(milliseconds_between(start, opened));
        all.milliseconds.push_back(milliseconds_between(start, end));
        stx_fontpack::set_release(*set);
    }
    // Scratch is reported per group above; the set cases report none.
    summarize("eager-groups", eager);
    summarize("all-in-turn", all);
    return 0;
}

int command_fresh(const char* pack_path, std::uint32_t threads) {
    Bytes pack;
    if (!read_file(pack_path, pack)) {
        std::printf("error cannot read %s\n", pack_path);
        return 1;
    }
    const stx_fontpack::Allocator allocator = stx_fontpack::standard_allocator();
    const stx_fontpack::Executor executor = executor_for(threads);
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(pack.data()), pack.size());
    std::unique_ptr<stx_fontpack::LoadedSet> set(new stx_fontpack::LoadedSet);
    const Clock::time_point start = Clock::now();
    const bool ok = stx_fontpack::set_open(bytes, allocator, executor, *set);
    const Clock::time_point end = Clock::now();
    if (!ok) {
        std::printf("error set_open refused the pack\n");
        return 1;
    }
    std::printf("fresh eager-groups workers %u %.2f ms\n", executor.worker_count, milliseconds_between(start, end));
    stx_fontpack::set_release(*set);
    return 0;
}

// Reads "--rounds N" and "--threads N" from argv[first...]; false on anything else.
bool parse_options(int argc, char** argv, int first, std::uint32_t& rounds, std::uint32_t& threads) {
    for (int k = first; k < argc; k += 2) {
        if (k + 1 >= argc) {
            return false;
        }
        char* end = nullptr;
        const unsigned long value = std::strtoul(argv[k + 1], &end, 10);
        if (end == argv[k + 1] || *end != 0 || value > 1000000UL) {
            return false;
        }
        if (std::strcmp(argv[k], "--rounds") == 0 && value > 0) {
            rounds = static_cast<std::uint32_t>(value);
        } else if (std::strcmp(argv[k], "--threads") == 0) {
            threads = static_cast<std::uint32_t>(value);
        } else {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    std::uint32_t rounds = 25;
    std::uint32_t threads = 1;
    if (argc >= 3 && std::strcmp(argv[1], "rounds") == 0 && parse_options(argc, argv, 3, rounds, threads)) {
        return command_rounds(argv[2], rounds, threads);
    }
    if (argc >= 3 && std::strcmp(argv[1], "fresh") == 0 && parse_options(argc, argv, 3, rounds, threads)) {
        return command_fresh(argv[2], threads);
    }
    std::printf("usage: stx_fontpack_bench rounds <in.fpk> [--rounds N] [--threads N]\n"
                "       stx_fontpack_bench fresh <in.fpk> [--threads N]\n");
    return 2;
}
