// The view index's order-statistic B-tree. See binding_order_tree.hpp.

#include "binding_order_tree.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace gui_forms::detail {
namespace {

// A bulk load fills nodes to three quarters, leaving room to insert.
constexpr std::size_t bulk_fill = BindingOrderTree::node_capacity * 3U / 4U;

} // namespace

BindingOrderTree::BindingOrderTree() : root_(std::make_unique<Node>()) {}

BindingOrderTree::~BindingOrderTree() = default;

void BindingOrderTree::record_leaf(std::uint32_t element, Node* leaf) {
    if (element >= leaf_of_.size()) {
        leaf_of_.resize(static_cast<std::size_t>(element) + 1U, nullptr);
    }
    leaf_of_[element] = leaf;
}

void BindingOrderTree::refresh_local(Node& node) noexcept {
    if (node.leaf) {
        node.count = node.elements.size();
        node.last = node.elements.empty() ? 0U : node.elements.back();
        return;
    }
    std::size_t total = 0U;
    for (const std::size_t child : node.child_counts) {
        total = total + child;
    }
    node.count = total;
    node.last = node.children.empty() ? 0U : (*node.children.back()).last;
}

std::size_t BindingOrderTree::index_in_parent(const Node& node) noexcept {
    const Node& parent = *node.parent;
    std::size_t index = 0U;
    while (index < parent.children.size() && parent.children[index].get() != &node) {
        ++index;
    }
    return index;
}

void BindingOrderTree::refresh_upward(Node* node) noexcept {
    while (node != nullptr) {
        refresh_local(*node);
        Node* parent = (*node).parent;
        if (parent != nullptr) {
            (*parent).child_counts[index_in_parent(*node)] = (*node).count;
        }
        node = parent;
    }
}

void BindingOrderTree::assign(std::span<const std::uint32_t> elements, std::size_t element_end) {
    clear();
    leaf_of_.assign(element_end, nullptr);
    if (elements.empty()) {
        return;
    }
    // Leaves, then each level of branches over the one below.
    std::vector<std::unique_ptr<Node>> level;
    level.reserve(elements.size() / bulk_fill + 1U);
    std::size_t start = 0U;
    while (start < elements.size()) {
        const std::size_t end = std::min(elements.size(), start + bulk_fill);
        std::unique_ptr<Node> leaf = std::make_unique<Node>();
        (*leaf).elements.assign(elements.begin() + static_cast<std::ptrdiff_t>(start),
                                elements.begin() + static_cast<std::ptrdiff_t>(end));
        for (const std::uint32_t element : (*leaf).elements) {
            record_leaf(element, leaf.get());
        }
        refresh_local(*leaf);
        level.push_back(std::move(leaf));
        start = end;
    }
    while (level.size() > 1U) {
        std::vector<std::unique_ptr<Node>> above;
        above.reserve(level.size() / bulk_fill + 1U);
        std::size_t first = 0U;
        while (first < level.size()) {
            const std::size_t end = std::min(level.size(), first + bulk_fill);
            std::unique_ptr<Node> branch = std::make_unique<Node>();
            (*branch).leaf = false;
            for (std::size_t index = first; index < end; ++index) {
                (*level[index]).parent = branch.get();
                (*branch).child_counts.push_back((*level[index]).count);
                (*branch).children.push_back(std::move(level[index]));
            }
            refresh_local(*branch);
            above.push_back(std::move(branch));
            first = end;
        }
        level = std::move(above);
    }
    root_ = std::move(level.front());
    (*root_).parent = nullptr;
}

void BindingOrderTree::clear() noexcept {
    root_ = std::make_unique<Node>();
    leaf_of_.clear();
}

std::size_t BindingOrderTree::size() const noexcept {
    return (*root_).count;
}

std::uint32_t BindingOrderTree::select(std::size_t position) const {
    if (position >= size()) {
        throw std::out_of_range("GUI.Forms binding view position is past the end");
    }
    const Node* node = root_.get();
    while (!(*node).leaf) {
        std::size_t index = 0U;
        while (position >= (*node).child_counts[index]) {
            position = position - (*node).child_counts[index];
            ++index;
        }
        node = (*node).children[index].get();
    }
    return (*node).elements[position];
}

std::size_t BindingOrderTree::rank(std::uint32_t element) const {
    if (!contains(element)) {
        throw std::logic_error("GUI.Forms binding view element is not in the tree");
    }
    const Node* node = leaf_of_[element];
    std::size_t position = 0U;
    while ((*node).elements[position] != element) {
        ++position;
    }
    while ((*node).parent != nullptr) {
        const Node& parent = *(*node).parent;
        const std::size_t index = index_in_parent(*node);
        for (std::size_t left = 0U; left < index; ++left) {
            position = position + parent.child_counts[left];
        }
        node = &parent;
    }
    return position;
}

bool BindingOrderTree::contains(std::uint32_t element) const noexcept {
    const bool present = element < leaf_of_.size() && leaf_of_[element] != nullptr;
    return present;
}

void BindingOrderTree::insert_at(std::size_t position, std::uint32_t element) {
    if (position > size()) {
        throw std::out_of_range("GUI.Forms binding view insertion is past the end");
    }
    Node* node = root_.get();
    while (!(*node).leaf) {
        std::size_t index = 0U;
        // The first child that reaches position, its end included.
        while (index + 1U < (*node).children.size() && position > (*node).child_counts[index]) {
            position = position - (*node).child_counts[index];
            ++index;
        }
        node = (*node).children[index].get();
    }
    insert_into_leaf(node, position, element);
}

void BindingOrderTree::insert_into_leaf(Node* leaf, std::size_t index, std::uint32_t element) {
    (*leaf).elements.insert((*leaf).elements.begin() + static_cast<std::ptrdiff_t>(index), element);
    record_leaf(element, leaf);
    refresh_upward(leaf);
    split_overfull(leaf);
}

void BindingOrderTree::split_overfull(Node* node) {
    while (node != nullptr) {
        const std::size_t held = (*node).leaf ? (*node).elements.size() : (*node).children.size();
        if (held <= node_capacity) {
            return;
        }
        std::unique_ptr<Node> sibling = std::make_unique<Node>();
        (*sibling).leaf = (*node).leaf;
        const std::size_t keep = held / 2U;
        if ((*node).leaf) {
            (*sibling).elements.assign((*node).elements.begin() + static_cast<std::ptrdiff_t>(keep),
                                       (*node).elements.end());
            (*node).elements.resize(keep);
            for (const std::uint32_t element : (*sibling).elements) {
                record_leaf(element, sibling.get());
            }
        } else {
            for (std::size_t index = keep; index < held; ++index) {
                (*(*node).children[index]).parent = sibling.get();
                (*sibling).child_counts.push_back((*node).child_counts[index]);
                (*sibling).children.push_back(std::move((*node).children[index]));
            }
            (*node).children.resize(keep);
            (*node).child_counts.resize(keep);
        }
        refresh_local(*node);
        refresh_local(*sibling);
        Node* parent = (*node).parent;
        if (parent == nullptr) {
            // The root splits: a new root over the two halves.
            std::unique_ptr<Node> grown = std::make_unique<Node>();
            (*grown).leaf = false;
            (*sibling).parent = grown.get();
            (*grown).child_counts.push_back((*root_).count);
            (*grown).child_counts.push_back((*sibling).count);
            (*root_).parent = grown.get();
            (*grown).children.push_back(std::move(root_));
            (*grown).children.push_back(std::move(sibling));
            refresh_local(*grown);
            root_ = std::move(grown);
            return;
        }
        const std::size_t index = index_in_parent(*node);
        (*sibling).parent = parent;
        (*parent).child_counts[index] = (*node).count;
        (*parent).child_counts.insert((*parent).child_counts.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                                      (*sibling).count);
        (*parent).children.insert((*parent).children.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                                  std::move(sibling));
        node = parent;
    }
}

void BindingOrderTree::erase(std::uint32_t element) {
    if (!contains(element)) {
        throw std::logic_error("GUI.Forms binding view element is not in the tree");
    }
    Node* leaf = leaf_of_[element];
    std::vector<std::uint32_t>::iterator found = (*leaf).elements.begin();
    while (*found != element) {
        ++found;
    }
    (*leaf).elements.erase(found);
    leaf_of_[element] = nullptr;
    if ((*leaf).elements.empty() && (*leaf).parent != nullptr) {
        remove_empty(leaf);
    } else {
        refresh_upward(leaf);
    }
    collapse_root();
}

void BindingOrderTree::remove_empty(Node* node) {
    // Removes node and every ancestor it leaves empty; bounded by the depth.
    while (node != nullptr && (*node).parent != nullptr) {
        Node* parent = (*node).parent;
        const std::size_t index = index_in_parent(*node);
        (*parent).children.erase((*parent).children.begin() + static_cast<std::ptrdiff_t>(index));
        (*parent).child_counts.erase((*parent).child_counts.begin() + static_cast<std::ptrdiff_t>(index));
        if (!(*parent).children.empty() || (*parent).parent == nullptr) {
            refresh_upward(parent);
            return;
        }
        node = parent;
    }
}

void BindingOrderTree::collapse_root() {
    while (!(*root_).leaf && (*root_).children.size() == 1U) {
        std::unique_ptr<Node> child = std::move((*root_).children.front());
        (*child).parent = nullptr;
        root_ = std::move(child);
    }
    if (!(*root_).leaf && (*root_).children.empty()) {
        root_ = std::make_unique<Node>();
    }
}

std::size_t BindingOrderTree::count_nodes(const Node& node) noexcept {
    std::size_t total = 1U;
    for (const std::unique_ptr<Node>& child : node.children) {
        total = total + count_nodes(*child);
    }
    return total;
}

std::size_t BindingOrderTree::node_count() const noexcept {
    return count_nodes(*root_);
}

} // namespace gui_forms::detail
