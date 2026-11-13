#pragma once

#include "stx_fontpack/limits.hpp"
#include "stx_fontpack/runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace stx_fontpack {

// The loader.
//
// pack_open reads a pack's group and font table and checks every size claim
// in it; it allocates nothing. pack_load_group then turns one group into its
// original font files, byte for byte, in storage taken from the caller's
// allocator and owned by the LoadedGroup. Each LoadedFont's bytes can be
// handed straight to FT_New_Memory_Face; they stay valid until group_release.
//
// LoadedSet is the application's view: it decodes the eager groups when it
// opens and every other group the first time one of its fonts is asked for.
// A group is decoded at most once.
//
// The pack's bytes are borrowed throughout: they must stay valid and
// unchanged while an index, a set or a loaded font's name is in use. An
// embedded pack (static storage) meets that trivially.

// One font as the container describes it. Names borrow from the pack.
struct PackFont {
    const char* name;            // not terminated
    std::uint32_t name_length;
    std::uint32_t size;
    std::uint32_t crc;           // CRC-32 of the original file (stx_zlib::crc32)
    std::uint32_t group;
};

struct PackGroup {
    const char* name;            // not terminated; borrows from the pack
    std::uint32_t name_length;
    bool eager;
    std::uint32_t font_first;    // into PackIndex::fonts
    std::uint32_t font_count;
    std::uint64_t font_bytes;    // checked: the sum of the fonts' sizes
    std::uint64_t stream_bytes;  // checked against the body before any stream is opened
    const std::uint8_t* body;    // followed by the container's padding (readable)
    std::size_t body_size;
};

// Fixed capacity, so that opening a pack allocates nothing whatever it claims.
// group_count and font_count are the live lengths of the two arrays.
struct PackIndex {
    const std::uint8_t* pack;
    std::size_t pack_size;
    PackGroup groups[kMaxGroups];
    std::uint32_t group_count;
    PackFont fonts[kMaxFonts];
    std::uint32_t font_count;
};

// Reads and checks the container. On failure the index is empty (both
// counts 0) and nothing else changed.
[[nodiscard]] bool pack_open(std::span<const std::byte> pack, PackIndex& index) noexcept;

struct LoadedFont {
    const char* name;            // not terminated; borrows from the pack
    std::uint32_t name_length;
    const std::uint8_t* data;    // borrows from LoadedGroup::storage
    std::size_t size;
};

// Why a group did not load. A refusal leaves the LoadedGroup without storage.
enum GroupRefusal : std::uint32_t {
    kGroupLoaded = 0,
    kGroupBadIndex,              // no such group, or an index that did not come from pack_open
    kGroupBadClaims,             // a size in the body disagrees with the container's claims or limits
    kGroupBadStreams,            // a stream or rebuild step found damage
    kGroupBadCheck,              // a rebuilt font's CRC-32 differs from the stored one
    kGroupNoMemory               // the allocator returned null
};

// A short English phrase for diagnostics ("bad check value"); static storage.
[[nodiscard]] const char* refusal_name(GroupRefusal refusal) noexcept;

struct LoadedGroup {
    std::uint32_t group;
    std::uint8_t* storage;       // every font's bytes, owned (from allocator)
    std::size_t storage_bytes;   // sum of the font sizes (each rounded up to 8)
    LoadedFont* fonts;           // owned (from allocator); the group's fonts in index order
    std::uint32_t font_count;
    GroupRefusal refusal;
    // Kept on refusal too, so a test can see how far a damaged claim got.
    std::size_t scratch_peak;        // most temporary memory held at once while loading
    std::size_t storage_requested;   // bytes asked for font storage; 0 if refused before that
    Allocator allocator;             // what storage and fonts go back to
};

// Decodes one group. Scratch and storage come from allocator; the steps run
// on executor, with at most min(executor.worker_count, kMaxWorkers) workers.
// On success out owns the fonts; on refusal it owns nothing and says why.
// Either way every scratch allocation has been released when this returns.
[[nodiscard]] bool pack_load_group(const PackIndex& index, std::uint32_t group, const Allocator& allocator,
                                   const Executor& executor, LoadedGroup& out) noexcept;

// Gives the group's storage back to its allocator; the group is then empty.
void group_release(LoadedGroup& group) noexcept;

// Lazy loading over one pack. Not safe for concurrent use: the caller keeps
// it on one execution context (a decode it starts still runs on executor).
struct LoadedSet {
    PackIndex index;
    LoadedGroup groups[kMaxGroups];
    bool loaded[kMaxGroups];
    bool refused[kMaxGroups];    // a refused group is not retried: the pack does not change
    Allocator allocator;
    Executor executor;
    // Why set_open failed: the index was refused (index_refused), or eager
    // group open_refused_group was refused for open_refusal.
    bool index_refused;
    std::uint32_t open_refused_group;
    GroupRefusal open_refusal;
};

// Opens the index and decodes every eager group. Fails if the container or
// any eager group is refused; the set then holds nothing and records why.
[[nodiscard]] bool set_open(std::span<const std::byte> pack, const Allocator& allocator, const Executor& executor,
                            LoadedSet& set) noexcept;

// Decodes group if it is not loaded yet. Returns whether it is loaded.
[[nodiscard]] bool set_need_group(LoadedSet& set, std::uint32_t group) noexcept;

// The font named name (first match in index order), decoding its group on
// first need; null if there is no such font or its group is refused. The
// result borrows from set until set_release.
[[nodiscard]] const LoadedFont* set_find_font(LoadedSet& set, std::string_view name) noexcept;

// Releases every loaded group; the set is then empty.
void set_release(LoadedSet& set) noexcept;

}  // namespace stx_fontpack
