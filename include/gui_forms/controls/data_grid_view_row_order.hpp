#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gui_forms {

// A row's stable identity: not empty, unique among the grid's rows, and kept
// while the row moves. Unbound rows, virtual rows and BindingSource records
// each carry one. The selection, the current cell, an edit in progress and a
// pressed button cell are kept by it, so inserting, removing and sorting rows
// never moves them onto other rows. It is also the row's semantic stable ID,
// and a cell's is the row's, '.' and the column's name.
struct DataGridViewRowId final {
    std::string value{};

    friend bool operator==(const DataGridViewRowId& left, const DataGridViewRowId& right) {
        const bool equal = left.value == right.value;
        return equal;
    }
};

// A row's presentation position: its index in the grid's current row order,
// 0 at the top. A position is not an identity: inserting, removing or sorting
// rows changes which row holds it. row_id and row_position convert between
// the two.
struct DataGridViewRowPosition final {
    std::size_t value{};

    friend constexpr bool operator==(DataGridViewRowPosition left, DataGridViewRowPosition right) noexcept {
        const bool equal = left.value == right.value;
        return equal;
    }
    friend constexpr bool operator<(DataGridViewRowPosition left, DataGridViewRowPosition right) noexcept {
        const bool less = left.value < right.value;
        return less;
    }
};


// The application side of a virtual grid's window mode (owner decision W-5):
// the application keeps the rows' order, and the grid asks for the IDs at
// the positions it shows (plus a margin) instead of holding the whole order.
// Calls arrive on the grid's UI thread; the source may not mutate the grid
// from inside one. IDs are not empty, valid UTF-8 and unique, as for
// set_virtual_rows.
class DataGridViewRowWindowSource {
public:
    virtual ~DataGridViewRowWindowSource();
    // The number of rows in the current order.
    [[nodiscard]] virtual std::size_t row_count() const = 0;
    // Fills `out` with the IDs at positions [first, first + out.size()), which
    // the grid keeps within row_count().
    virtual void row_ids(DataGridViewRowPosition first, std::span<DataGridViewRowId> out) const = 0;
    // The position of a row in the current order, or none when it is gone.
    [[nodiscard]] virtual std::optional<DataGridViewRowPosition> row_position(const DataGridViewRowId& row) const = 0;
};

// How a virtual grid holds its rows' order (owner decision W-5). edit: the
// grid holds every ID in a chunked order index and the program reports
// inserts, removals and moves by ID, each costing in proportion to the change.
// window: the grid holds only the IDs around the rows it shows and asks a
// DataGridViewRowWindowSource for the rest.
enum class DataGridViewRowOrderMode : std::uint8_t { edit, window };

// Who chooses the mode: the grid by row count and change rate (automatic),
// or the program, pinning one. window needs a window source.
enum class DataGridViewRowOrderPolicy : std::uint8_t { automatic, edit, window };

// The automatic policy's thresholds. The grid goes to window mode when the
// rows reach window_rows, or the reported changes since the last layout pass
// reach window_changes; it returns to edit mode when the rows are at most
// edit_rows and both the last pass's and this pass's changes are at most
// edit_changes. The gaps between the pairs keep it from switching back and
// forth. window_margin is how many rows past each end of the visible ones a
// window holds.
struct DataGridViewRowOrderThresholds final {
    std::size_t window_rows{250000};
    std::size_t edit_rows{100000};
    std::size_t window_changes{20000};
    std::size_t edit_changes{1000};
    std::size_t window_margin{64};

    friend bool operator==(const DataGridViewRowOrderThresholds& left,
                           const DataGridViewRowOrderThresholds& right) noexcept {
        return left.window_rows == right.window_rows && left.edit_rows == right.edit_rows &&
               left.window_changes == right.window_changes && left.edit_changes == right.edit_changes &&
               left.window_margin == right.window_margin;
    }
};

// Why the mode changed.
enum class DataGridViewRowOrderSwitchReason : std::uint8_t {
    // The rows crossed window_rows or edit_rows.
    row_count,
    // The reported changes crossed window_changes, or fell to edit_changes.
    change_rate,
    // The program pinned a mode or returned to automatic.
    policy,
    // A window source was added or removed, or the rows were replaced.
    source,
};

struct DataGridViewRowOrderModeChange final {
    DataGridViewRowOrderMode old_mode{DataGridViewRowOrderMode::edit};
    DataGridViewRowOrderMode new_mode{DataGridViewRowOrderMode::edit};
    DataGridViewRowOrderSwitchReason reason{DataGridViewRowOrderSwitchReason::row_count};
    // The row count and this pass's reported changes when it switched.
    std::size_t row_count{};
    std::size_t changes{};
};

// Counters of the order's work since it was made, for diagnostics and the
// tests that measure each change's cost.
struct DataGridViewRowOrderStatistics final {
    // Rows reported inserted, removed or moved.
    std::uint64_t reported_changes{};
    // ID-index entries written (edit mode).
    std::uint64_t index_writes{};
    // IDs shifted within chunks (edit mode).
    std::uint64_t slot_moves{};
    // Rebuilds of the chunk table after a chunk split, merged or emptied;
    // each costs the number of chunks.
    std::uint64_t chunk_table_rebuilds{};
    // Whole-index builds: resets and switches to edit mode; each costs the
    // number of rows.
    std::uint64_t full_builds{};
    // Window mode: the row_ids calls, the IDs they asked for, and the
    // row_position calls.
    std::uint64_t window_requests{};
    std::uint64_t ids_requested{};
    std::uint64_t position_queries{};
    std::uint64_t mode_switches{};
};

// A virtual grid's row order (owner decision W-5), renderer-free: the IDs in
// presentation order in one of two modes.
//
// Edit mode keeps every ID in chunks of at most 2 * chunk_capacity IDs, in
// order, with a Fenwick tree of the chunks' sizes and an ID-to-chunk index.
// Finding an ID's position or a position's ID costs O(log chunks +
// chunk_capacity); inserting or removing one row costs O(chunk_capacity +
// log chunks) (it shifts at most one chunk's IDs), so r rows cost r times
// that, plus O(chunks) when a chunk splits, merges or empties (at most once
// per chunk_capacity changes to one chunk); a move is a removal and an
// insertion. A reset builds everything, O(rows).
//
// Window mode holds the row count and the IDs of one window of positions,
// [window_first, window_first + window size), each with its position;
// prepare_window fetches a window from the source when the positions asked
// for are not all held, and any reported change discards it. An ID outside
// the window is asked of the source, one at a time.
//
// The order validates what the program reports in edit mode (IDs not empty,
// valid UTF-8, unique, and present when removed or moved) and throws
// std::invalid_argument, changing nothing; in window mode the source is
// trusted. Single UI thread.
class DataGridViewRowOrder final {
public:
    static constexpr std::size_t chunk_capacity = 256U;

    DataGridViewRowOrder();
    ~DataGridViewRowOrder();
    DataGridViewRowOrder(const DataGridViewRowOrder&) = delete;
    DataGridViewRowOrder& operator=(const DataGridViewRowOrder&) = delete;

    [[nodiscard]] DataGridViewRowOrderMode mode() const noexcept { return mode_; }
    [[nodiscard]] std::size_t count() const noexcept;
    [[nodiscard]] const DataGridViewRowOrderStatistics& statistics() const noexcept { return statistics_; }

    // Edit mode with this order. Throws std::invalid_argument, changing
    // nothing, for an empty, repeated or invalid UTF-8 ID.
    void reset(std::span<const DataGridViewRowId> order);
    // Window mode over a source (not null; std::invalid_argument).
    void reset_to_window(std::shared_ptr<DataGridViewRowWindowSource> source);
    // Switches to edit mode by asking the source for every ID; false,
    // changing nothing, when the source gives an empty or repeated ID.
    bool switch_to_edit();
    // Switches to window mode over a source; the held IDs are released.
    void switch_to_window(std::shared_ptr<DataGridViewRowWindowSource> source);
    void clear() noexcept;

    // The ID at a position below count(), by value. In window mode a
    // position outside the window is asked of the source.
    [[nodiscard]] std::string id_at(std::size_t position) const;
    // The position of an ID now, or none.
    [[nodiscard]] std::optional<std::size_t> position_of(std::string_view id) const;
    // Window mode: the position an ID had in the window held before the last
    // reported change, or none. Edit mode: position_of.
    [[nodiscard]] std::optional<std::size_t> held_position(std::string_view id) const;
    // The ID at a position when it is held without asking the source (always
    // in edit mode; inside the window in window mode), or none.
    [[nodiscard]] std::optional<std::string> held_id_at(std::size_t position) const;
    // Edit mode: whether an ID is in the order. Window mode: false.
    [[nodiscard]] bool contains(std::string_view id) const;
    // Window mode: makes the window hold [first, end) widened by `margin`
    // rows on each side, asking the source only when it does not already.
    // Edit mode: does nothing.
    void prepare_window(std::size_t first, std::size_t end, std::size_t margin);
    // Window mode: the positions held, [window_first(), window_end()).
    [[nodiscard]] std::size_t window_first() const noexcept { return window_first_; }
    [[nodiscard]] std::size_t window_end() const noexcept { return window_first_ + window_ids_.size(); }

    // Reported changes. The rows are already in their new places in the
    // application's order. insert: the IDs now stand at [position, position
    // + ids.size()); position is at most count(). move: the ID now stands at
    // `position`, below count(). Edit mode validates (see above); window mode
    // forgets its window and asks the source for the new count.
    void insert(std::size_t position, std::span<const DataGridViewRowId> ids);
    void remove(std::span<const DataGridViewRowId> ids);
    void move(std::string_view id, std::size_t position);

private:
    struct Chunk final {
        // A serial number that stays with the chunk while chunks are added
        // and removed around it.
        std::uint32_t serial{};
        std::vector<std::string> ids{};
    };
    struct IdHash final {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view id) const noexcept {
            const std::size_t result = std::hash<std::string_view>{}(id);
            return result;
        }
    };
    using ChunkIndex = std::unordered_map<std::string, std::uint32_t, IdHash, std::equal_to<>>;
    using WindowIndex = std::unordered_map<std::string, std::size_t, IdHash, std::equal_to<>>;

    // Edit mode internals.
    void build_chunks(std::vector<std::string> ids);
    void rebuild_chunk_table();
    void fenwick_add(std::size_t chunk, std::ptrdiff_t delta) noexcept;
    [[nodiscard]] std::size_t fenwick_prefix(std::size_t chunk) const noexcept;
    // The chunk holding a position and the offset in it; position below
    // count().
    void locate(std::size_t position, std::size_t& chunk, std::size_t& offset) const noexcept;
    [[nodiscard]] std::uint32_t new_serial() noexcept;
    void insert_one(std::size_t position, const std::string& id);
    void remove_one(std::string_view id);
    void split_chunk(std::size_t chunk);
    void after_removal(std::size_t chunk);
    void validate_new_ids(std::span<const DataGridViewRowId> ids) const;
    // Window mode internals.
    void forget_window() noexcept;
    void fetch_window(std::size_t first, std::size_t end);

    DataGridViewRowOrderMode mode_{DataGridViewRowOrderMode::edit};
    // Edit mode: the chunks in order, the ID-to-chunk-serial index, each
    // serial's index in chunks_, and the Fenwick tree over chunk sizes
    // (one-based, size chunks_.size() + 1).
    std::vector<Chunk> chunks_{};
    ChunkIndex chunk_of_{};
    std::vector<std::size_t> chunk_slot_{};
    std::vector<std::size_t> fenwick_{};
    std::size_t edit_count_{};
    std::uint32_t next_serial_{};
    // Window mode: the source, its count, the window's IDs and their
    // positions, and the window before the last change (for held_position).
    std::shared_ptr<DataGridViewRowWindowSource> source_{};
    std::size_t window_count_{};
    std::size_t window_first_{};
    std::vector<DataGridViewRowId> window_ids_{};
    WindowIndex window_index_{};
    WindowIndex previous_window_{};
    // Counters; mutable since lookups in window mode count source calls.
    mutable DataGridViewRowOrderStatistics statistics_{};
};

} // namespace gui_forms
