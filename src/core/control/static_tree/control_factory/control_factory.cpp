#include "gui_forms/static_tree.hpp"

#include "../../../algorithm/tree_teardown.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

// A source description and the copy being filled from it.
struct StaticCopyStep final {
    const StaticNode* source{nullptr};
    StaticNode* destination{nullptr};
};

} // namespace

StaticNode::StaticNode(std::string node_type, std::string node_stable_id, Rect node_requested_bounds,
                       std::vector<StaticNode> node_children)
    : type(std::move(node_type)),
      stable_id(std::move(node_stable_id)),
      requested_bounds(node_requested_bounds),
      children(std::move(node_children)) {}

StaticNode::StaticNode(const StaticNode& other)
    : type(other.type), stable_id(other.stable_id), requested_bounds(other.requested_bounds) {
    // Each step fills one destination's children with copies of the
    // source's own fields, then queues each child pair; the child list is
    // reserved first, so the queued pointers into it stay valid.
    std::vector<StaticCopyStep> pending{};
    pending.push_back(StaticCopyStep{&other, this});
    while (!pending.empty()) {
        const StaticCopyStep step = pending.back();
        pending.pop_back();
        const std::vector<StaticNode>& source_children = (*step.source).children;
        std::vector<StaticNode>& destination_children = (*step.destination).children;
        destination_children.reserve(source_children.size());
        for (const StaticNode& source_child : source_children) {
            destination_children.push_back(
                StaticNode(source_child.type, source_child.stable_id, source_child.requested_bounds));
        }
        for (std::size_t index = 0U; index < source_children.size(); ++index) {
            if (source_children[index].children.empty()) continue;
            pending.push_back(StaticCopyStep{&source_children[index], &destination_children[index]});
        }
    }
}

StaticNode& StaticNode::operator=(const StaticNode& other) {
    StaticNode copy(other);
    *this = std::move(copy);
    return *this;
}

StaticNode::~StaticNode() {
    if (!children.empty()) {
        detail::release_children_iteratively<StaticNode, &StaticNode::children>(children);
    }
}

void ControlFactory::register_type(std::string type, Creator creator) {
    if (type.empty() || !creator) {
        throw std::invalid_argument("GUI.Forms control registration requires type and creator");
    }
    const std::pair<std::unordered_map<std::string, Creator>::iterator, bool>
        insertion = creators_.emplace(std::move(type), std::move(creator));
    const std::unordered_map<std::string, Creator>::iterator iterator =
        insertion.first;
    const bool inserted = insertion.second;
    if (!inserted) {
        throw std::logic_error("GUI.Forms control type is already registered: " + (*iterator).first);
    }
}

Control::Ptr ControlFactory::create(std::string_view type, StableId stable_id) const {
    const CreatorMap::const_iterator found = creators_.find(std::string(type));
    if (found == creators_.end()) {
        throw std::out_of_range("GUI.Forms unknown static control type: " + std::string(type));
    }
    Control::Ptr result = (*found).second(std::move(stable_id));
    if (!result) {
        throw std::logic_error("GUI.Forms control factory returned null");
    }
    return result;
}

namespace {

// One level of the description being built: its node, the control made
// for it, and the next of its children to build.
struct BuildFrame final {
    const StaticNode* node{nullptr};
    Control* control{nullptr};
    std::size_t next_child{0U};
};

using BuildFrames = std::array<BuildFrame, maximum_control_tree_depth>;

// Whether the description nests within maximum_control_tree_depth levels,
// the root counted as one. Walked before anything is made, in fixed
// storage of one frame per level (the depth rule, owner 2026-11-11): a
// frame is a node with children, so a node with children whose children
// would pass the limit is the refusal, and the walk never needs a frame
// past it.
[[nodiscard]] bool description_within_depth(const StaticNode& root, BuildFrames& frames) noexcept {
    frames[0] = BuildFrame{&root, nullptr, 0U};
    std::size_t depth = 1U;
    while (depth > 0U) {
        BuildFrame& frame = frames[depth - 1U];
        if (frame.next_child == (*frame.node).children.size()) {
            --depth;
            continue;
        }
        const StaticNode& child = (*frame.node).children[frame.next_child];
        ++frame.next_child;
        if (child.children.empty()) continue;
        // The child is at level depth + 1, so its children would be at
        // depth + 2: refused past the limit. Within it, the child's frame
        // index, depth, is at most maximum_control_tree_depth - 2.
        if (depth + 2U > maximum_control_tree_depth) return false;
        frames[depth] = BuildFrame{&child, nullptr, 0U};
        ++depth;
    }
    return true;
}

} // namespace

// Builds the description top-down in fixed storage, never recursing per
// level: the depth is checked first, so a too-deep description makes
// nothing (ControlTreeDepthError). Each control is made, sized and added to
// its parent before its own children are made, in document order, so the
// tree ends as the bottom-up build left it. A failure (an unknown type, a
// refused child) throws; the controls made so far go with `root`.
Control::Ptr build_static_tree(const StaticNode& node, const ControlFactory& factory) {
    BuildFrames frames{};
    if (!description_within_depth(node, frames)) {
        throw ControlTreeDepthError("GUI.Forms static tree cannot nest deeper than " +
                                    std::to_string(maximum_control_tree_depth) + " levels");
    }
    Control::Ptr root = factory.create(node.type, StableId(node.stable_id));
    (*root).set_requested_bounds(node.requested_bounds);
    frames[0] = BuildFrame{&node, root.get(), 0U};
    std::size_t depth = 1U;
    while (depth > 0U) {
        BuildFrame& frame = frames[depth - 1U];
        if (frame.next_child == (*frame.node).children.size()) {
            --depth;
            continue;
        }
        const StaticNode& child_node = (*frame.node).children[frame.next_child];
        ++frame.next_child;
        Control::Ptr child = factory.create(child_node.type, StableId(child_node.stable_id));
        (*child).set_requested_bounds(child_node.requested_bounds);
        // The parent's list owns the child from here; the frame observes it.
        Control* const made = child.get();
        (*frame.control).add_child(std::move(child));
        if (child_node.children.empty()) continue;
        frames[depth] = BuildFrame{&child_node, made, 0U};
        ++depth;
    }
    return root;
}

} // namespace gui_forms
