#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace gui_forms::detail {

// An order-statistic B-tree over a sequence of 32-bit elements (the view
// index's slots), each present at most once. Nodes keep their subtree's
// element count, so a position and an element convert in O(log n):
//
//   select(position), rank(element)        O(log n) node visits, each
//                                          scanning at most node_capacity
//   insert_at, insert_ordered, erase        the same, plus a split per full
//                                          node on the path
//   assign (bulk load)                      O(n)
//
// Each element's leaf is recorded (leaf_of_), so rank and erase start at the
// element and walk up. Branches keep their children's counts and each node
// its last element, so an ordered insertion descends by comparing with each
// child's last element. This is the node family of RowHeightIndex (audit
// N94, owner ruling D10 b) with counts in place of heights; the two meet
// when claude/cp28-controls lands.
//
// Emptied nodes are removed and a one-child root collapses; partly filled
// nodes are not merged, so the node count is bounded by the elements plus
// the erasures since the last assign. The owner (BindingView) rebuilds with
// assign when that grows past twice the full-tree size. One thread; the
// value is owned by its view.
class BindingOrderTree final {
public:
    static constexpr std::size_t node_capacity = 32U;

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

    // Storage, for bounded-memory checks.
    [[nodiscard]] std::size_t node_count() const noexcept;

private:
    struct Node final {
        Node* parent{};
        bool leaf{true};
        std::size_t count{};
        // The subtree's last element; meaningful when count > 0.
        std::uint32_t last{};
        // A leaf's elements, in order.
        std::vector<std::uint32_t> elements{};
        // A branch's children, in order, and each one's count.
        std::vector<std::unique_ptr<Node>> children{};
        std::vector<std::size_t> child_counts{};
    };

    void insert_into_leaf(Node* leaf, std::size_t index, std::uint32_t element);
    void split_overfull(Node* node);
    void refresh_upward(Node* node) noexcept;
    void remove_empty(Node* node);
    void collapse_root();
    void record_leaf(std::uint32_t element, Node* leaf);
    static void refresh_local(Node& node) noexcept;
    static std::size_t index_in_parent(const Node& node) noexcept;
    static std::size_t count_nodes(const Node& node) noexcept;

    std::unique_ptr<Node> root_;
    // The leaf holding each element, or null; indexed by element.
    std::vector<Node*> leaf_of_;
};

template <typename Order>
void BindingOrderTree::insert_ordered(std::uint32_t element, const Order& order) {
    Node* node = root_.get();
    while (!(*node).leaf) {
        const std::size_t children = (*node).children.size();
        std::size_t chosen = children - 1U;
        for (std::size_t index = 0U; index < children; ++index) {
            if (order.before(element, (*(*node).children[index]).last)) {
                chosen = index;
                break;
            }
        }
        node = (*node).children[chosen].get();
    }
    std::size_t index = (*node).elements.size();
    for (std::size_t candidate = 0U; candidate < (*node).elements.size(); ++candidate) {
        if (order.before(element, (*node).elements[candidate])) {
            index = candidate;
            break;
        }
    }
    insert_into_leaf(node, index, element);
}

} // namespace gui_forms::detail
