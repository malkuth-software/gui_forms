#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace gui_forms::detail {

// The order-statistic B-tree family (audit N94, owner ruling D10 b). One node
// shape and one set of node operations serve every sequence that needs a
// position in O(log n): the binding view's slot sequences (BindingOrderTree,
// used by BindingView and DataGridViewBoundIndex). The row extents of long
// lists (RowHeightIndex) are kept by stx_auxlib's stx_btree sequence tree
// instead. Each holder is a thin facade
// that keeps its own API and edits leaves; this tree keeps the counts, the
// summaries, the splits, the removals and the depth.
//
// Leaves hold entries and branches hold children, at most node_capacity each.
// Every node keeps its subtree's row count and a summary, and a branch repeats
// its children's counts and summaries so a descent reads one array. Totals
// are recomputed from the children on every path a change touches (no running
// deltas), so a floating summary does not accumulate rounding.
//
// Traits declares the entries:
//
//   using Entry          what a leaf holds (an element, a run of rows)
//   using Summary        what a node keeps besides its count
//   static constexpr bool tracks_leaves
//                        whether each entry's leaf is recorded, so a holder
//                        can start a rank at the entry (then entry_key gives
//                        each entry a distinct key below the key table's size)
//   static std::size_t entry_count(const Entry&) noexcept      rows, >= 1
//   static Summary summarize_entries(std::span<const Entry>) noexcept
//   static Summary summarize_children(std::span<const Summary>) noexcept
//   static std::uint32_t entry_key(const Entry&) noexcept      when tracked
//
// Depth. Nodes emptied by an erasure are removed and a one-child root
// collapses; partly filled nodes are not merged. All leaves are at one depth.
// After every change the tree enforces depth() <= depth_limit(count()): the
// levels a tree of count() rows needs at a fanout of split_fill (the fill a
// split leaves), plus one. A change that would leave the tree deeper compacts
// it, re-loading its entries bottom-up at bulk_fill, which gives at most
// depth_limit(count()) - 1 levels. So the depth never exceeds max_depth (17
// for a 64-bit size_t), whatever the sequence of edits, and a compaction is
// paid for by the growth or the erasures that made the tree that deep.
//
// No operation recurses: descents and climbs are loops, and leaves are walked
// and subtrees torn down through the parent links, without work storage.
//
// One thread; the tree is a value owned by its holder. Node addresses stay
// valid across a move of the tree, and across edits until settle, detach_child,
// assign, clear or compact, any of which may free or rebuild nodes.
template <typename Traits>
class OrderStatisticTree final {
public:
    using Entry = typename Traits::Entry;
    using Summary = typename Traits::Summary;

    static constexpr std::size_t node_capacity = 32U;
    // A bulk load fills nodes to three quarters, leaving room to insert.
    static constexpr std::size_t bulk_fill = node_capacity * 3U / 4U;
    // A split leaves at least this many entries or children on each side.
    static constexpr std::size_t split_fill = node_capacity / 2U;

    // The deepest a tree of `rows` rows may be: the levels it needs at
    // split_fill per node, plus one.
    [[nodiscard]] static constexpr std::size_t depth_limit(std::size_t rows) noexcept {
        std::size_t levels = 1U;
        std::size_t reach = split_fill;
        while (reach < rows) {
            ++levels;
            if (reach > std::numeric_limits<std::size_t>::max() / split_fill) {
                break;
            }
            reach = reach * split_fill;
        }
        const std::size_t limit = levels + 1U;
        return limit;
    }
    static constexpr std::size_t max_depth = depth_limit(std::numeric_limits<std::size_t>::max());
    static_assert(max_depth <= 17U, "the depth bound covers every size_t row count");

    struct Node final {
        Node* parent{};
        bool leaf{true};
        std::size_t count{};
        Summary summary{};
        // A leaf's entries, in order.
        std::vector<Entry> entries{};
        // A branch's children, in order, with each one's count and summary.
        std::vector<std::unique_ptr<Node>> children{};
        std::vector<std::size_t> child_counts{};
        std::vector<Summary> child_summaries{};
    };

    // Where a row falls: its leaf, the entry there, and the rows before it
    // within that entry. For an insertion point, entry may be past the last.
    struct Place final {
        Node* leaf{};
        std::size_t entry{};
        std::size_t within{};
    };

    OrderStatisticTree() : root_(), leaf_of_(), nodes_(0U), depth_(1U) { reset_empty(); }
    ~OrderStatisticTree() { release(std::move(root_)); }
    OrderStatisticTree(const OrderStatisticTree&) = delete;
    OrderStatisticTree& operator=(const OrderStatisticTree&) = delete;
    OrderStatisticTree(OrderStatisticTree&& other) noexcept
        : root_(std::move(other.root_)), leaf_of_(std::move(other.leaf_of_)), nodes_(other.nodes_),
          depth_(other.depth_) {
        other.reset_empty();
    }
    OrderStatisticTree& operator=(OrderStatisticTree&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        release(std::move(root_));
        root_ = std::move(other.root_);
        leaf_of_ = std::move(other.leaf_of_);
        nodes_ = other.nodes_;
        depth_ = other.depth_;
        other.reset_empty();
        return *this;
    }

    [[nodiscard]] std::size_t count() const noexcept { return (*root_).count; }
    [[nodiscard]] Summary summary() const noexcept { return (*root_).summary; }
    [[nodiscard]] Node* root() noexcept { return root_.get(); }
    [[nodiscard]] const Node* root() const noexcept { return root_.get(); }
    // Nodes held and levels, both O(1).
    [[nodiscard]] std::size_t node_count() const noexcept { return nodes_; }
    [[nodiscard]] std::size_t depth() const noexcept { return depth_; }

    // Replaces the sequence with entries, loaded bottom-up at bulk_fill.
    // key_end sizes the leaf table when leaves are tracked (past every key
    // that may be inserted later; it grows on demand).
    void assign(std::span<const Entry> entries, std::size_t key_end) {
        clear();
        if constexpr (Traits::tracks_leaves) {
            leaf_of_.assign(key_end, nullptr);
        }
        load(entries);
    }

    // Leaves one empty leaf; the leaf table is cleared (capacity kept).
    void clear() noexcept {
        release(std::move(root_));
        reset_empty();
    }

    // The row (below count()) and where it falls; entry and within locate
    // it inside the leaf.
    [[nodiscard]] Place find(std::size_t row) const noexcept {
        Node* node = root_.get();
        std::size_t remaining = row;
        while (!(*node).leaf) {
            std::size_t child = 0U;
            while (child + 1U < (*node).children.size() && remaining >= (*node).child_counts[child]) {
                remaining = remaining - (*node).child_counts[child];
                ++child;
            }
            node = (*node).children[child].get();
        }
        const Place place = place_in_leaf(node, remaining);
        return place;
    }

    // The point before row (at most count()). An insertion at a boundary
    // between children goes to the end of the child before it.
    [[nodiscard]] Place find_insertion(std::size_t row) const noexcept {
        Node* node = root_.get();
        std::size_t remaining = row;
        while (!(*node).leaf) {
            std::size_t child = 0U;
            while (child + 1U < (*node).children.size() && remaining > (*node).child_counts[child]) {
                remaining = remaining - (*node).child_counts[child];
                ++child;
            }
            node = (*node).children[child].get();
        }
        const Place place = place_in_leaf(node, remaining);
        return place;
    }

    // The rows before entry `entry` of `leaf` in the whole sequence.
    [[nodiscard]] std::size_t rank_in_leaf(const Node* leaf, std::size_t entry) const noexcept {
        std::size_t rows = 0U;
        for (std::size_t index = 0U; index < entry; ++index) {
            rows = rows + Traits::entry_count((*leaf).entries[index]);
        }
        const Node* node = leaf;
        while ((*node).parent != nullptr) {
            const Node& parent = *(*node).parent;
            const std::size_t at = index_in_parent(*node);
            for (std::size_t left = 0U; left < at; ++left) {
                rows = rows + parent.child_counts[left];
            }
            node = &parent;
        }
        return rows;
    }

    // Tracked leaves only: the leaf holding the entry with `key`, or null.
    [[nodiscard]] Node* leaf_of(std::uint32_t key) const noexcept {
        Node* leaf = key < leaf_of_.size() ? leaf_of_[key] : nullptr;
        return leaf;
    }
    // Tracked leaves only: the entry with `key` has left its leaf.
    void forget(std::uint32_t key) noexcept {
        if (key < leaf_of_.size()) {
            leaf_of_[key] = nullptr;
        }
    }

    // After a holder has edited (*node).entries of a leaf (any number of
    // entries, up to twice node_capacity), or the tree has removed children
    // of a branch: records the leaf's entries, refreshes the path to the
    // root, splits an overfull node, removes an emptied one, collapses the
    // root and enforces the depth limit. node may be freed.
    void settle(Node* node) {
        if constexpr (Traits::tracks_leaves) {
            if ((*node).leaf) {
                record_entries(node);
            }
        }
        refresh_upward(node);
        const bool emptied = (*node).leaf ? (*node).entries.empty() : (*node).children.empty();
        if (emptied && (*node).parent != nullptr) {
            remove_empty(node);
        } else {
            split_overfull(node);
        }
        collapse_root();
        enforce_depth();
    }

    // Removes the branch's child at `index` with its whole subtree, without
    // visiting the entries (keys of a tracked tree are not forgotten, so
    // tracked holders erase entry by entry instead), then settles the branch.
    void detach_child(Node* branch, std::size_t index) {
        const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(index);
        std::unique_ptr<Node> child = std::move((*branch).children[index]);
        (*branch).children.erase((*branch).children.begin() + at);
        (*branch).child_counts.erase((*branch).child_counts.begin() + at);
        (*branch).child_summaries.erase((*branch).child_summaries.begin() + at);
        release(std::move(child));
        settle(branch);
    }

    // Leaves in order: the first, and the one after `leaf` (null past the
    // last). O(depth * node_capacity) per step, amortised O(1) over a walk.
    [[nodiscard]] const Node* first_leaf() const noexcept {
        const Node* node = root_.get();
        while (!(*node).leaf) {
            node = (*node).children.front().get();
        }
        return node;
    }
    [[nodiscard]] const Node* next_leaf(const Node* leaf) const noexcept {
        const Node* node = leaf;
        while ((*node).parent != nullptr) {
            const Node& parent = *(*node).parent;
            const std::size_t at = index_in_parent(*node);
            if (at + 1U < parent.children.size()) {
                const Node* next = parent.children[at + 1U].get();
                while (!(*next).leaf) {
                    next = (*next).children.front().get();
                }
                return next;
            }
            node = &parent;
        }
        return nullptr;
    }

    // Entries held, by walking the leaves.
    [[nodiscard]] std::size_t entry_total() const noexcept {
        std::size_t total = 0U;
        for (const Node* leaf = first_leaf(); leaf != nullptr; leaf = next_leaf(leaf)) {
            total = total + (*leaf).entries.size();
        }
        return total;
    }

    // Re-loads the entries bottom-up at bulk_fill (the leaf table keeps its
    // size). Allocates one array of the entries first.
    void compact() {
        std::vector<Entry> entries;
        entries.reserve(entry_total());
        for (const Node* leaf = first_leaf(); leaf != nullptr; leaf = next_leaf(leaf)) {
            entries.insert(entries.end(), (*leaf).entries.begin(), (*leaf).entries.end());
        }
        release(std::move(root_));
        root_ = make_node();
        depth_ = 1U;
        load(entries);
    }

private:
    [[nodiscard]] std::unique_ptr<Node> make_node() {
        std::unique_ptr<Node> node = std::make_unique<Node>();
        ++nodes_;
        return node;
    }

    // An empty leaf as the root, as after construction.
    void reset_empty() noexcept {
        root_ = std::make_unique<Node>();
        nodes_ = 1U;
        depth_ = 1U;
        leaf_of_.clear();
    }

    // Frees a subtree without recursion: descends to a childless node,
    // frees it from its parent's back, and climbs.
    void release(std::unique_ptr<Node> top) noexcept {
        if (!top) {
            return;
        }
        Node* const stop = top.get();
        Node* node = stop;
        while (true) {
            if (!(*node).children.empty()) {
                node = (*node).children.back().get();
                continue;
            }
            if (node == stop) {
                break;
            }
            Node* parent = (*node).parent;
            (*parent).children.pop_back();
            --nodes_;
            node = parent;
        }
        top.reset();
        --nodes_;
    }

    [[nodiscard]] static Place place_in_leaf(Node* leaf, std::size_t rows) noexcept {
        std::size_t entry = 0U;
        std::size_t remaining = rows;
        while (entry < (*leaf).entries.size() && remaining >= Traits::entry_count((*leaf).entries[entry])) {
            remaining = remaining - Traits::entry_count((*leaf).entries[entry]);
            ++entry;
        }
        const Place place{leaf, entry, remaining};
        return place;
    }

    // Builds leaves over entries, then each level of branches over the one
    // below, into the (empty) root.
    void load(std::span<const Entry> entries) {
        if (entries.empty()) {
            return;
        }
        std::vector<std::unique_ptr<Node>> level;
        level.reserve(entries.size() / bulk_fill + 1U);
        std::size_t start = 0U;
        while (start < entries.size()) {
            const std::size_t end = std::min(entries.size(), start + bulk_fill);
            std::unique_ptr<Node> leaf = make_node();
            (*leaf).entries.assign(entries.begin() + static_cast<std::ptrdiff_t>(start),
                                   entries.begin() + static_cast<std::ptrdiff_t>(end));
            if constexpr (Traits::tracks_leaves) {
                record_entries(leaf.get());
            }
            refresh_local(*leaf);
            level.push_back(std::move(leaf));
            start = end;
        }
        std::size_t levels = 1U;
        while (level.size() > 1U) {
            std::vector<std::unique_ptr<Node>> above;
            above.reserve(level.size() / bulk_fill + 1U);
            std::size_t first = 0U;
            while (first < level.size()) {
                const std::size_t end = std::min(level.size(), first + bulk_fill);
                std::unique_ptr<Node> branch = make_node();
                (*branch).leaf = false;
                for (std::size_t index = first; index < end; ++index) {
                    (*level[index]).parent = branch.get();
                    (*branch).child_counts.push_back((*level[index]).count);
                    (*branch).child_summaries.push_back((*level[index]).summary);
                    (*branch).children.push_back(std::move(level[index]));
                }
                refresh_local(*branch);
                above.push_back(std::move(branch));
                first = end;
            }
            level = std::move(above);
            ++levels;
        }
        release(std::move(root_));
        root_ = std::move(level.front());
        (*root_).parent = nullptr;
        depth_ = levels;
    }

    void record_entries(Node* leaf) {
        for (const Entry& entry : (*leaf).entries) {
            const std::uint32_t key = Traits::entry_key(entry);
            if (key >= leaf_of_.size()) {
                leaf_of_.resize(static_cast<std::size_t>(key) + 1U, nullptr);
            }
            leaf_of_[key] = leaf;
        }
    }

    static void refresh_local(Node& node) noexcept {
        if (node.leaf) {
            std::size_t rows = 0U;
            for (const Entry& entry : node.entries) {
                rows = rows + Traits::entry_count(entry);
            }
            node.count = rows;
            node.summary = Traits::summarize_entries(std::span<const Entry>(node.entries));
            return;
        }
        std::size_t rows = 0U;
        for (const std::size_t child : node.child_counts) {
            rows = rows + child;
        }
        node.count = rows;
        node.summary = Traits::summarize_children(std::span<const Summary>(node.child_summaries));
    }

    [[nodiscard]] static std::size_t index_in_parent(const Node& node) noexcept {
        const Node& parent = *node.parent;
        std::size_t index = 0U;
        while (index < parent.children.size() && parent.children[index].get() != &node) {
            ++index;
        }
        return index;
    }

    void refresh_upward(Node* node) noexcept {
        while (node != nullptr) {
            refresh_local(*node);
            Node* parent = (*node).parent;
            if (parent != nullptr) {
                const std::size_t at = index_in_parent(*node);
                (*parent).child_counts[at] = (*node).count;
                (*parent).child_summaries[at] = (*node).summary;
            }
            node = parent;
        }
    }

    // Splits each overfull node on the path up in two halves; a split root
    // grows a level.
    void split_overfull(Node* node) {
        while (node != nullptr) {
            const std::size_t held = (*node).leaf ? (*node).entries.size() : (*node).children.size();
            if (held <= node_capacity) {
                return;
            }
            std::unique_ptr<Node> sibling = make_node();
            (*sibling).leaf = (*node).leaf;
            const std::size_t keep = held / 2U;
            const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(keep);
            if ((*node).leaf) {
                (*sibling).entries.assign((*node).entries.begin() + at, (*node).entries.end());
                (*node).entries.erase((*node).entries.begin() + at, (*node).entries.end());
                if constexpr (Traits::tracks_leaves) {
                    record_entries(sibling.get());
                }
            } else {
                for (std::size_t index = keep; index < held; ++index) {
                    (*(*node).children[index]).parent = sibling.get();
                    (*sibling).children.push_back(std::move((*node).children[index]));
                }
                (*sibling).child_counts.assign((*node).child_counts.begin() + at, (*node).child_counts.end());
                (*sibling).child_summaries.assign((*node).child_summaries.begin() + at,
                                                  (*node).child_summaries.end());
                (*node).children.erase((*node).children.begin() + at, (*node).children.end());
                (*node).child_counts.erase((*node).child_counts.begin() + at, (*node).child_counts.end());
                (*node).child_summaries.erase((*node).child_summaries.begin() + at, (*node).child_summaries.end());
            }
            refresh_local(*node);
            refresh_local(*sibling);
            Node* parent = (*node).parent;
            if (parent == nullptr) {
                // The root splits: a new root over the two halves.
                std::unique_ptr<Node> grown = make_node();
                (*grown).leaf = false;
                (*sibling).parent = grown.get();
                (*root_).parent = grown.get();
                (*grown).child_counts.push_back((*root_).count);
                (*grown).child_summaries.push_back((*root_).summary);
                (*grown).child_counts.push_back((*sibling).count);
                (*grown).child_summaries.push_back((*sibling).summary);
                (*grown).children.push_back(std::move(root_));
                (*grown).children.push_back(std::move(sibling));
                refresh_local(*grown);
                root_ = std::move(grown);
                ++depth_;
                return;
            }
            const std::size_t index = index_in_parent(*node);
            const std::ptrdiff_t after = static_cast<std::ptrdiff_t>(index + 1U);
            (*sibling).parent = parent;
            (*parent).child_counts[index] = (*node).count;
            (*parent).child_summaries[index] = (*node).summary;
            (*parent).child_counts.insert((*parent).child_counts.begin() + after, (*sibling).count);
            (*parent).child_summaries.insert((*parent).child_summaries.begin() + after, (*sibling).summary);
            (*parent).children.insert((*parent).children.begin() + after, std::move(sibling));
            node = parent;
        }
    }

    // Removes the emptied node and every ancestor it leaves empty (the root
    // stays), then refreshes the path above.
    void remove_empty(Node* node) {
        while ((*node).parent != nullptr) {
            Node* parent = (*node).parent;
            const std::size_t index = index_in_parent(*node);
            const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(index);
            std::unique_ptr<Node> gone = std::move((*parent).children[index]);
            (*parent).children.erase((*parent).children.begin() + at);
            (*parent).child_counts.erase((*parent).child_counts.begin() + at);
            (*parent).child_summaries.erase((*parent).child_summaries.begin() + at);
            release(std::move(gone));
            if (!(*parent).children.empty() || (*parent).parent == nullptr) {
                refresh_upward(parent);
                return;
            }
            node = parent;
        }
    }

    void collapse_root() {
        while (!(*root_).leaf && (*root_).children.size() == 1U) {
            std::unique_ptr<Node> child = std::move((*root_).children.front());
            (*root_).children.clear();
            (*child).parent = nullptr;
            release(std::move(root_));
            root_ = std::move(child);
            --depth_;
        }
        if (!(*root_).leaf && (*root_).children.empty()) {
            release(std::move(root_));
            root_ = make_node();
            depth_ = 1U;
        }
    }

    void enforce_depth() {
        if (depth_ > depth_limit(count())) {
            compact();
        }
    }

    std::unique_ptr<Node> root_;
    // Tracked leaves: the leaf holding each key, or null; indexed by key.
    std::vector<Node*> leaf_of_;
    std::size_t nodes_{};
    std::size_t depth_{1U};
};

} // namespace gui_forms::detail
