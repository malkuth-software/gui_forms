// The view index's order-statistic sequence. See binding_order_tree.hpp; the
// node operations are the shared family's (order_statistic_tree.hpp).

#include "binding_order_tree.hpp"

#include <stdexcept>
#include <vector>

namespace gui_forms::detail {

BindingOrderTree::BindingOrderTree() : tree_() {}

BindingOrderTree::~BindingOrderTree() {}

void BindingOrderTree::assign(std::span<const std::uint32_t> elements, std::size_t element_end) {
    tree_.assign(elements, element_end);
}

void BindingOrderTree::clear() noexcept {
    tree_.clear();
}

std::size_t BindingOrderTree::size() const noexcept {
    return tree_.count();
}

std::uint32_t BindingOrderTree::select(std::size_t position) const {
    if (position >= size()) {
        throw std::out_of_range("GUI.Forms binding view position is past the end");
    }
    const Tree::Place place = tree_.find(position);
    const std::uint32_t element = (*place.leaf).entries[place.entry];
    return element;
}

std::size_t BindingOrderTree::rank(std::uint32_t element) const {
    if (!contains(element)) {
        throw std::logic_error("GUI.Forms binding view element is not in the tree");
    }
    const Tree::Node* leaf = tree_.leaf_of(element);
    std::size_t entry = 0U;
    while ((*leaf).entries[entry] != element) {
        ++entry;
    }
    const std::size_t position = tree_.rank_in_leaf(leaf, entry);
    return position;
}

bool BindingOrderTree::contains(std::uint32_t element) const noexcept {
    const bool present = tree_.leaf_of(element) != nullptr;
    return present;
}

void BindingOrderTree::insert_at(std::size_t position, std::uint32_t element) {
    if (position > size()) {
        throw std::out_of_range("GUI.Forms binding view insertion is past the end");
    }
    const Tree::Place place = tree_.find_insertion(position);
    std::vector<std::uint32_t>& entries = (*place.leaf).entries;
    entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(place.entry), element);
    tree_.settle(place.leaf);
}

void BindingOrderTree::erase(std::uint32_t element) {
    if (!contains(element)) {
        throw std::logic_error("GUI.Forms binding view element is not in the tree");
    }
    Tree::Node* leaf = tree_.leaf_of(element);
    std::vector<std::uint32_t>::iterator found = (*leaf).entries.begin();
    while (*found != element) {
        ++found;
    }
    (*leaf).entries.erase(found);
    tree_.forget(element);
    tree_.settle(leaf);
}

std::size_t BindingOrderTree::node_count() const noexcept {
    return tree_.node_count();
}

std::size_t BindingOrderTree::depth() const noexcept {
    return tree_.depth();
}

} // namespace gui_forms::detail
