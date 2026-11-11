#pragma once

// Iterative teardown for a tree whose nodes own their children by value in
// a std::vector (XmlElement::children, JsonValue::items), so destroying a
// deep tree never nests a destructor call per level (HOUSE_STYLE section 1:
// tear deep owning structures down iteratively). Private to the library.
//
// A node type opts in by calling release_children_iteratively from its own
// destructor on its child list. The child list is moved into a work list;
// each node taken from the work list's end hands its children to the work
// list before it is destroyed childless, so its own destructor finds
// nothing to do. The work list grows with the number of nodes waiting, not
// with the depth.
//
// Allocation: growing the work list can fail. A node whose children cannot
// be appended is then destroyed with them in place, through its own
// destructor, which runs this teardown again one call deeper; only
// allocation failure nests a call, never the tree's shape.

#include <cstddef>
#include <utility>
#include <vector>

namespace gui_forms::detail {

template <typename Node, std::vector<Node> Node::*Children>
void release_children_iteratively(std::vector<Node>& children) noexcept {
    std::vector<Node> pending = std::move(children);
    children.clear();
    while (!pending.empty()) {
        std::vector<Node>& below = pending.back().*Children;
        if (below.empty()) {
            pending.pop_back();
            continue;
        }
        // Moving a node moves its child list's storage, never its children.
        Node held = std::move(pending.back());
        pending.pop_back();
        std::vector<Node>& held_children = held.*Children;
        const std::size_t needed = pending.size() + held_children.size();
        bool reserved = false;
        try {
            pending.reserve(needed);
            reserved = true;
        } catch (...) {
            reserved = false;
        }
        if (!reserved) continue;
        for (Node& child : held_children) {
            pending.push_back(std::move(child));
        }
        held_children.clear();
        // `held` is destroyed here, childless.
    }
}

} // namespace gui_forms::detail
