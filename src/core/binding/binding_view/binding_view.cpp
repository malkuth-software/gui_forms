// The view index. See binding_view.hpp.

#include "binding_view.hpp"

#include <algorithm>
#include <bit>
#include <functional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace gui_forms::detail {
namespace {

// Text arena compaction: when more than half is dead and at least this much.
constexpr std::size_t compaction_floor = 64U * 1024U;

} // namespace

BindingView::BindingView(std::vector<BindingViewKey> keys, std::vector<BindingViewPredicate> filter)
    : keys_(std::move(keys)), filter_(std::move(filter)) {
    // Each distinct column is read once per row.
    for (const BindingViewKey& key : keys_) {
        std::vector<std::size_t>::iterator found = std::find(used_columns_.begin(), used_columns_.end(), key.column);
        if (found == used_columns_.end()) {
            used_columns_.push_back(key.column);
            found = used_columns_.end() - 1;
        }
        key_value_.push_back(static_cast<std::size_t>(found - used_columns_.begin()));
    }
    for (const BindingViewPredicate& predicate : filter_) {
        std::vector<std::size_t>::iterator found =
            std::find(used_columns_.begin(), used_columns_.end(), predicate.column);
        if (found == used_columns_.end()) {
            used_columns_.push_back(predicate.column);
            found = used_columns_.end() - 1;
        }
        predicate_value_.push_back(static_cast<std::size_t>(found - used_columns_.begin()));
    }
    scratch_.resize(used_columns_.size());
    scratch_valid_.resize(used_columns_.size());
}

bool BindingView::tie_before(std::size_t left_position, std::size_t right_position) noexcept {
    return left_position < right_position;
}

BindingKeyView BindingView::key_view(const KeyCell& cell) const noexcept {
    BindingKeyView view;
    view.kind = cell.kind;
    switch (cell.kind) {
    case BindingValueKind::boolean:
        view.boolean = cell.bits != 0U;
        break;
    case BindingValueKind::signed_integer:
        view.signed_value = std::bit_cast<std::int64_t>(cell.bits);
        break;
    case BindingValueKind::unsigned_integer:
        view.unsigned_value = cell.bits;
        break;
    case BindingValueKind::number:
        view.number = std::bit_cast<double>(cell.bits);
        break;
    case BindingValueKind::text:
        view.text = std::string_view(text_).substr(static_cast<std::size_t>(cell.text_offset), cell.text_size);
        break;
    default:
        break;
    }
    return view;
}

int BindingView::compare_keys(std::uint32_t left, std::uint32_t right) const noexcept {
    const std::size_t width = keys_.size();
    for (std::size_t key = 0U; key < width; ++key) {
        const KeyCell& left_cell = cells_[static_cast<std::size_t>(left) * width + key];
        const KeyCell& right_cell = cells_[static_cast<std::size_t>(right) * width + key];
        // A cell without a valid answer sorts last in either direction.
        if (left_cell.error != right_cell.error) {
            return left_cell.error ? 1 : -1;
        }
        if (left_cell.error) {
            continue;
        }
        int order = compare_binding_key_views(key_view(left_cell), key_view(right_cell));
        if (keys_[key].descending) {
            order = -order;
        }
        if (order != 0) {
            return order;
        }
    }
    return 0;
}

bool BindingView::ViewOrder::before(std::uint32_t left, std::uint32_t right) const {
    const int order = (*view).compare_keys(left, right);
    if (order != 0) {
        return order < 0;
    }
    const bool earlier = tie_before((*view).source_order_.rank(left), (*view).source_order_.rank(right));
    return earlier;
}

std::uint32_t BindingView::allocate_slot() {
    if (!free_slots_.empty()) {
        const std::uint32_t slot = free_slots_.back();
        free_slots_.pop_back();
        live_[slot] = 1U;
        return slot;
    }
    if (live_.size() >= binding_maximum_rows) {
        throw std::length_error("GUI.Forms binding view is past binding_maximum_rows");
    }
    const std::uint32_t slot = static_cast<std::uint32_t>(live_.size());
    cells_.resize(cells_.size() + keys_.size());
    member_.push_back(0U);
    live_.push_back(1U);
    return slot;
}

void BindingView::drop_keys(std::uint32_t slot) noexcept {
    const std::size_t width = keys_.size();
    for (std::size_t key = 0U; key < width; ++key) {
        KeyCell& cell = cells_[static_cast<std::size_t>(slot) * width + key];
        dead_text_ = dead_text_ + cell.text_size;
        cell = KeyCell{};
    }
}

void BindingView::release_slot(std::uint32_t slot) noexcept {
    drop_keys(slot);
    member_[slot] = 0U;
    live_[slot] = 0U;
    free_slots_.push_back(slot);
}

void BindingView::store_key(KeyCell& cell, const BindingValue& value, bool valid) {
    cell = KeyCell{};
    if (!valid) {
        cell.error = true;
        return;
    }
    cell.kind = binding_value_kind(value);
    if (const bool* boolean = std::get_if<bool>(&value)) {
        cell.bits = *boolean ? 1U : 0U;
    } else if (const std::int64_t* signed_value = std::get_if<std::int64_t>(&value)) {
        cell.bits = std::bit_cast<std::uint64_t>(*signed_value);
    } else if (const std::uint64_t* unsigned_value = std::get_if<std::uint64_t>(&value)) {
        cell.bits = *unsigned_value;
    } else if (const double* number = std::get_if<double>(&value)) {
        cell.bits = std::bit_cast<std::uint64_t>(*number);
    } else if (const std::string* text = std::get_if<std::string>(&value)) {
        // The source has bounded text to binding_maximum_text_bytes.
        cell.text_offset = text_.size();
        cell.text_size = static_cast<std::uint32_t>(std::min((*text).size(), binding_maximum_text_bytes));
        text_.append((*text).data(), cell.text_size);
    }
}

void BindingView::load_slot(std::uint32_t slot, std::size_t row, BindingKeyReader& reader) {
    for (std::size_t index = 0U; index < used_columns_.size(); ++index) {
        BindingValue& value = scratch_[index];
        value = BindingValue{};
        scratch_valid_[index] = reader.read_key(row, used_columns_[index], value) ? 1U : 0U;
        if (scratch_valid_[index] == 0U) {
            value = BindingValue{};
        }
    }
    drop_keys(slot);
    const std::size_t width = keys_.size();
    for (std::size_t key = 0U; key < width; ++key) {
        const std::size_t source = key_value_[key];
        store_key(cells_[static_cast<std::size_t>(slot) * width + key], scratch_[source], scratch_valid_[source] != 0U);
    }
    // A cell without a valid answer is filtered as null (scratch_ holds it).
    bool admitted = true;
    for (std::size_t index = 0U; index < filter_.size() && admitted; ++index) {
        admitted = binding_predicate_holds(filter_[index].predicate, scratch_[predicate_value_[index]]);
    }
    member_[slot] = admitted ? 1U : 0U;
}

void BindingView::build(std::size_t row_count, BindingKeyReader& reader) {
    if (row_count > binding_maximum_rows) {
        throw std::length_error("GUI.Forms binding view is past binding_maximum_rows");
    }
    cells_.assign(row_count * keys_.size(), KeyCell{});
    member_.assign(row_count, 0U);
    live_.assign(row_count, 1U);
    free_slots_.clear();
    text_.clear();
    dead_text_ = 0U;
    erasures_since_build_ = 0U;
    // Slots are numbered by source row, so the source order is the identity
    // and the build's tie-break is the slot number.
    std::vector<std::uint32_t> identity(row_count);
    for (std::size_t row = 0U; row < row_count; ++row) {
        identity[row] = static_cast<std::uint32_t>(row);
        load_slot(static_cast<std::uint32_t>(row), row, reader);
    }
    std::vector<std::uint32_t> shown;
    shown.reserve(row_count);
    for (const std::uint32_t slot : identity) {
        if (member_[slot] != 0U) {
            shown.push_back(slot);
        }
    }
    const BindingView* self = this;
    struct SlotBefore final {
        const BindingView* view{};
        bool operator()(std::uint32_t left, std::uint32_t right) const noexcept {
            const int order = (*view).compare_keys(left, right);
            if (order != 0) {
                return order < 0;
            }
            return tie_before(left, right);
        }
    };
    std::sort(shown.begin(), shown.end(), SlotBefore{self});
    source_order_.assign(identity, row_count);
    view_order_.assign(shown, row_count);
}

std::size_t BindingView::row_of(std::size_t position) const {
    const std::uint32_t slot = view_order_.select(position);
    const std::size_t row = source_order_.rank(slot);
    return row;
}

std::optional<std::size_t> BindingView::position_of(std::size_t row) const {
    const std::uint32_t slot = source_order_.select(row);
    if (member_[slot] == 0U) {
        return std::nullopt;
    }
    const std::size_t position = view_order_.rank(slot);
    return position;
}

bool BindingView::uses_column(std::size_t column) const noexcept {
    const bool used = std::find(used_columns_.begin(), used_columns_.end(), column) != used_columns_.end();
    return used;
}

bool BindingView::wants_rebuild() const noexcept {
    // Partly filled nodes left by erasures: rebuild when the erasures since
    // the build pass half the rows and the trees hold more than twice the
    // nodes a fresh build would.
    const std::size_t rows = source_order_.size();
    const std::size_t fresh = 2U * (rows / (BindingOrderTree::node_capacity / 2U) + 1U);
    const bool sparse = erasures_since_build_ > rows / 2U && node_count() > 2U * fresh;
    return sparse;
}

void BindingView::insert_rows(std::size_t row, std::size_t count, BindingKeyReader& reader,
                              std::vector<std::size_t>& added) {
    std::vector<std::uint32_t> slots;
    slots.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const std::uint32_t slot = allocate_slot();
        source_order_.insert_at(row + index, slot);
        slots.push_back(slot);
    }
    const ViewOrder order{this};
    for (std::size_t index = 0U; index < count; ++index) {
        load_slot(slots[index], row + index, reader);
        if (member_[slots[index]] != 0U) {
            view_order_.insert_ordered(slots[index], order);
        }
    }
    const std::size_t first_added = added.size();
    for (const std::uint32_t slot : slots) {
        if (member_[slot] != 0U) {
            added.push_back(view_order_.rank(slot));
        }
    }
    std::sort(added.begin() + static_cast<std::ptrdiff_t>(first_added), added.end());
    compact_text();
}

void BindingView::remove_rows(std::size_t row, std::size_t count, std::vector<std::size_t>& removed) {
    std::vector<std::uint32_t> slots;
    slots.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        slots.push_back(source_order_.select(row + index));
    }
    const std::size_t first_removed = removed.size();
    for (const std::uint32_t slot : slots) {
        if (member_[slot] != 0U) {
            removed.push_back(view_order_.rank(slot));
        }
    }
    std::sort(removed.begin() + static_cast<std::ptrdiff_t>(first_removed), removed.end(),
              std::greater<std::size_t>{});
    for (const std::uint32_t slot : slots) {
        if (member_[slot] != 0U) {
            view_order_.erase(slot);
        }
        source_order_.erase(slot);
        release_slot(slot);
    }
    erasures_since_build_ = erasures_since_build_ + count;
    compact_text();
}

void append_block_move_steps(std::size_t row, std::size_t count, std::size_t target,
                             std::vector<BindingRowMove>& steps) {
    if (target < row) {
        for (std::size_t index = 0U; index < count; ++index) {
            steps.push_back(BindingRowMove{row + index, target + index});
        }
    } else if (target > row) {
        const std::size_t last = target + count - 1U;
        for (std::size_t index = 0U; index < count; ++index) {
            steps.push_back(BindingRowMove{row, last});
        }
    }
}

void BindingView::move_rows(std::size_t row, std::size_t count, std::size_t target,
                            std::vector<BindingRowMove>& moves) {
    std::vector<BindingRowMove> steps;
    steps.reserve(count);
    append_block_move_steps(row, count, target, steps);
    // One source row at a time, so the view stays consistent with the
    // source order (its tie-break) after every step. Keys are unchanged.
    const ViewOrder order{this};
    for (const BindingRowMove& step : steps) {
        const std::uint32_t slot = source_order_.select(step.from);
        const bool shown = member_[slot] != 0U;
        std::size_t before = 0U;
        if (shown) {
            before = view_order_.rank(slot);
        }
        source_order_.erase(slot);
        source_order_.insert_at(step.to, slot);
        if (shown) {
            view_order_.erase(slot);
            view_order_.insert_ordered(slot, order);
            const std::size_t after = view_order_.rank(slot);
            if (after != before) {
                moves.push_back(BindingRowMove{before, after});
            }
        }
    }
    erasures_since_build_ = erasures_since_build_ + steps.size();
}

BindingView::RowChange BindingView::change_row(std::size_t row, std::size_t column, BindingKeyReader& reader) {
    RowChange change;
    const std::uint32_t slot = source_order_.select(row);
    if (member_[slot] != 0U) {
        change.before = view_order_.rank(slot);
    }
    if (column != binding_whole_row && !uses_column(column)) {
        change.after = change.before;
        return change;
    }
    if (member_[slot] != 0U) {
        view_order_.erase(slot);
    }
    load_slot(slot, row, reader);
    if (member_[slot] != 0U) {
        const ViewOrder order{this};
        view_order_.insert_ordered(slot, order);
        change.after = view_order_.rank(slot);
    }
    compact_text();
    return change;
}

void BindingView::compact_text() {
    if (dead_text_ < compaction_floor || dead_text_ * 2U < text_.size()) {
        return;
    }
    std::string packed;
    packed.reserve(text_.size() - dead_text_);
    for (KeyCell& cell : cells_) {
        if (cell.kind == BindingValueKind::text && !cell.error) {
            const std::uint64_t offset = packed.size();
            packed.append(text_, static_cast<std::size_t>(cell.text_offset), cell.text_size);
            cell.text_offset = offset;
        }
    }
    text_ = std::move(packed);
    dead_text_ = 0U;
}

std::size_t BindingView::node_count() const noexcept {
    const std::size_t nodes = source_order_.node_count() + view_order_.node_count();
    return nodes;
}

} // namespace gui_forms::detail
