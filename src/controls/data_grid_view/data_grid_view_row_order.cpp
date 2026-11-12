#include "gui_forms/controls/data_grid_view_row_order.hpp"

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/text.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

// A chunk merges into a neighbour when it falls below a quarter of the
// capacity and both fit in one chunk; it splits above twice the capacity.
constexpr std::size_t merge_below = DataGridViewRowOrder::chunk_capacity / 4U;
constexpr std::size_t split_above = DataGridViewRowOrder::chunk_capacity * 2U;

[[nodiscard]] bool valid_id(std::string_view id) {
    // A row's ID names its semantic node, so it follows the StableId rule.
    const bool valid = valid_stable_id(id);
    return valid;
}

// The lowest set bit of a one-based Fenwick index.
[[nodiscard]] std::size_t low_bit(std::size_t index) noexcept {
    const std::size_t bit = index & (~index + 1U);
    return bit;
}

} // namespace

DataGridViewRowWindowSource::~DataGridViewRowWindowSource() = default;

DataGridViewRowOrder::DataGridViewRowOrder() = default;
DataGridViewRowOrder::~DataGridViewRowOrder() = default;

std::size_t DataGridViewRowOrder::count() const noexcept {
    const std::size_t rows = mode_ == DataGridViewRowOrderMode::edit ? edit_count_ : window_count_;
    return rows;
}

// -- the chunk table -------------------------------------------------------------

std::uint32_t DataGridViewRowOrder::new_serial() noexcept {
    const std::uint32_t serial = next_serial_;
    ++next_serial_;
    return serial;
}

// The serial-to-index table and the Fenwick tree, from the chunks; O(chunks).
void DataGridViewRowOrder::rebuild_chunk_table() {
    chunk_slot_.assign(next_serial_, 0U);
    const std::size_t chunks = chunks_.size();
    fenwick_.assign(chunks + 1U, 0U);
    for (std::size_t index = 0; index < chunks; ++index) {
        chunk_slot_[chunks_[index].serial] = index;
        fenwick_[index + 1U] += chunks_[index].ids.size();
        const std::size_t parent = index + 1U + low_bit(index + 1U);
        if (parent <= chunks) fenwick_[parent] += fenwick_[index + 1U];
    }
    ++statistics_.chunk_table_rebuilds;
}

void DataGridViewRowOrder::fenwick_add(std::size_t chunk, std::ptrdiff_t delta) noexcept {
    for (std::size_t index = chunk + 1U; index < fenwick_.size(); index += low_bit(index)) {
        fenwick_[index] = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(fenwick_[index]) + delta);
    }
}

// The number of rows in chunks [0, chunk).
std::size_t DataGridViewRowOrder::fenwick_prefix(std::size_t chunk) const noexcept {
    std::size_t sum = 0U;
    for (std::size_t index = chunk; index > 0U; index -= low_bit(index)) sum += fenwick_[index];
    return sum;
}

// Descends the Fenwick tree: the last chunk whose start is at or before the
// position. Chunks are never empty, so the chunk holds it.
void DataGridViewRowOrder::locate(std::size_t position, std::size_t& chunk, std::size_t& offset) const noexcept {
    const std::size_t chunks = chunks_.size();
    std::size_t step = 1U;
    while (step * 2U <= chunks) step *= 2U;
    std::size_t index = 0U;
    std::size_t remaining = position;
    for (; step > 0U; step /= 2U) {
        const std::size_t next = index + step;
        if (next <= chunks && fenwick_[next] <= remaining) {
            index = next;
            remaining -= fenwick_[next];
        }
    }
    chunk = index;
    offset = remaining;
}

// Whole chunks of chunk_capacity IDs and the index of every ID; O(rows).
void DataGridViewRowOrder::build_chunks(std::vector<std::string> ids) {
    chunks_.clear();
    chunk_of_.clear();
    next_serial_ = 0U;
    const std::size_t total = ids.size();
    chunk_of_.reserve(total);
    for (std::size_t first = 0; first < total; first += chunk_capacity) {
        const std::size_t end = std::min(total, first + chunk_capacity);
        Chunk chunk{};
        chunk.serial = new_serial();
        chunk.ids.reserve(split_above + 1U);
        for (std::size_t index = first; index < end; ++index) {
            chunk_of_.emplace(ids[index], chunk.serial);
            chunk.ids.push_back(std::move(ids[index]));
        }
        chunks_.push_back(std::move(chunk));
    }
    edit_count_ = total;
    statistics_.index_writes += total;
    ++statistics_.full_builds;
    rebuild_chunk_table();
}

// -- resets and switches -------------------------------------------------------------

void DataGridViewRowOrder::reset(std::span<const DataGridViewRowId> order) {
    // Validated in full before anything changes.
    std::unordered_set<std::string_view> seen{};
    seen.reserve(order.size());
    for (const DataGridViewRowId& id : order) {
        if (!valid_id(id.value)) {
            throw std::invalid_argument("DataGridView virtual row IDs must follow the StableId segment rule");
        }
        if (!seen.insert(id.value).second) throw std::invalid_argument("DataGridView virtual row IDs must be unique");
    }
    std::vector<std::string> ids{};
    ids.reserve(order.size());
    for (const DataGridViewRowId& id : order) ids.push_back(id.value);
    clear();
    build_chunks(std::move(ids));
    mode_ = DataGridViewRowOrderMode::edit;
}

void DataGridViewRowOrder::reset_to_window(std::shared_ptr<DataGridViewRowWindowSource> source) {
    if (!source) throw std::invalid_argument("DataGridView window mode requires a row window source");
    clear();
    source_ = std::move(source);
    mode_ = DataGridViewRowOrderMode::window;
    window_count_ = (*source_).row_count();
}

bool DataGridViewRowOrder::switch_to_edit() {
    if (mode_ == DataGridViewRowOrderMode::edit || !source_) return mode_ == DataGridViewRowOrderMode::edit;
    const std::size_t rows = (*source_).row_count();
    std::vector<DataGridViewRowId> fetched(rows);
    if (rows > 0U) {
        (*source_).row_ids(DataGridViewRowPosition{0U}, std::span<DataGridViewRowId>(fetched));
        ++statistics_.window_requests;
        statistics_.ids_requested += rows;
    }
    std::unordered_set<std::string_view> seen{};
    seen.reserve(rows);
    for (const DataGridViewRowId& id : fetched) {
        if (!valid_id(id.value) || !seen.insert(id.value).second) return false;
    }
    std::vector<std::string> ids{};
    ids.reserve(rows);
    for (DataGridViewRowId& id : fetched) ids.push_back(std::move(id.value));
    forget_window();
    previous_window_.clear();
    source_.reset();
    window_count_ = 0U;
    build_chunks(std::move(ids));
    mode_ = DataGridViewRowOrderMode::edit;
    ++statistics_.mode_switches;
    return true;
}

void DataGridViewRowOrder::switch_to_window(std::shared_ptr<DataGridViewRowWindowSource> source) {
    if (!source) throw std::invalid_argument("DataGridView window mode requires a row window source");
    const bool switching = mode_ != DataGridViewRowOrderMode::window;
    chunks_.clear();
    chunk_of_.clear();
    chunk_slot_.clear();
    fenwick_.clear();
    edit_count_ = 0U;
    next_serial_ = 0U;
    forget_window();
    previous_window_.clear();
    source_ = std::move(source);
    window_count_ = (*source_).row_count();
    mode_ = DataGridViewRowOrderMode::window;
    if (switching) ++statistics_.mode_switches;
}

void DataGridViewRowOrder::clear() noexcept {
    chunks_.clear();
    chunk_of_.clear();
    chunk_slot_.clear();
    fenwick_.clear();
    edit_count_ = 0U;
    next_serial_ = 0U;
    source_.reset();
    window_count_ = 0U;
    window_first_ = 0U;
    window_ids_.clear();
    window_index_.clear();
    previous_window_.clear();
    mode_ = DataGridViewRowOrderMode::edit;
}

// -- lookups ---------------------------------------------------------------------------

std::string DataGridViewRowOrder::id_at(std::size_t position) const {
    if (mode_ == DataGridViewRowOrderMode::edit) {
        std::size_t chunk = 0U;
        std::size_t offset = 0U;
        locate(position, chunk, offset);
        const std::string id = chunks_[chunk].ids[offset];
        return id;
    }
    if (position >= window_first_ && position < window_end()) {
        const std::string id = window_ids_[position - window_first_].value;
        return id;
    }
    // Outside the window: one ID from the source.
    std::array<DataGridViewRowId, 1> one{};
    (*source_).row_ids(DataGridViewRowPosition{position}, std::span<DataGridViewRowId>(one));
    ++statistics_.window_requests;
    ++statistics_.ids_requested;
    const std::string id = std::move(one[0].value);
    return id;
}

std::optional<std::size_t> DataGridViewRowOrder::position_of(std::string_view id) const {
    if (mode_ == DataGridViewRowOrderMode::edit) {
        const ChunkIndex::const_iterator found = chunk_of_.find(id);
        if (found == chunk_of_.end()) return std::nullopt;
        const std::size_t chunk = chunk_slot_[(*found).second];
        const std::vector<std::string>& ids = chunks_[chunk].ids;
        const std::vector<std::string>::const_iterator at = std::find(ids.begin(), ids.end(), id);
        const std::size_t position = fenwick_prefix(chunk) + static_cast<std::size_t>(at - ids.begin());
        return position;
    }
    const WindowIndex::const_iterator held = window_index_.find(id);
    if (held != window_index_.end()) return (*held).second;
    if (!source_) return std::nullopt;
    ++statistics_.position_queries;
    const DataGridViewRowId identity{std::string(id)};
    const std::optional<DataGridViewRowPosition> position = (*source_).row_position(identity);
    if (!position || (*position).value >= window_count_) return std::nullopt;
    return (*position).value;
}

std::optional<std::size_t> DataGridViewRowOrder::held_position(std::string_view id) const {
    if (mode_ == DataGridViewRowOrderMode::edit) return position_of(id);
    const WindowIndex::const_iterator held = window_index_.find(id);
    if (held != window_index_.end()) return (*held).second;
    const WindowIndex::const_iterator before = previous_window_.find(id);
    if (before != previous_window_.end()) return (*before).second;
    return std::nullopt;
}

std::optional<std::string> DataGridViewRowOrder::held_id_at(std::size_t position) const {
    if (position >= count()) return std::nullopt;
    if (mode_ == DataGridViewRowOrderMode::edit) {
        std::optional<std::string> id = id_at(position);
        return id;
    }
    if (position < window_first_ || position >= window_end()) return std::nullopt;
    std::optional<std::string> id = window_ids_[position - window_first_].value;
    return id;
}

bool DataGridViewRowOrder::contains(std::string_view id) const {
    const bool held = mode_ == DataGridViewRowOrderMode::edit && chunk_of_.contains(id);
    return held;
}

// -- the window ----------------------------------------------------------------------------

void DataGridViewRowOrder::forget_window() noexcept {
    // The last window that held anything is kept for held_position.
    if (!window_index_.empty()) previous_window_.swap(window_index_);
    window_index_.clear();
    window_ids_.clear();
    window_first_ = 0U;
}

void DataGridViewRowOrder::fetch_window(std::size_t first, std::size_t end) {
    window_ids_.assign(end - first, DataGridViewRowId{});
    if (end > first) {
        (*source_).row_ids(DataGridViewRowPosition{first}, std::span<DataGridViewRowId>(window_ids_));
        ++statistics_.window_requests;
        statistics_.ids_requested += end - first;
    }
    window_first_ = first;
    window_index_.clear();
    window_index_.reserve(window_ids_.size());
    for (std::size_t index = 0; index < window_ids_.size(); ++index) {
        window_index_.emplace(window_ids_[index].value, first + index);
    }
}

void DataGridViewRowOrder::prepare_window(std::size_t first, std::size_t end, std::size_t margin) {
    if (mode_ != DataGridViewRowOrderMode::window || !source_) return;
    const std::size_t rows = window_count_;
    const std::size_t clamped_end = std::min(end, rows);
    const std::size_t clamped_first = std::min(first, clamped_end);
    const bool held = !window_ids_.empty() && clamped_first >= window_first_ && clamped_end <= window_end();
    if (held || (clamped_first == clamped_end && !window_ids_.empty())) return;
    const std::size_t wanted_first = clamped_first > margin ? clamped_first - margin : 0U;
    const std::size_t wanted_end = rows - clamped_end > margin ? clamped_end + margin : rows;
    fetch_window(wanted_first, wanted_end);
}

// -- reported changes ------------------------------------------------------------------------

void DataGridViewRowOrder::validate_new_ids(std::span<const DataGridViewRowId> ids) const {
    std::unordered_set<std::string_view> seen{};
    seen.reserve(ids.size());
    for (const DataGridViewRowId& id : ids) {
        if (!valid_id(id.value)) {
            throw std::invalid_argument("DataGridView virtual row IDs must follow the StableId segment rule");
        }
        if (!seen.insert(id.value).second || chunk_of_.contains(id.value)) {
            throw std::invalid_argument("DataGridView virtual row IDs must be unique");
        }
    }
}

void DataGridViewRowOrder::insert_one(std::size_t position, const std::string& id) {
    if (chunks_.empty()) {
        Chunk chunk{};
        chunk.serial = new_serial();
        chunk.ids.reserve(split_above + 1U);
        chunks_.push_back(std::move(chunk));
        rebuild_chunk_table();
    }
    std::size_t chunk = chunks_.size() - 1U;
    std::size_t offset = chunks_.back().ids.size();
    if (position < edit_count_) locate(position, chunk, offset);
    std::vector<std::string>& ids = chunks_[chunk].ids;
    statistics_.slot_moves += ids.size() - offset;
    ids.insert(ids.begin() + static_cast<std::ptrdiff_t>(offset), id);
    chunk_of_.insert_or_assign(id, chunks_[chunk].serial);
    ++statistics_.index_writes;
    fenwick_add(chunk, 1);
    ++edit_count_;
    if (ids.size() > split_above) split_chunk(chunk);
}

// Moves the upper half of a chunk into a new chunk after it.
void DataGridViewRowOrder::split_chunk(std::size_t chunk) {
    std::vector<std::string>& ids = chunks_[chunk].ids;
    const std::size_t half = ids.size() / 2U;
    Chunk upper{};
    upper.serial = new_serial();
    upper.ids.reserve(split_above + 1U);
    for (std::size_t index = half; index < ids.size(); ++index) {
        chunk_of_.insert_or_assign(ids[index], upper.serial);
        upper.ids.push_back(std::move(ids[index]));
    }
    statistics_.index_writes += upper.ids.size();
    statistics_.slot_moves += upper.ids.size();
    ids.resize(half);
    chunks_.insert(chunks_.begin() + static_cast<std::ptrdiff_t>(chunk + 1U), std::move(upper));
    rebuild_chunk_table();
}

void DataGridViewRowOrder::remove_one(std::string_view id) {
    const ChunkIndex::const_iterator found = chunk_of_.find(id);
    const std::size_t chunk = chunk_slot_[(*found).second];
    std::vector<std::string>& ids = chunks_[chunk].ids;
    const std::vector<std::string>::iterator at = std::find(ids.begin(), ids.end(), id);
    statistics_.slot_moves += static_cast<std::size_t>(ids.end() - at) - 1U;
    chunk_of_.erase(found);
    ids.erase(at);
    ++statistics_.index_writes;
    fenwick_add(chunk, -1);
    --edit_count_;
    after_removal(chunk);
}

// An emptied chunk goes; a small one merges into a neighbour when both fit.
void DataGridViewRowOrder::after_removal(std::size_t chunk) {
    std::vector<std::string>& ids = chunks_[chunk].ids;
    if (ids.empty()) {
        chunks_.erase(chunks_.begin() + static_cast<std::ptrdiff_t>(chunk));
        rebuild_chunk_table();
        return;
    }
    if (ids.size() >= merge_below) return;
    std::size_t into = chunks_.size();
    if (chunk + 1U < chunks_.size() && ids.size() + chunks_[chunk + 1U].ids.size() <= chunk_capacity) into = chunk + 1U;
    if (into == chunks_.size() && chunk > 0U && ids.size() + chunks_[chunk - 1U].ids.size() <= chunk_capacity) {
        into = chunk - 1U;
    }
    if (into == chunks_.size()) return;
    // The two chunks become the lower one, in order.
    const std::size_t lower = std::min(chunk, into);
    const std::size_t upper = std::max(chunk, into);
    std::vector<std::string>& kept = chunks_[lower].ids;
    std::vector<std::string>& gone = chunks_[upper].ids;
    for (std::string& moved : gone) {
        chunk_of_.insert_or_assign(moved, chunks_[lower].serial);
        kept.push_back(std::move(moved));
    }
    statistics_.index_writes += gone.size();
    statistics_.slot_moves += gone.size();
    chunks_.erase(chunks_.begin() + static_cast<std::ptrdiff_t>(upper));
    rebuild_chunk_table();
}

void DataGridViewRowOrder::insert(std::size_t position, std::span<const DataGridViewRowId> ids) {
    if (mode_ == DataGridViewRowOrderMode::window) {
        statistics_.reported_changes += ids.size();
        forget_window();
        window_count_ = (*source_).row_count();
        return;
    }
    if (position > edit_count_) throw std::out_of_range("DataGridView virtual row insertion position is past the end");
    validate_new_ids(ids);
    statistics_.reported_changes += ids.size();
    for (std::size_t index = 0; index < ids.size(); ++index) insert_one(position + index, ids[index].value);
}

void DataGridViewRowOrder::remove(std::span<const DataGridViewRowId> ids) {
    if (mode_ == DataGridViewRowOrderMode::window) {
        statistics_.reported_changes += ids.size();
        forget_window();
        window_count_ = (*source_).row_count();
        return;
    }
    std::unordered_set<std::string_view> seen{};
    seen.reserve(ids.size());
    for (const DataGridViewRowId& id : ids) {
        if (!chunk_of_.contains(id.value) || !seen.insert(id.value).second) {
            throw std::invalid_argument("DataGridView removed virtual rows must be present, each once");
        }
    }
    statistics_.reported_changes += ids.size();
    for (const DataGridViewRowId& id : ids) remove_one(id.value);
}

void DataGridViewRowOrder::move(std::string_view id, std::size_t position) {
    if (mode_ == DataGridViewRowOrderMode::window) {
        ++statistics_.reported_changes;
        forget_window();
        window_count_ = (*source_).row_count();
        return;
    }
    if (!chunk_of_.contains(id)) throw std::invalid_argument("DataGridView moved virtual row must be present");
    if (position >= edit_count_) throw std::out_of_range("DataGridView virtual row position is past the end");
    ++statistics_.reported_changes;
    const std::string moved(id);
    remove_one(moved);
    insert_one(position, moved);
}

} // namespace gui_forms
