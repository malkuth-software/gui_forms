#pragma once
// The driver of a fuzz target built without libFuzzer
// (GUI_FORMS_FUZZ_STANDALONE): the ctest program that runs the target's seed
// corpus and a fixed number of mutations of it, deterministically, so that
// the suite runs every target without the libFuzzer runtime (audit N115).
//
// Order: every file named on the command line, and every regular file of
// each directory named there in path order, each run once as it is; then
// the target's built-in seeds, each once; then `mutated_runs` inputs, each
// one of those seeds in turn with one to eight bits flipped and, every fifth
// run, cut short, or every seventh or eleventh, grown in the middle by up to
// 511 copies of a fill byte. A target reports a broken invariant itself (it
// aborts); the driver returns 1 when a path named on the command line cannot
// be read or nothing is left to run, and 0 otherwise.
//
// One header for the fuzz sources, each a single translation unit, so its
// functions are inline.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size);

namespace gui_forms::fuzz {

struct StandaloneRun final {
    // The target's name in the summary line.
    std::string_view name{};
    // The target's own seeds, run after the corpus files. Borrowed for the
    // call; a seed may hold any byte, NUL included.
    std::span<const std::string_view> seeds{};
    std::size_t mutated_runs{};
    // The xorshift64 state the mutations start from; must not be zero.
    std::uint64_t random_state{0x6a09e667f3bcc908ULL};
    // The bytes the seventh-run and eleventh-run growths repeat.
    char first_fill{'9'};
    char second_fill{'"'};
};

// What one run of the driver read and ran.
struct StandaloneCounts final {
    std::size_t corpus_files{};
    std::size_t seeds{};
    std::size_t mutated_runs{};
};

// One xorshift64 step: advances `state` and returns it.
inline std::uint64_t next_random(std::uint64_t& state) noexcept {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
}

// Appends the bytes of the file at `path` to `inputs` as one input. False
// when the file cannot be read.
inline bool read_corpus_file(const std::filesystem::path& path, std::vector<std::string>& inputs) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return false;
    const std::istreambuf_iterator<char> first(stream);
    const std::istreambuf_iterator<char> end{};
    std::string bytes(first, end);
    if (stream.bad()) return false;
    inputs.push_back(std::move(bytes));
    return true;
}

// Appends the input at `path`: the file itself, or each regular file of the
// directory in path order. False when the path or a file in it cannot be read.
inline bool read_corpus_path(const std::filesystem::path& path, std::vector<std::string>& inputs) {
    std::error_code error{};
    if (!std::filesystem::is_directory(path, error)) {
        const bool read = read_corpus_file(path, inputs);
        return read;
    }
    std::vector<std::filesystem::path> files{};
    std::filesystem::directory_iterator entry(path, error);
    const std::filesystem::directory_iterator end{};
    if (error) return false;
    for (; entry != end; entry.increment(error)) {
        if (error) return false;
        if ((*entry).is_regular_file(error)) files.push_back((*entry).path());
    }
    std::sort(files.begin(), files.end());
    for (const std::filesystem::path& file : files) {
        if (!read_corpus_file(file, inputs)) return false;
    }
    return true;
}

// Runs one input through the target. The input's bytes are copied into
// storage of exactly its size, so that a sanitizer sees a read past its end.
inline void run_input(std::string_view input) {
    const std::vector<std::uint8_t> bytes(input.begin(), input.end());
    static_cast<void>(LLVMFuzzerTestOneInput(bytes.data(), bytes.size()));
}

// The driver (see the file comment). `arguments` are main's; `counts`
// receives what was run.
inline int run_standalone(int argument_count, char** arguments, const StandaloneRun& run,
                          StandaloneCounts& counts) {
    counts = StandaloneCounts{};
    std::vector<std::string> inputs{};
    for (int index = 1; index < argument_count; ++index) {
        const std::filesystem::path path(arguments[index]);
        if (!read_corpus_path(path, inputs)) {
            std::fprintf(stderr, "%.*s fuzz: cannot read %s\n", static_cast<int>(run.name.size()), run.name.data(),
                         arguments[index]);
            return 1;
        }
    }
    counts.corpus_files = inputs.size();
    for (const std::string_view seed : run.seeds) inputs.emplace_back(seed);
    counts.seeds = run.seeds.size();
    if (inputs.empty()) {
        std::fprintf(stderr, "%.*s fuzz: no input to run\n", static_cast<int>(run.name.size()), run.name.data());
        return 1;
    }
    for (const std::string& input : inputs) run_input(input);

    std::uint64_t state = run.random_state;
    std::string input{};
    for (std::size_t index = 0U; index < run.mutated_runs; ++index) {
        const std::uint64_t run_draw = next_random(state);
        input = inputs[index % inputs.size()];
        const std::size_t mutations = 1U + static_cast<std::size_t>(run_draw % 8U);
        // Each mutation flips one bit of one byte.
        for (std::size_t mutation = 0U; mutation < mutations; ++mutation) {
            const std::uint64_t draw = next_random(state);
            if (input.empty()) continue;
            const std::size_t offset = static_cast<std::size_t>(draw % input.size());
            const char flipped_bit = static_cast<char>(1U << (draw % 8U));
            input[offset] = static_cast<char>(input[offset] ^ flipped_bit);
        }
        if (index % 5U == 0U && !input.empty()) {
            input.resize(static_cast<std::size_t>(state % input.size()));
        } else if (index % 7U == 0U) {
            input.insert(input.size() / 2U, static_cast<std::size_t>(state % 512U), run.first_fill);
        } else if (index % 11U == 0U) {
            input.insert(input.size() / 2U, static_cast<std::size_t>(state % 512U), run.second_fill);
        }
        run_input(input);
    }
    counts.mutated_runs = run.mutated_runs;
    std::printf("%.*s fuzz: %zu corpus files, %zu seeds, %zu mutated runs\n", static_cast<int>(run.name.size()),
                run.name.data(), counts.corpus_files, counts.seeds, counts.mutated_runs);
    return 0;
}

} // namespace gui_forms::fuzz
