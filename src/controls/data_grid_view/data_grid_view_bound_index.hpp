#pragma once

// DataGridView's identity index over a binding source's rows (audit N31
// batch 2): each row's stable ID to its position, kept with the source's
// list changes instead of rebuilt for each one.
//
// A row has a slot, the index's own identity for it, which stays the same
// while rows around it are added, removed or moved. A hash maps an ID to
// its slot and an order-statistic tree (BindingOrderTree) holds the slots
// in row order, so
//
//   find, contains                 one hash probe, plus O(log n) for the
//                                  position (rank)
//   insert, erase, move            O(log n) plus the hash update
//   assign                         O(n)
//
// A position cannot be held per ID in O(1) under insertions, since one
// insertion moves every position after it; the tree turns that O(n) into
// O(log n). An ID held by two rows (a provider that repeats one) maps to
// the slot that took it first; once that row goes, the other is found
// again at the next assign. The tree does not merge partly filled nodes, so
// erasures since the last assign are counted and the index reassigns
// itself when they pass the rows held plus a tree node's capacity.
//
// Owned by its DataGridView (one UI thread); the grid checks each position
// it finds against the source, so an index the source got ahead of (list
// changes not raised) finds nothing wrong, only nothing.

#include "../../core/binding/binding_view/binding_order_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gui_forms::detail {

class DataGridViewBoundIndex final {
public:
    DataGridViewBoundIndex();
    ~DataGridViewBoundIndex();
    DataGridViewBoundIndex(const DataGridViewBoundIndex&) = delete;
    DataGridViewBoundIndex& operator=(const DataGridViewBoundIndex&) = delete;

    // Replaces every row with ids, in row order.
    void assign(std::span<const std::string> ids);
    void clear() noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return order_.size(); }

    // A row with id now stands at position (at most size()); false,
    // changing nothing, past the end.
    bool insert(std::size_t position, std::string_view id);
    // The row at position (below size()) is gone; false past the end.
    bool erase(std::size_t position);
    // The row at from now stands at to (both below size()); false past the
    // end.
    bool move(std::size_t from, std::size_t to);

    [[nodiscard]] std::optional<std::size_t> find(std::string_view id) const;
    [[nodiscard]] bool contains(std::string_view id) const;
    // The ID of the row at position (below size()).
    [[nodiscard]] const std::string& id_at(std::size_t position) const;

private:
    struct IdHash final {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(std::string_view id) const noexcept {
            const std::size_t result = std::hash<std::string_view>{}(id);
            return result;
        }
    };
    using SlotIndex = std::unordered_map<std::string, std::uint32_t, IdHash, std::equal_to<>>;

    [[nodiscard]] std::uint32_t take_slot(std::string_view id);
    void reassign_from_tree();

    BindingOrderTree order_;
    // Each slot's ID, by slot; a free slot holds an empty text.
    std::vector<std::string> slot_ids_;
    std::vector<std::uint32_t> free_slots_;
    SlotIndex slot_of_;
    std::size_t erasures_since_assign_{};
};

} // namespace gui_forms::detail
