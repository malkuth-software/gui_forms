#pragma once

// A spatial index over rectangles in graph space (node graph gap G8; the
// node engine study, section 2.5): hit testing and visible-set queries for
// a retained scene, such as a NodeCanvas's nodes and links. It is a uniform
// grid in compressed rows (each cell's entries stored contiguously after a
// count and prefix pass), built once per structural change and queried
// many times per frame.
//
// Entries are named by caller identities, 0 to count - 1 (a node graph's
// slot indices); the index copies their bounds at build. Bounds may change
// afterwards without a rebuild (move_entry): an entry whose cell range is
// unchanged is updated in place, and one that left its cells joins a short
// displaced list every query visits, until the list is full and the caller
// rebuilds. A drag of a few hundred nodes therefore costs no rebuild.
//
// Bounds and costs:
// - at most maximum_spatial_entries entries;
// - at most SpatialIndexOptions::maximum_cells cells: the cell grows (by
//   doubling, bounded) until the grid over every present entry fits;
// - an entry covering more than maximum_cells_per_entry cells is kept in an
//   oversize list every query visits, so one huge entry cannot fill the
//   grid; the build counts them (statistics().oversize);
// - a query visits at most the cells its area covers (clamped to the grid,
//   so an area or an entry outside the built box meets the edge cells),
//   plus the oversize and displaced lists;
// - build may allocate; move_entry, remove_entry and query never do.
//
// A query writes matching identities into caller storage, each once (a
// per-entry stamp marks those already met), in no particular order, and
// counts every match even past the storage's end, so a caller can size its
// storage and query again. The index keeps no pointer into caller data.
//
// One thread: queries update the stamps, so even a query mutates.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/scene/world_geometry/world_geometry.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace gui_forms {

// The most entries one index holds (identities are 32-bit; this bound keeps
// the per-entry tables and every cell list countable in 32 bits).
inline constexpr std::size_t maximum_spatial_entries = std::size_t{1U} << 24U;
// The largest maximum_cells an index accepts (its grid sides are 32-bit).
inline constexpr std::size_t maximum_spatial_cells = std::size_t{1U} << 26U;

// One entry given to build: its bounds, and whether it is present (an
// absent entry, such as a free graph slot, is skipped and never matches).
struct GUI_FORMS_API SpatialEntry final {
    WorldRect bounds{};
    bool present{};
    friend bool operator==(const SpatialEntry& left, const SpatialEntry& right) noexcept {
        return left.bounds == right.bounds && left.present == right.present;
    }
};

// Which entries a query matches: those whose bounds share a point with the
// area (touching counts), or those wholly inside it (a full-containment
// marquee).
enum class SpatialQueryMode : std::uint8_t { intersecting, contained };

// The grid's terms. `cell_size` is the smallest cell side in world units;
// `maximum_cells` caps the grid (4 to maximum_spatial_cells);
// `maximum_cells_per_entry`
// sends a larger entry to the oversize list (at least 1);
// `maximum_displaced` is how many moved entries may leave their cells
// before move_entry asks for a rebuild (at most maximum_spatial_entries,
// reserved at build).
struct GUI_FORMS_API SpatialIndexOptions final {
    double cell_size{256.0};
    std::size_t maximum_cells{std::size_t{1U} << 20U};
    std::size_t maximum_cells_per_entry{64U};
    std::size_t maximum_displaced{1024U};
};

// What one query found: `written` identities stored at the front of the
// caller's storage, and `matched` matches in all. `matched > written`
// means the storage was too small; the identities written are then some of
// the matches, which ones unspecified.
struct GUI_FORMS_API SpatialQueryResult final {
    std::size_t written{};
    std::size_t matched{};
};

// Counters, plain data. The build fields describe the latest build; the
// rest only grow until the next build resets them with the grid.
struct GUI_FORMS_API SpatialIndexStatistics final {
    std::uint64_t builds{};
    std::uint64_t queries{};
    std::uint64_t cells_visited{};
    std::uint64_t entries_visited{};
    std::uint64_t moved_in_place{};
    std::uint64_t moved_out_of_cells{};
    std::uint64_t moves_refused{};
    std::size_t columns{};
    std::size_t rows{};
    double cell_size{};
    std::size_t cell_entries{};
    std::size_t oversize{};
    std::size_t displaced{};
};

class GUI_FORMS_API SpatialIndex final {
public:
    SpatialIndex() = default;
    SpatialIndex(const SpatialIndex&) = delete;
    SpatialIndex& operator=(const SpatialIndex&) = delete;
    SpatialIndex(SpatialIndex&&) noexcept = default;
    SpatialIndex& operator=(SpatialIndex&&) noexcept = default;

    // Replaces the whole index with `entries` (entry i is identity i).
    // Validates first: throws std::invalid_argument for options outside
    // their stated ranges (a non-finite or non-positive cell size, a cell
    // cap outside 4 to maximum_spatial_cells, no cell per entry, too long a
    // displaced list) or a present entry whose bounds are not
    // valid_world_rect, and std::length_error for more than
    // maximum_spatial_entries entries or a grid whose cell lists would not
    // fit 32-bit offsets; the index is then unchanged. Storage is grown,
    // never shrunk, so rebuilding a graph of the same size allocates
    // nothing. If an allocation fails the index is left empty.
    void build(std::span<const SpatialEntry> entries, const SpatialIndexOptions& options);

    // Gives entry `id` new bounds. False, and nothing changes, when `id` is
    // past the entries, the entry is absent, `bounds` is not
    // valid_world_rect, or the entry would leave its cells while the
    // displaced list is full (rebuild then). Never allocates.
    [[nodiscard]] bool move_entry(std::uint32_t id, const WorldRect& bounds) noexcept;

    // Makes entry `id` absent: no query matches it until a build. False
    // when it is past the entries or already absent. Never allocates.
    bool remove_entry(std::uint32_t id) noexcept;

    // Writes into `out` the present entries matching `area` under `mode`.
    // An area that is not valid_world_rect matches nothing. Never
    // allocates.
    [[nodiscard]] SpatialQueryResult query(const WorldRect& area, SpatialQueryMode mode,
                                           std::span<std::uint32_t> out) noexcept;

    // Empties the index (keeping its storage).
    void clear() noexcept;

    // The identities the latest build covered (present or not).
    [[nodiscard]] std::size_t entry_count() const noexcept { return entries_.size(); }
    // Whether entry `id` is present, and its current bounds (an empty
    // rectangle at the origin for an absent or unknown entry).
    [[nodiscard]] bool present(std::uint32_t id) const noexcept;
    [[nodiscard]] WorldRect bounds(std::uint32_t id) const noexcept;
    [[nodiscard]] const SpatialIndexStatistics& statistics() const noexcept { return statistics_; }

private:
    // Where an entry is listed: nowhere (absent), in its grid cells, in the
    // oversize list, or in the displaced list (its old cells still list it
    // and are skipped for it).
    enum class Placement : std::uint8_t { absent, cells, oversize, displaced };

    // An entry's cell range at build (inclusive), its placement and, while
    // displaced, its position in displaced_.
    struct EntryPlacement final {
        std::uint32_t first_column{};
        std::uint32_t last_column{};
        std::uint32_t first_row{};
        std::uint32_t last_row{};
        std::uint32_t displaced_at{};
        Placement placement{Placement::absent};
    };

    // An inclusive cell range, clamped to the grid.
    struct CellRange final {
        std::uint32_t first_column{};
        std::uint32_t last_column{};
        std::uint32_t first_row{};
        std::uint32_t last_row{};
    };

    [[nodiscard]] std::uint32_t column_of(double x) const noexcept;
    [[nodiscard]] std::uint32_t row_of(double y) const noexcept;
    [[nodiscard]] CellRange range_of(const WorldRect& rect) const noexcept;
    [[nodiscard]] static std::size_t cell_count_of(const CellRange& range) noexcept;
    [[nodiscard]] bool same_cells(const EntryPlacement& placement, const CellRange& range) const noexcept;
    void drop_displaced(std::uint32_t id) noexcept;
    void advance_stamp() noexcept;
    // Counts a match of `id` and stores it if room remains.
    static void emit(std::uint32_t id, std::span<std::uint32_t> out, SpatialQueryResult& result) noexcept;

    std::vector<WorldRect> entries_{};
    std::vector<EntryPlacement> placements_{};
    // cell_start_[cell] .. cell_start_[cell + 1] index cell_entries_ (the
    // compressed rows); its length is the cell count plus one.
    std::vector<std::uint32_t> cell_start_{};
    std::vector<std::uint32_t> cell_entries_{};
    // The build's per-cell fill counters, kept to reuse their storage.
    std::vector<std::uint32_t> cell_fill_{};
    std::vector<std::uint32_t> oversize_{};
    // Capacity maximum_displaced, reserved at build; size the live count.
    std::vector<std::uint32_t> displaced_{};
    std::size_t maximum_displaced_{};
    // One stamp per entry: the query that last met it.
    std::vector<std::uint32_t> marks_{};
    std::uint32_t stamp_{};
    double left_{};
    double top_{};
    double cell_{1.0};
    std::uint32_t columns_{};
    std::uint32_t rows_{};
    SpatialIndexStatistics statistics_{};
};

} // namespace gui_forms
