// DataGridView's identity index over bound rows. See
// data_grid_view_bound_index.hpp.

#include "data_grid_view_bound_index.hpp"

#include <stdexcept>
#include <utility>

namespace gui_forms::detail {

DataGridViewBoundIndex::DataGridViewBoundIndex() = default;

DataGridViewBoundIndex::~DataGridViewBoundIndex() = default;

void DataGridViewBoundIndex::assign(std::span<const std::string> ids) {
    std::vector<std::string> held(ids.begin(), ids.end());
    SlotIndex mapped;
    mapped.reserve(held.size());
    std::vector<std::uint32_t> slots;
    slots.reserve(held.size());
    for (std::size_t slot = 0U; slot < held.size(); ++slot) {
        static_cast<void>(mapped.emplace(held[slot], static_cast<std::uint32_t>(slot)));
        slots.push_back(static_cast<std::uint32_t>(slot));
    }
    order_.assign(slots, held.size());
    slot_ids_ = std::move(held);
    slot_of_ = std::move(mapped);
    free_slots_.clear();
    erasures_since_assign_ = 0U;
}

void DataGridViewBoundIndex::clear() noexcept {
    order_.clear();
    slot_ids_.clear();
    free_slots_.clear();
    slot_of_.clear();
    erasures_since_assign_ = 0U;
}

std::uint32_t DataGridViewBoundIndex::take_slot(std::string_view id) {
    std::uint32_t slot = 0U;
    if (free_slots_.empty()) {
        if (slot_ids_.size() >= static_cast<std::size_t>(UINT32_MAX)) {
            throw std::length_error("GUI.Forms DataGridView bound index holds at most 2^32 - 1 rows");
        }
        slot = static_cast<std::uint32_t>(slot_ids_.size());
        slot_ids_.emplace_back(id);
    } else {
        slot = free_slots_.back();
        free_slots_.pop_back();
        slot_ids_[slot] = std::string(id);
    }
    return slot;
}

bool DataGridViewBoundIndex::insert(std::size_t position, std::string_view id) {
    if (position > order_.size()) {
        return false;
    }
    const std::uint32_t slot = take_slot(id);
    order_.insert_at(position, slot);
    static_cast<void>(slot_of_.emplace(slot_ids_[slot], slot));
    return true;
}

bool DataGridViewBoundIndex::erase(std::size_t position) {
    if (position >= order_.size()) {
        return false;
    }
    const std::uint32_t slot = order_.select(position);
    order_.erase(slot);
    const SlotIndex::iterator mapped = slot_of_.find(slot_ids_[slot]);
    if (mapped != slot_of_.end() && (*mapped).second == slot) {
        slot_of_.erase(mapped);
    }
    slot_ids_[slot].clear();
    free_slots_.push_back(slot);
    ++erasures_since_assign_;
    if (erasures_since_assign_ > order_.size() + BindingOrderTree::node_capacity) {
        reassign_from_tree();
    }
    return true;
}

bool DataGridViewBoundIndex::move(std::size_t from, std::size_t to) {
    const std::size_t rows = order_.size();
    if (from >= rows || to >= rows) {
        return false;
    }
    if (from == to) {
        return true;
    }
    const std::uint32_t slot = order_.select(from);
    order_.erase(slot);
    order_.insert_at(to, slot);
    ++erasures_since_assign_;
    if (erasures_since_assign_ > order_.size() + BindingOrderTree::node_capacity) {
        reassign_from_tree();
    }
    return true;
}

std::optional<std::size_t> DataGridViewBoundIndex::find(std::string_view id) const {
    const SlotIndex::const_iterator mapped = slot_of_.find(id);
    if (mapped == slot_of_.end()) {
        return std::nullopt;
    }
    const std::size_t position = order_.rank((*mapped).second);
    return position;
}

bool DataGridViewBoundIndex::contains(std::string_view id) const {
    const bool present = slot_of_.contains(id);
    return present;
}

const std::string& DataGridViewBoundIndex::id_at(std::size_t position) const {
    const std::uint32_t slot = order_.select(position);
    return slot_ids_[slot];
}

// Packs the slots again in row order, so the tree's nodes are full. The
// IDs are copied out first: assign replaces slot_ids_.
void DataGridViewBoundIndex::reassign_from_tree() {
    const std::size_t rows = order_.size();
    std::vector<std::string> ids;
    ids.reserve(rows);
    for (std::size_t position = 0U; position < rows; ++position) {
        ids.push_back(slot_ids_[order_.select(position)]);
    }
    assign(ids);
}

} // namespace gui_forms::detail
