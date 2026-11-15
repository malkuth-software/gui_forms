// stx_fontpack tests: the round trip, claims, damage and per-group decode.
//
//   stx_fontpack_tests [<font directory>]
//
// Without an argument the inputs are synthetic: incompressible and repetitive
// blobs (stored verbatim) and small sfnt files of raw tables. With a
// directory, every .ttf and .otf file in it joins the inputs too (GUI.Forms
// passes assets/fonts), which exercises the glyf, CFF and hmtx transforms.
//
// Every allocation goes through a checking allocator that records each
// block's size and refuses a release of the wrong size, so the tests also
// prove that a load (loaded or refused) gives back everything it took.
#include "stx_fontpack/loader.hpp"
#include "stx_fontpack/packer.hpp"
#include "detail/codec.hpp"
#include "detail/crc_combine.hpp"
#include "detail/format.hpp"
#include "thread_executor.hpp"

#include <stx_zlib/checksum.hpp>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

typedef std::vector<std::uint8_t> Bytes;

int failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        failures += 1;
        std::printf("FAIL: %s\n", what);
    }
}

// ---- checking allocator --------------------------------------------------------

// Each block carries its size in a header of one max_align_t, so release can
// check the size it is given. fail_from: the allocation with this ordinal and
// every later one return null.
struct CheckingAllocator {
    std::atomic<std::size_t> outstanding_bytes;
    std::atomic<std::size_t> outstanding_blocks;
    std::atomic<std::size_t> calls;
    std::atomic<std::size_t> wrong_sizes;
    std::size_t fail_from;
};

enum : std::size_t { kHeaderBytes = alignof(std::max_align_t) };

void* checking_allocate(void* context, std::size_t size) {
    CheckingAllocator& state = *static_cast<CheckingAllocator*>(context);
    const std::size_t ordinal = state.calls.fetch_add(1);
    if (ordinal >= state.fail_from || size == 0 || size > std::numeric_limits<std::size_t>::max() - kHeaderBytes) {
        return nullptr;
    }
    unsigned char* block = static_cast<unsigned char*>(std::malloc(size + kHeaderBytes));
    if (block == nullptr) {
        return nullptr;
    }
    std::memcpy(block, &size, sizeof(size));
    state.outstanding_bytes.fetch_add(size);
    state.outstanding_blocks.fetch_add(1);
    return block + kHeaderBytes;
}

void checking_release(void* context, void* memory, std::size_t size) {
    CheckingAllocator& state = *static_cast<CheckingAllocator*>(context);
    unsigned char* block = static_cast<unsigned char*>(memory) - kHeaderBytes;
    std::size_t recorded = 0;
    std::memcpy(&recorded, block, sizeof(recorded));
    if (recorded != size) {
        state.wrong_sizes.fetch_add(1);
    }
    state.outstanding_bytes.fetch_sub(recorded);
    state.outstanding_blocks.fetch_sub(1);
    std::free(block);
}

void checking_reset(CheckingAllocator& state, std::size_t fail_from) {
    state.outstanding_bytes.store(0);
    state.outstanding_blocks.store(0);
    state.calls.store(0);
    state.wrong_sizes.store(0);
    state.fail_from = fail_from;
}

stx_fontpack::Allocator checking_interface(CheckingAllocator& state) {
    stx_fontpack::Allocator allocator;
    allocator.allocate = checking_allocate;
    allocator.release = checking_release;
    allocator.context = &state;
    return allocator;
}

// ---- executors -------------------------------------------------------------------

// Runs the workers in reverse order on the calling thread: a load must not
// depend on which worker takes which job.
void reverse_run_workers(void* context, const stx_fontpack::WorkerTask& task) {
    static_cast<void>(context);
    for (std::uint32_t worker = task.worker_count; worker != 0; worker -= 1) {
        task.run(task.context, worker - 1);
    }
}

stx_fontpack::Executor reverse_executor(std::uint32_t worker_count) {
    stx_fontpack::Executor executor;
    executor.run_workers = reverse_run_workers;
    executor.context = nullptr;
    executor.worker_count = worker_count;
    return executor;
}

// ---- inputs ----------------------------------------------------------------------

std::span<const std::byte> as_bytes(const Bytes& bytes) {
    return std::span<const std::byte>(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
}

std::span<const std::byte> as_bytes(Bytes&& bytes) = delete;

std::uint64_t random_next(std::uint64_t& state) {
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return state;
}

stx_fontpack::PackInput blob_input(const char* name, std::size_t size, std::uint64_t seed, bool repetitive) {
    stx_fontpack::PackInput input;
    input.name = name;
    input.data.resize(size);
    std::uint64_t state = seed;
    for (std::size_t k = 0; k < size; k += 1) {
        if (repetitive && k >= 64 && (random_next(state) & 3u) != 0) {
            input.data[k] = input.data[k - 64];
        } else {
            input.data[k] = static_cast<std::uint8_t>(random_next(state) >> 24);
        }
    }
    return input;
}

void put_be16(Bytes& out, std::size_t at, std::uint32_t value) {
    out[at] = static_cast<std::uint8_t>(value >> 8);
    out[at + 1] = static_cast<std::uint8_t>(value);
}

void put_be32(Bytes& out, std::size_t at, std::uint32_t value) {
    put_be16(out, at, value >> 16);
    put_be16(out, at + 2, value & 0xFFFFu);
}

// An sfnt of table_count raw tables with 4-byte aligned, zero-padded bodies.
stx_fontpack::PackInput sfnt_input(const char* name, std::uint32_t table_count, std::uint64_t seed) {
    static const std::uint32_t tags[4] = {0x636D6170u, 0x6E616D65u, 0x706F7374u, 0x4F532F32u};   // cmap name post OS/2
    stx_fontpack::PackInput input;
    input.name = name;
    const std::size_t directory = 12 + static_cast<std::size_t>(table_count) * 16;
    input.data.assign(directory, 0);
    put_be32(input.data, 0, 0x00010000u);
    put_be16(input.data, 4, table_count);
    std::uint64_t state = seed;
    for (std::uint32_t t = 0; t < table_count; t += 1) {
        const std::size_t length = 200 + static_cast<std::size_t>(random_next(state) % 1500);
        const std::size_t offset = input.data.size();
        for (std::size_t k = 0; k < length; k += 1) {
            input.data.push_back(static_cast<std::uint8_t>((k * 7 + t) & 0x3Fu));
        }
        while ((input.data.size() & 3u) != 0) {
            input.data.push_back(0);
        }
        const std::size_t record = 12 + static_cast<std::size_t>(t) * 16;
        put_be32(input.data, record, tags[t & 3u]);
        put_be32(input.data, record + 8, static_cast<std::uint32_t>(offset));
        put_be32(input.data, record + 12, static_cast<std::uint32_t>(length));
    }
    return input;
}

// An sfnt whose second table's directory offset lies far past the end of
// the file (A-gf-typography-01-F4: 0x4F54544F is "OTTO", what a one-font
// collection's header shows where a table offset would be). The packer must
// keep it verbatim, and the pack must round-trip it.
stx_fontpack::PackInput sfnt_past_end_input() {
    stx_fontpack::PackInput input = sfnt_input("past-end.ttf", 2, 7);
    put_be32(input.data, 12 + 16 + 8, 0x4F54544Fu);
    return input;
}

bool read_file(const std::filesystem::path& path, Bytes& bytes) {
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
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

// Every .ttf and .otf in directory, in name order (so packs are reproducible).
void directory_inputs(const char* directory, std::vector<stx_fontpack::PackInput>& fonts) {
    std::vector<std::filesystem::path> paths;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory)) {
        const std::string extension = entry.path().extension().string();
        if (entry.is_regular_file() && (extension == ".ttf" || extension == ".otf")) {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end());
    for (std::size_t k = 0; k < paths.size(); k += 1) {
        stx_fontpack::PackInput input;
        input.name = paths[k].filename().string();
        check(read_file(paths[k], input.data), "a font in the directory reads");
        fonts.push_back(input);
    }
}

// ---- helpers ---------------------------------------------------------------------

// Every font of every group of pack loads and equals its input; nothing leaks.
bool round_trip(const Bytes& pack, const std::vector<stx_fontpack::PackInput>& fonts, const stx_fontpack::Executor& executor) {
    CheckingAllocator state;
    checking_reset(state, static_cast<std::size_t>(-1));
    const stx_fontpack::Allocator allocator = checking_interface(state);
    std::unique_ptr<stx_fontpack::PackIndex> index(new stx_fontpack::PackIndex);
    if (!stx_fontpack::pack_open(as_bytes(pack), *index) || (*index).font_count != fonts.size()) {
        return false;
    }
    bool same = true;
    for (std::uint32_t g = 0; g < (*index).group_count && same; g += 1) {
        stx_fontpack::LoadedGroup loaded;
        same = stx_fontpack::pack_load_group(*index, g, allocator, executor, loaded);
        for (std::uint32_t f = 0; f < loaded.font_count && same; f += 1) {
            const stx_fontpack::LoadedFont& font = loaded.fonts[f];
            const std::string name(font.name, font.name_length);
            std::size_t k = 0;
            while (k < fonts.size() && fonts[k].name != name) {
                k += 1;
            }
            same = k < fonts.size() && font.size == fonts[k].data.size() &&
                   std::memcmp(font.data, fonts[k].data.data(), font.size) == 0;
        }
        stx_fontpack::group_release(loaded);
    }
    return same && state.outstanding_blocks.load() == 0 && state.wrong_sizes.load() == 0;
}

std::string group_name(const stx_fontpack::PackGroup& group) {
    return std::string(group.name, group.name_length);
}

// The container as the packer would write it, rebuilt from an opened index,
// so that one claim can be changed and the pack written again.
void container_from_index(const stx_fontpack::PackIndex& index, std::vector<stx_fontpack::ContainerGroup>& groups) {
    groups.resize(index.group_count);
    for (std::uint32_t g = 0; g < index.group_count; g += 1) {
        const stx_fontpack::PackGroup& source = index.groups[g];
        stx_fontpack::ContainerGroup& group = groups[g];
        group.name = group_name(source);
        group.eager = source.eager;
        group.font_bytes = source.font_bytes;
        group.stream_bytes = source.stream_bytes;
        group.body.assign(source.body, source.body + source.body_size);
        group.fonts.resize(source.font_count);
        for (std::uint32_t f = 0; f < source.font_count; f += 1) {
            const stx_fontpack::PackFont& font = index.fonts[source.font_first + f];
            group.fonts[f].name.assign(font.name, font.name_length);
            group.fonts[f].size = font.size;
            group.fonts[f].crc = font.crc;
        }
    }
}

// ---- tests -----------------------------------------------------------------------

void test_crc_combine() {
    static const std::uint8_t text[9] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    const std::span<const std::byte> all(reinterpret_cast<const std::byte*>(text), 9);
    const std::uint32_t whole = stx_zlib::crc32(all);
    check(whole == 0xCBF43926u, "stx_zlib::crc32 check value");
    for (std::size_t split = 0; split <= 9; split += 1) {
        const std::uint32_t a = stx_zlib::crc32(all.first(split));
        const std::uint32_t b = stx_zlib::crc32(all.subspan(split));
        check(stx_fontpack::crc32_combine(a, b, 9 - split) == whole, "crc32_combine joins any split");
    }
    Bytes large(3u << 20);
    std::uint64_t state = 7;
    for (std::size_t k = 0; k < large.size(); k += 1) {
        large[k] = static_cast<std::uint8_t>(random_next(state));
    }
    const std::span<const std::byte> bytes = as_bytes(large);
    const std::size_t piece = std::size_t(1) << 20;
    std::uint32_t joined = stx_zlib::crc32(bytes.first(piece));
    joined = stx_fontpack::crc32_combine(joined, stx_zlib::crc32(bytes.subspan(piece, piece)), piece);
    joined = stx_fontpack::crc32_combine(joined, stx_zlib::crc32(bytes.subspan(2 * piece)), bytes.size() - 2 * piece);
    check(joined == stx_zlib::crc32(bytes), "crc32_combine over 1 MiB pieces");
}

void test_manifest() {
    check(stx_fontpack::pattern_matches("Portsmouth*.ttf", "PortsmouthMono.ttf"), "star matches a run");
    check(stx_fontpack::pattern_matches("Portsmouth*.ttf", "Portsmouth.ttf"), "star matches nothing");
    check(!stx_fontpack::pattern_matches("Portsmouth*.ttf", "Portsmouth.otf"), "suffix must match");
    check(stx_fontpack::pattern_matches("Noto?ans*", "NotoSansCJKjp-Regular.otf"), "question mark matches one");
    check(!stx_fontpack::pattern_matches("a?", "a"), "question mark needs a character");
    check(stx_fontpack::pattern_matches("*a*b*", "xxaxxbxx"), "several stars");
    check(!stx_fontpack::pattern_matches("*a*b", "xxaxxbxxc"), "several stars, wrong end");
    std::vector<stx_fontpack::PackInput> fonts(3);
    fonts[0].name = "Portsmouth.ttf";
    fonts[1].name = "Carlito-Regular.ttf";
    fonts[2].name = "Tinos-Regular.ttf";
    std::vector<stx_fontpack::PackGroupSpec> groups;
    std::string error;
    const std::string text = "# comment\ngroup interface eager\nPortsmouth*.ttf\r\nCarlito-*.ttf\n\ngroup families lazy\n*\n";
    check(stx_fontpack::manifest_groups(text, fonts, groups, error), "manifest parses");
    check(groups.size() == 2 && groups[0].eager && !groups[1].eager, "manifest groups and modes");
    check(groups.size() == 2 && groups[0].fonts.size() == 2 && groups[1].fonts.size() == 1, "first matching group wins");
    check(!stx_fontpack::manifest_groups("group a eager\nPortsmouth*\n", fonts, groups, error) && groups.empty(),
          "an unplaced font fails and leaves no groups");
    check(!stx_fontpack::manifest_groups("Portsmouth*\n", fonts, groups, error), "a pattern before a group fails");
    check(!stx_fontpack::manifest_groups("group a sometimes\n*\n", fonts, groups, error), "a bad mode fails");
    check(!stx_fontpack::manifest_groups("group a eager\n*\ngroup b lazy\nX*\n", fonts, groups, error),
          "a group matching no font fails");
}

// Packs fonts in two groups (eager, lazy), checks the round trip on three
// executors, then the lazy set: opening decodes only the eager group, and
// the first request for a lazy font decodes its group once. compare_serial
// also packs on one worker and requires the identical pack (slow for the
// bundled fonts, so only the synthetic run does it).
void test_round_trip_and_groups(const std::vector<stx_fontpack::PackInput>& fonts, bool compare_serial, Bytes& pack_out) {
    std::vector<stx_fontpack::PackGroupSpec> groups(2);
    groups[0].name = "first";
    groups[0].eager = true;
    groups[1].name = "second";
    groups[1].eager = false;
    for (std::size_t f = 0; f < fonts.size(); f += 1) {
        groups[f % 2 == 0 ? 0 : 1].fonts.push_back(static_cast<std::uint32_t>(f));
    }
    const stx_fontpack::Executor threads = stx_fontpack_tools::thread_executor(4);
    std::string error;
    Bytes pack;
    check(stx_fontpack::pack_fonts(fonts, groups, threads, nullptr, pack, error), "the inputs pack");
    if (compare_serial) {
        Bytes serial_pack;
        check(stx_fontpack::pack_fonts(fonts, groups, stx_fontpack::serial_executor(), nullptr, serial_pack, error) &&
                  serial_pack == pack,
              "packing is the same on one worker and on four");
    }
    check(round_trip(pack, fonts, stx_fontpack::serial_executor()), "round trip, serial executor");
    check(round_trip(pack, fonts, reverse_executor(7)), "round trip, workers in reverse order");
    check(round_trip(pack, fonts, threads), "round trip, four threads");

    CheckingAllocator state;
    checking_reset(state, static_cast<std::size_t>(-1));
    const stx_fontpack::Allocator allocator = checking_interface(state);
    std::unique_ptr<stx_fontpack::LoadedSet> set(new stx_fontpack::LoadedSet);
    check(stx_fontpack::set_open(as_bytes(pack), allocator, threads, *set), "the set opens");
    check((*set).loaded[0] && !(*set).loaded[1], "opening decodes exactly the eager group");
    const std::string lazy_name = fonts[1].name;
    const stx_fontpack::LoadedFont* lazy = stx_fontpack::set_find_font(*set, lazy_name);
    check(lazy != nullptr && (*set).loaded[1], "the first request decodes the lazy group");
    check(lazy != nullptr && (*lazy).size == fonts[1].data.size() &&
              std::memcmp((*lazy).data, fonts[1].data.data(), (*lazy).size) == 0,
          "the lazy font is its input");
    const std::size_t calls = state.calls.load();
    check(stx_fontpack::set_find_font(*set, lazy_name) == lazy && state.calls.load() == calls,
          "a second request decodes nothing");
    check(stx_fontpack::set_find_font(*set, "no such font") == nullptr, "an unknown name finds nothing");
    stx_fontpack::set_release(*set);
    check(state.outstanding_blocks.load() == 0 && state.wrong_sizes.load() == 0, "the set gives everything back");

    // The lazy group alone, without the eager one.
    std::unique_ptr<stx_fontpack::PackIndex> index(new stx_fontpack::PackIndex);
    check(stx_fontpack::pack_open(as_bytes(pack), *index), "the index opens");
    stx_fontpack::LoadedGroup second;
    check(stx_fontpack::pack_load_group(*index, 1, allocator, threads, second) && second.font_count == groups[1].fonts.size(),
          "the lazy group decodes alone");
    stx_fontpack::group_release(second);
    stx_fontpack::LoadedGroup missing;
    check(!stx_fontpack::pack_load_group(*index, 2, allocator, threads, missing) &&
              missing.refusal == stx_fontpack::kGroupBadIndex,
          "a group past the end is a bad index");
    check(state.outstanding_blocks.load() == 0, "single group loads give everything back");

    // An eager group that fails its check refuses the whole set, and says which and why.
    std::vector<stx_fontpack::ContainerGroup> container;
    container_from_index(*index, container);
    container[0].fonts[0].crc ^= 0x80000000u;
    Bytes damaged;
    stx_fontpack::container_write(container, damaged);
    check(!stx_fontpack::set_open(as_bytes(damaged), allocator, threads, *set) && !(*set).index_refused &&
              (*set).open_refused_group == 0 && (*set).open_refusal == stx_fontpack::kGroupBadCheck,
          "an eager group failing integrity refuses the set with its reason");
    check(std::strcmp(stx_fontpack::refusal_name((*set).open_refusal), "bad check value") == 0, "the refusal has a name");
    damaged[0] = 'X';
    check(!stx_fontpack::set_open(as_bytes(damaged), allocator, threads, *set) && (*set).index_refused,
          "a container that does not open refuses the set at the index");
    check(state.outstanding_blocks.load() == 0, "refused sets give everything back");
    pack_out.swap(pack);
}

// Every allocation of a load in turn fails: each load is refused as out of
// memory (or loads, once the failure is past the last allocation) and gives
// back everything it took.
void test_no_memory(const Bytes& pack) {
    std::unique_ptr<stx_fontpack::PackIndex> index(new stx_fontpack::PackIndex);
    check(stx_fontpack::pack_open(as_bytes(pack), *index), "the index opens");
    CheckingAllocator state;
    const stx_fontpack::Allocator allocator = checking_interface(state);
    bool loaded_once = false;
    for (std::size_t fail_from = 0; fail_from < 4096 && !loaded_once; fail_from += 1) {
        checking_reset(state, fail_from);
        stx_fontpack::LoadedGroup group;
        const bool ok = stx_fontpack::pack_load_group(*index, 0, allocator, stx_fontpack::serial_executor(), group);
        if (ok) {
            loaded_once = true;
            stx_fontpack::group_release(group);
        } else {
            check(group.refusal == stx_fontpack::kGroupNoMemory, "a failed allocation refuses as no memory");
        }
        check(state.outstanding_blocks.load() == 0 && state.wrong_sizes.load() == 0,
              "an out-of-memory refusal gives everything back");
    }
    check(loaded_once, "the load succeeds once no allocation fails");
}

struct ClaimsCase {
    const char* what;
    std::uint32_t group;
    bool expect_open;
    stx_fontpack::GroupRefusal expected;   // kGroupLoaded: any refusal before storage
};

// Opens damaged and loads one group on one worker. Refusals other than a
// check value must come before any font storage is asked for and with no
// more scratch than the genuine load (a refused load on one worker is an
// exact prefix of the genuine one).
void claims_case(const ClaimsCase& claim, const Bytes& damaged, std::size_t genuine_scratch) {
    CheckingAllocator state;
    checking_reset(state, static_cast<std::size_t>(-1));
    const stx_fontpack::Allocator allocator = checking_interface(state);
    std::unique_ptr<stx_fontpack::PackIndex> index(new stx_fontpack::PackIndex);
    const bool opened = stx_fontpack::pack_open(as_bytes(damaged), *index);
    bool pass = false;
    if (!claim.expect_open) {
        pass = !opened && (*index).group_count == 0;
    } else if (opened) {
        stx_fontpack::LoadedGroup loaded;
        const bool ok = stx_fontpack::pack_load_group(*index, claim.group, allocator, stx_fontpack::serial_executor(), loaded);
        if (ok) {
            stx_fontpack::group_release(loaded);
        } else if (claim.expected == stx_fontpack::kGroupBadCheck) {
            pass = loaded.refusal == stx_fontpack::kGroupBadCheck;
        } else {
            pass = (claim.expected == stx_fontpack::kGroupLoaded || loaded.refusal == claim.expected) &&
                   loaded.storage_requested == 0 && loaded.scratch_peak <= genuine_scratch;
        }
    }
    pass = pass && state.outstanding_blocks.load() == 0;
    if (!pass) {
        std::printf("claims case: %s (group %u)\n", claim.what, claim.group);
    }
    check(pass, "a wrong claim is refused before it allocates");
}

// A-gf-typography-01-F2: the first font of a group starts every stream at 0.
// Each start of font 0 that can be raised to 1 without breaking the order of
// starts (font 1 starts that stream at 1 or later, or font 0 is alone and the
// stream is not empty) is raised in place, one stream at a time; the load is
// refused as a bad claim before anything decodes.
void test_first_font_starts(const Bytes& pack, const stx_fontpack::PackIndex& index) {
    std::uint32_t cases = 0;
    for (std::uint32_t g = 0; g < index.group_count; g += 1) {
        const stx_fontpack::PackGroup& group = index.groups[g];
        stx_fontpack::Reader body = stx_fontpack::reader_make(group.body, group.body_size);
        std::size_t start_at[stx_fontpack::kStreamCount];
        std::uint64_t first_starts[stx_fontpack::kStreamCount];
        std::uint64_t second_starts[stx_fontpack::kStreamCount];
        for (std::uint32_t s = 0; s < stx_fontpack::kStreamCount; s += 1) {
            start_at[s] = static_cast<std::size_t>(body.cursor - pack.data());
            first_starts[s] = stx_fontpack::get_varint(body);
        }
        for (std::uint32_t s = 0; s < stx_fontpack::kStreamCount; s += 1) {
            second_starts[s] = group.font_count > 1 ? stx_fontpack::get_varint(body) : 0;
        }
        for (std::uint32_t f = 2; f < group.font_count; f += 1) {
            for (std::uint32_t s = 0; s < stx_fontpack::kStreamCount; s += 1) {
                static_cast<void>(stx_fontpack::get_varint(body));
            }
        }
        std::size_t blob_sizes[stx_fontpack::kStreamCount];
        for (std::uint32_t s = 0; s < stx_fontpack::kStreamCount; s += 1) {
            blob_sizes[s] = static_cast<std::size_t>(stx_fontpack::get_varint(body));
        }
        std::uint64_t declared[stx_fontpack::kStreamCount];
        for (std::uint32_t s = 0; s < stx_fontpack::kStreamCount; s += 1) {
            stx_fontpack::Reader peek = stx_fontpack::reader_make(body.cursor, blob_sizes[s]);
            declared[s] = blob_sizes[s] != 0 ? stx_fontpack::get_varint(peek) : 0;
            body.cursor += blob_sizes[s];
        }
        check(!body.failed, "the genuine body parses");
        for (std::uint32_t s = 0; s < stx_fontpack::kStreamCount && !body.failed; s += 1) {
            const bool room = group.font_count > 1 ? second_starts[s] >= 1 : declared[s] >= 1;
            if (first_starts[s] != 0 || !room) {
                continue;
            }
            Bytes damaged = pack;
            damaged[start_at[s]] = 1;
            ClaimsCase claim;
            claim.what = "the first font starts a stream at 1";
            claim.group = g;
            claim.expect_open = true;
            claim.expected = stx_fontpack::kGroupBadClaims;
            claims_case(claim, damaged, static_cast<std::size_t>(-1));
            cases += 1;
        }
    }
    check(cases > 0, "some first-font start can be raised");
}

void test_claims(const Bytes& pack) {
    std::unique_ptr<stx_fontpack::PackIndex> index(new stx_fontpack::PackIndex);
    check(stx_fontpack::pack_open(as_bytes(pack), *index), "the genuine pack opens");
    std::vector<stx_fontpack::ContainerGroup> genuine;
    container_from_index(*index, genuine);
    const stx_fontpack::Allocator allocator = stx_fontpack::standard_allocator();
    for (std::uint32_t g = 0; g < (*index).group_count; g += 1) {
        const stx_fontpack::PackGroup& group = (*index).groups[g];
        stx_fontpack::LoadedGroup loaded;
        check(stx_fontpack::pack_load_group(*index, g, allocator, stx_fontpack::serial_executor(), loaded),
              "the genuine group loads");
        const std::size_t genuine_scratch = loaded.scratch_peak;
        stx_fontpack::group_release(loaded);
        Bytes damaged;
        std::vector<stx_fontpack::ContainerGroup> changed;
        ClaimsCase claim;
        claim.group = g;

        changed = genuine;
        changed[g].fonts[0].size = 0x7FFFFFFFu;
        changed[g].font_bytes = genuine[g].font_bytes - genuine[g].fonts[0].size + 0x7FFFFFFFu;
        stx_fontpack::container_write(changed, damaged);
        claim.what = "font size 2^31 - 1, group total to match";
        claim.expect_open = false;
        claim.expected = stx_fontpack::kGroupLoaded;
        claims_case(claim, damaged, genuine_scratch);

        changed = genuine;
        changed[g].fonts[0].size += 4096;
        changed[g].font_bytes += 4096;
        stx_fontpack::container_write(changed, damaged);
        claim.what = "font size + 4096, group total to match";
        claim.expect_open = true;
        claim.expected = stx_fontpack::kGroupLoaded;
        claims_case(claim, damaged, genuine_scratch);

        changed = genuine;
        changed[g].font_bytes += 1;
        stx_fontpack::container_write(changed, damaged);
        claim.what = "group font total + 1";
        claim.expect_open = false;
        claims_case(claim, damaged, genuine_scratch);

        changed = genuine;
        changed[g].font_bytes = stx_fontpack::kMaxGroupFontBytes + 1ull;
        changed[g].fonts[0].size += changed[g].font_bytes - genuine[g].font_bytes;
        stx_fontpack::container_write(changed, damaged);
        claim.what = "group font total above kMaxGroupFontBytes";
        claim.expect_open = false;
        claims_case(claim, damaged, genuine_scratch);

        changed = genuine;
        changed[g].stream_bytes += 1;
        stx_fontpack::container_write(changed, damaged);
        claim.what = "group stream total + 1";
        claim.expect_open = true;
        claim.expected = stx_fontpack::kGroupBadClaims;
        claims_case(claim, damaged, genuine_scratch);

        changed = genuine;
        changed[g].stream_bytes = stx_fontpack::kMaxStreamExpansion * genuine[g].font_bytes + 1;
        stx_fontpack::container_write(changed, damaged);
        claim.what = "group stream total above kMaxStreamExpansion";
        claim.expect_open = false;
        claim.expected = stx_fontpack::kGroupLoaded;
        claims_case(claim, damaged, genuine_scratch);

        changed = genuine;
        changed[g].fonts[0].crc ^= 1u;
        stx_fontpack::container_write(changed, damaged);
        claim.what = "first font's CRC-32 with one bit flipped";
        claim.expect_open = true;
        claim.expected = stx_fontpack::kGroupBadCheck;
        claims_case(claim, damaged, genuine_scratch);

        // Each stream's own decoded-size claim, raised in place to the largest
        // value its varint length can hold: the body keeps its length, so only
        // that claim changes.
        stx_fontpack::Reader body = stx_fontpack::reader_make(group.body, group.body_size);
        for (std::uint32_t k = 0; k < group.font_count * stx_fontpack::kStreamCount; k += 1) {
            static_cast<void>(stx_fontpack::get_varint(body));
        }
        std::size_t blob_sizes[stx_fontpack::kStreamCount];
        for (std::uint32_t s = 0; s < stx_fontpack::kStreamCount; s += 1) {
            blob_sizes[s] = static_cast<std::size_t>(stx_fontpack::get_varint(body));
        }
        std::size_t at = static_cast<std::size_t>(body.cursor - pack.data());
        for (std::uint32_t s = 0; s < stx_fontpack::kStreamCount && !body.failed; s += 1) {
            damaged = pack;
            std::size_t length = 1;
            while (length < blob_sizes[s] && length < 5 && (damaged[at + length - 1] & 0x80u) != 0) {
                length += 1;
            }
            for (std::size_t b = 0; b + 1 < length; b += 1) {
                damaged[at + b] = 0xFF;
            }
            damaged[at + length - 1] = 0x7F;
            if (blob_sizes[s] != 0 && damaged != pack) {
                claim.what = "a stream's decoded size raised to its varint's largest";
                claim.expect_open = true;
                claim.expected = stx_fontpack::kGroupBadClaims;
                claims_case(claim, damaged, genuine_scratch);
            }
            at += blob_sizes[s];
        }
    }
    test_first_font_starts(pack, *index);
    Bytes damaged = pack;
    damaged[4] = 0x7F;
    ClaimsCase count;
    count.what = "group count 127";
    count.group = 0;
    count.expect_open = false;
    count.expected = stx_fontpack::kGroupLoaded;
    claims_case(count, damaged, 0);
}

// Loads damaged copies of the pack on four threads. Every load must return
// (loaded, which the CRC-32 vouches for, or refused) and give back all it
// took; run under the sanitizers, it also proves no out-of-bounds access.
void test_damage(const Bytes& pack, int rounds) {
    std::uint64_t random = 0x9E3779B97F4A7C15ull;
    CheckingAllocator state;
    const stx_fontpack::Allocator allocator = checking_interface(state);
    const stx_fontpack::Executor threads = stx_fontpack_tools::thread_executor(4);
    std::unique_ptr<stx_fontpack::PackIndex> index(new stx_fontpack::PackIndex);
    int refused = 0;
    for (int round = 0; round < rounds; round += 1) {
        Bytes damaged = pack;
        const std::uint32_t kind = static_cast<std::uint32_t>(random_next(random) >> 60);
        const std::uint32_t edits = 1 + static_cast<std::uint32_t>((random >> 52) & 7u);
        for (std::uint32_t e = 0; e < edits && damaged.size() > 64; e += 1) {
            random_next(random);
            // Half the edits land in the first 8 KiB, where the headers are.
            std::size_t where = static_cast<std::size_t>(random % damaged.size());
            if ((random >> 40 & 1u) != 0 && damaged.size() > 8192) {
                where = static_cast<std::size_t>(random % 8192);
            }
            if (kind < 10) {
                damaged[where] = static_cast<std::uint8_t>(random >> 32);
            } else if (kind < 13) {
                const std::size_t span = static_cast<std::size_t>((random >> 20) % 4096);
                for (std::size_t k = where; k < damaged.size() && k < where + span; k += 1) {
                    damaged[k] = 0;
                }
            } else if (where > 64) {
                damaged.resize(where);
            }
        }
        checking_reset(state, static_cast<std::size_t>(-1));
        if (!stx_fontpack::pack_open(as_bytes(damaged), *index)) {
            refused += 1;
            continue;
        }
        for (std::uint32_t g = 0; g < (*index).group_count; g += 1) {
            stx_fontpack::LoadedGroup result;
            if (stx_fontpack::pack_load_group(*index, g, allocator, threads, result)) {
                stx_fontpack::group_release(result);
            } else {
                refused += 1;
            }
        }
        check(state.outstanding_blocks.load() == 0 && state.wrong_sizes.load() == 0, "a damaged load gives everything back");
    }
    check(refused > 0, "damage is noticed");
}

// ---- crafted stream headers ---------------------------------------------------
//
// Headers the encoder never writes, stated field by field, so each decoder
// refusal can be shown on its own. Every shape has a control: the same header
// with the field the encoder would write, which must open, so a builder error
// shows as a failed control rather than as a refusal for the wrong reason.

// The LSB-first bit form of codec_encode.cpp's BitWriter, gamma code and table.
struct TestBits {
    Bytes bytes;
    std::uint64_t bits;
    std::uint32_t count;
};

void test_bits_reset(TestBits& writer) {
    writer.bytes.clear();
    writer.bits = 0;
    writer.count = 0;
}

// width is at most 32.
void test_bits_put(TestBits& writer, std::uint32_t value, std::uint32_t width) {
    if (width == 0) {
        return;
    }
    writer.bits |= static_cast<std::uint64_t>(value) << writer.count;
    writer.count += width;
    while (writer.count >= 8) {
        writer.bytes.push_back(static_cast<std::uint8_t>(writer.bits & 0xFFu));
        writer.bits >>= 8;
        writer.count -= 8;
    }
}

void test_bits_flush(TestBits& writer) {
    if (writer.count != 0) {
        writer.bytes.push_back(static_cast<std::uint8_t>(writer.bits & 0xFFu));
        writer.bits = 0;
        writer.count = 0;
    }
}

// value is at least 1.
void test_gamma_put(TestBits& writer, std::uint32_t value) {
    std::uint32_t top = 0;
    while ((value >> (top + 1)) != 0) {
        top += 1;
    }
    test_bits_put(writer, 0, top);
    test_bits_put(writer, 1, 1);
    test_bits_put(writer, value - (1u << top), top);
}

// A frequency table of alphabet symbols in which only symbols 0 and second
// (0 < second < alphabet) are used, each with half of kProbScale; or, with
// second 0, a table with no symbol used.
void test_table_put(TestBits& writer, std::uint32_t alphabet, std::uint32_t second) {
    if (second == 0) {
        test_bits_put(writer, 0, 1);
        test_gamma_put(writer, alphabet);
        return;
    }
    const std::uint32_t half = stx_fontpack::kProbScale / 2;
    test_bits_put(writer, 1, 1);
    test_gamma_put(writer, half);
    if (second > 1) {
        test_bits_put(writer, 0, 1);
        test_gamma_put(writer, second - 1);
    }
    test_bits_put(writer, 1, 1);
    test_gamma_put(writer, half);
    if (second + 1 < alphabet) {
        test_bits_put(writer, 0, 1);
        test_gamma_put(writer, alphabet - second - 1);
    }
}

// What a crafted number stream header says. The defaults (number_shape_default)
// are a header the encoder could write: one class, one literal table, one
// correction table, one chunk of four numbers in one sequence.
struct NumberShape {
    std::uint32_t class_count;
    std::uint32_t table_count;        // literal tables; the context map maps every class to table 0
    std::uint32_t residual_second;    // the correction table's second used code
    std::uint32_t offset_second;      // the offset table's second used code
};

NumberShape number_shape_default() {
    NumberShape shape;
    shape.class_count = 1;
    shape.table_count = 1;
    shape.residual_second = 1;
    shape.offset_second = 3;
    return shape;
}

// Appends the header and kStreamPadding zero bytes; size receives the header's length.
void number_stream_put(const NumberShape& shape, Bytes& out, std::size_t& size) {
    const std::uint32_t count = 4;
    out.clear();
    stx_fontpack::put_varint(out, count);
    out.push_back(static_cast<std::uint8_t>(shape.class_count));
    out.push_back(static_cast<std::uint8_t>(shape.table_count));
    if (shape.table_count != 1) {
        out.push_back(0);
        stx_fontpack::put_varint(out, shape.class_count);
    }
    out.push_back(1);   // one correction table: its map is implied
    TestBits bits;
    test_bits_reset(bits);
    for (std::uint32_t t = 0; t < shape.table_count; t += 1) {
        test_table_put(bits, 256, 1);
    }
    for (std::uint32_t t = 0; t < stx_fontpack::kLowTables; t += 1) {
        test_table_put(bits, 256, 0);
    }
    test_table_put(bits, 256, shape.residual_second);
    // Literal length, kind, the two match lengths, offset.
    for (std::uint32_t t = 0; t < 4; t += 1) {
        test_table_put(bits, stx_fontpack::kSeqAlphabet, 1);
    }
    test_table_put(bits, stx_fontpack::kSeqAlphabet, shape.offset_second);
    test_bits_flush(bits);
    stx_fontpack::put_varint(out, bits.bytes.size());
    out.insert(out.end(), bits.bytes.begin(), bits.bytes.end());
    stx_fontpack::put_varint(out, 1);       // chunks
    stx_fontpack::put_varint(out, count);   // the chunk's numbers
    stx_fontpack::put_varint(out, 1);       // its sequences
    for (std::uint32_t k = 0; k < stx_fontpack::kNumberPayloads; k += 1) {
        stx_fontpack::put_varint(out, 0);
    }
    size = out.size();
    out.insert(out.end(), static_cast<std::size_t>(stx_fontpack::kStreamPadding), static_cast<std::uint8_t>(0));
}

// Whether number_open accepts the shape; everything it took is given back either way.
bool number_shape_opens(const NumberShape& shape) {
    Bytes blob;
    std::size_t size = 0;
    number_stream_put(shape, blob, size);
    CheckingAllocator state;
    checking_reset(state, static_cast<std::size_t>(-1));
    const stx_fontpack::Allocator allocator = checking_interface(state);
    stx_fontpack::NumberDecoder decoder;
    std::size_t consumed = 0;
    const bool opened = stx_fontpack::number_open(decoder, allocator, blob.data(), size, consumed);
    if (opened) {
        check(consumed == size, "a crafted number header is consumed whole");
        stx_fontpack::number_close(decoder);
    }
    check(state.outstanding_blocks.load() == 0 && state.wrong_sizes.load() == 0, "a crafted number header gives everything back");
    return opened;
}

// What a crafted byte stream header says. The defaults
// (byte_shape_default) are a header the encoder could write: one class, no
// quantiser, one literal table, one chunk of four bytes in one sequence.
struct ByteShape {
    std::uint32_t class_count;
    std::uint32_t quantiser;
    std::uint32_t table_count;     // the context map maps every fine context to table 0
    std::uint32_t offset_second;   // the offset table's second used code
};

ByteShape byte_shape_default() {
    ByteShape shape;
    shape.class_count = 1;
    shape.quantiser = 0;
    shape.table_count = 1;
    shape.offset_second = 3;
    return shape;
}

// Appends the header and kStreamPadding zero bytes; size receives the header's length.
void byte_stream_put(const ByteShape& shape, Bytes& out, std::size_t& size) {
    const std::uint32_t decoded = 4;
    out.clear();
    stx_fontpack::put_varint(out, decoded);
    out.push_back(1);   // coded
    stx_fontpack::put_varint(out, shape.class_count);
    out.push_back(static_cast<std::uint8_t>(shape.quantiser));
    out.push_back(static_cast<std::uint8_t>(shape.table_count));
    if (shape.table_count != 1) {
        out.push_back(0);
        stx_fontpack::put_varint(out, shape.class_count * (stx_fontpack::quantiser_classes(shape.quantiser) + 1));
    }
    TestBits bits;
    test_bits_reset(bits);
    for (std::uint32_t t = 0; t < shape.table_count; t += 1) {
        test_table_put(bits, 256, 1);
    }
    test_table_put(bits, stx_fontpack::kSeqAlphabet, 1);   // literal lengths
    test_table_put(bits, stx_fontpack::kSeqAlphabet, 1);   // match lengths
    test_table_put(bits, stx_fontpack::kSeqAlphabet, shape.offset_second);
    test_bits_flush(bits);
    stx_fontpack::put_varint(out, bits.bytes.size());
    out.insert(out.end(), bits.bytes.begin(), bits.bytes.end());
    stx_fontpack::put_varint(out, 1);         // chunks
    stx_fontpack::put_varint(out, decoded);   // the chunk's bytes
    stx_fontpack::put_varint(out, 1);         // its sequences
    stx_fontpack::put_varint(out, 0);         // sequence payload bytes
    stx_fontpack::put_varint(out, 0);         // extra bits bytes
    stx_fontpack::put_varint(out, 0);         // literal payload bytes
    size = out.size();
    out.insert(out.end(), static_cast<std::size_t>(stx_fontpack::kStreamPadding), static_cast<std::uint8_t>(0));
}

// Whether stream_open accepts the shape; everything it took is given back either way.
bool byte_shape_opens(const ByteShape& shape) {
    Bytes blob;
    std::size_t size = 0;
    byte_stream_put(shape, blob, size);
    CheckingAllocator state;
    checking_reset(state, static_cast<std::size_t>(-1));
    const stx_fontpack::Allocator allocator = checking_interface(state);
    stx_fontpack::StreamDecoder decoder;
    std::size_t consumed = 0;
    const bool opened = stx_fontpack::stream_open(decoder, allocator, blob.data(), size, consumed);
    if (opened) {
        check(consumed == size, "a crafted byte header is consumed whole");
        stx_fontpack::stream_close(decoder);
    }
    check(state.outstanding_blocks.load() == 0 && state.wrong_sizes.load() == 0, "a crafted byte header gives everything back");
    return opened;
}

// A-gf-typography-01-F3: no more literal tables than contexts. A one-class
// stream with two tables would have read its null class array.
void test_table_counts() {
    ByteShape bytes = byte_shape_default();
    check(byte_shape_opens(bytes), "control: a one-class byte stream with one table opens");
    bytes.table_count = 2;
    check(!byte_shape_opens(bytes), "a one-class byte stream with two tables is refused");
    bytes.quantiser = 1;
    check(byte_shape_opens(bytes), "with a quantiser, two tables for nine fine contexts open");
    bytes.table_count = 10;
    check(!byte_shape_opens(bytes), "ten tables for nine fine contexts are refused");
    NumberShape numbers = number_shape_default();
    numbers.table_count = 2;
    check(!number_shape_opens(numbers), "a one-class number stream with two tables is refused");
    numbers.class_count = 2;
    check(number_shape_opens(numbers), "control: two classes with two tables open");
}

// A-gf-typography-01-F6: offset codes above 70 would decode with a shift of
// 32 or more; a table that gives one a frequency is refused at open.
void test_offset_codes() {
    ByteShape bytes = byte_shape_default();
    bytes.offset_second = stx_fontpack::kOffsetCodes - 1;
    check(byte_shape_opens(bytes), "a byte stream whose largest offset code is 70 opens");
    bytes.offset_second = stx_fontpack::kOffsetCodes;
    check(!byte_shape_opens(bytes), "a byte stream with offset code 71 is refused");
    bytes.offset_second = stx_fontpack::kSeqAlphabet - 1;
    check(!byte_shape_opens(bytes), "a byte stream with offset code 79 is refused");
    NumberShape numbers = number_shape_default();
    numbers.offset_second = stx_fontpack::kOffsetCodes - 1;
    check(number_shape_opens(numbers), "a number stream whose largest offset code is 70 opens");
    numbers.offset_second = 75;
    check(!number_shape_opens(numbers), "a number stream with offset code 75 is refused");
}

// A-gf-typography-01-F1: a correction code indexes 64-entry tables, so a
// correction table that gives a code of 64 or more a frequency is refused
// when the stream opens, before any chunk decodes with it.
void test_number_correction_codes() {
    NumberShape shape = number_shape_default();
    check(number_shape_opens(shape), "control: a correction table with codes below 64 opens");
    shape.residual_second = stx_fontpack::kResidualAlphabet - 1;
    check(number_shape_opens(shape), "a correction table whose largest code is 63 opens");
    shape.residual_second = stx_fontpack::kResidualAlphabet;
    check(!number_shape_opens(shape), "a correction table with code 64 is refused");
    shape.residual_second = 255;
    check(!number_shape_opens(shape), "a correction table with code 255 is refused");
}

}  // namespace

int main(int argc, char** argv) {
    test_crc_combine();
    test_manifest();
    test_number_correction_codes();
    test_table_counts();
    test_offset_codes();

    std::vector<stx_fontpack::PackInput> fonts;
    fonts.push_back(blob_input("noise-a.bin", 70000, 1, false));
    fonts.push_back(blob_input("repeat-a.bin", 300000, 2, true));
    fonts.push_back(sfnt_input("raw-a.ttf", 3, 3));
    fonts.push_back(sfnt_input("raw-b.ttf", 4, 4));
    fonts.push_back(blob_input("noise-b.bin", 1500000, 5, false));
    fonts.push_back(sfnt_input("raw-c.ttf", 2, 6));
    fonts.push_back(sfnt_past_end_input());
    if (argc > 1) {
        directory_inputs(argv[1], fonts);
    }
    // With the bundled fonts the slow parts (a second, serial pack and a load
    // per allocation) are left to the synthetic run.
    const bool synthetic_only = argc <= 1;
    Bytes pack;
    test_round_trip_and_groups(fonts, synthetic_only, pack);
    if (!pack.empty()) {
        if (synthetic_only) {
            test_no_memory(pack);
        }
        test_claims(pack);
        test_damage(pack, synthetic_only ? 200 : 60);
    }
    std::printf("stx_fontpack_tests: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
