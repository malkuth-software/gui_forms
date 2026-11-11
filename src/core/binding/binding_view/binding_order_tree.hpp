#pragma once

#include "../../collections/order_statistic_tree.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::detail {

// The entries of a BindingOrderTree: 32-bit elements (the view index's
// slots), each a row of its own, each node summarised by its subtree's last
// element so an ordered insertion can descend by comparing with it. Leaves
// are tracked, so rank and erase start at the element and walk up.
struct BindingOrderElements final {
    using Entry = std::uint32_t;
    using Summary = std::uint32_t;
    static constexpr bool tracks_leaves = true;

    [[nodiscard]] static std::size_t entry_count(std::uint32_t) noexcept { return 1U; }
    [[nodiscard]] static std::uint32_t entry_key(std::uint32_t element) noexcept { return element; }
    [[nodiscard]] static std::uint32_t summarize_entries(std::span<const std::uint32_t> elements) noexcept {
        const std::uint32_t last = elements.empty() ? 0U : elements.back();
        return last;
    }
    [[nodiscard]] static std::uint32_t summarize_children(std::span<const std::uint32_t> lasts) noexcept {
        const std::uint32_t last = lasts.empty() ? 0U : lasts.back();
        return last;
    }
};

// An order-statistic sequence of 32-bit elements, each present at most once,
// over the shared B-tree family (order_statistic_tree.hpp, which states the
// node layout and the depth bound):
//
//   select(position), rank(element)        O(log n) node visits, each
//                                          scanning at most node_capacity
//   insert_at, insert_ordered, erase        the same, plus a split per full
//                                          node on the path
//   assign (bulk load)                      O(n)
//
// Partly filled nodes are not merged, so the node count is bounded by the
// elements plus the erasures since the last assign; the holders (BindingView,
// DataGridViewBoundIndex) rebuild with assign when that grows. One thread;
// the value is owned by its holder.
class BindingOrderTree final {
public:
    using Tree = OrderStatisticTree<BindingOrderElements>;
    static constexpr std::size_t node_capacity = Tree::node_capacity;

    BindingOrderTree();
    ~BindingOrderTree();
    BindingOrderTree(const BindingOrderTree&) = delete;
    BindingOrderTree& operator=(const BindingOrderTree&) = delete;

    // Replaces the sequence with elements, in that order. element_end is
    // past every element that may be inserted later (the leaf table's size).
    void assign(std::span<const std::uint32_t> elements, std::size_t element_end);
    void clear() noexcept;

    [[nodiscard]] std::size_t size() const noexcept;
    // The element at position (below size()).
    [[nodiscard]] std::uint32_t select(std::size_t position) const;
    // The position of element, which must be present.
    [[nodiscard]] std::size_t rank(std::uint32_t element) const;
    [[nodiscard]] bool contains(std::uint32_t element) const noexcept;

    // Inserts element (not present) at position (at most size()).
    void insert_at(std::size_t position, std::uint32_t element);
    // Inserts element before the first element e for which
    // order.before(element, e), or at the end. order is a strict total
    // order consistent with the sequence.
    template <typename Order>
    void insert_ordered(std::uint32_t element, const Order& order);
    // Removes element, which must be present.
    void erase(std::uint32_t element);

    // Storage, for bounded-memory checks: nodes held (O(1)) and levels.
    [[nodiscard]] std::size_t node_count() const noexcept;
    [[nodiscard]] std::size_t depth() const noexcept;

private:
    Tree tree_;
};

template <typename Order>
void BindingOrderTree::insert_ordered(std::uint32_t element, const Order& order) {
    Tree::Node* node = tree_.root();
    while (!(*node).leaf) {
        const std::size_t children = (*node).children.size();
        std::size_t chosen = children - 1U;
        for (std::size_t index = 0U; index < children; ++index) {
            if (order.before(element, (*node).child_summaries[index])) {
                chosen = index;
                break;
            }
        }
        node = (*node).children[chosen].get();
    }
    std::size_t index = (*node).entries.size();
    for (std::size_t candidate = 0U; candidate < (*node).entries.size(); ++candidate) {
        if (order.before(element, (*node).entries[candidate])) {
            index = candidate;
            break;
        }
    }
    (*node).entries.insert((*node).entries.begin() + static_cast<std::ptrdiff_t>(index), element);
    tree_.settle(node);
}

} // namespace gui_forms::detail
