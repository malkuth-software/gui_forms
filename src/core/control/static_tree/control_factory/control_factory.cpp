#include "gui_forms/static_tree.hpp"

#include "../../../algorithm/tree_teardown.hpp"

#include <cstddef>
#include <stdexcept>
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

Control::Ptr build_static_tree(const StaticNode& node, const ControlFactory& factory) {
    Control::Ptr control = factory.create(node.type, StableId(node.stable_id));
    (*control).set_requested_bounds(node.requested_bounds);
    for (const StaticNode& child_node : node.children) {
        (*control).add_child(build_static_tree(child_node, factory));
    }
    return control;
}

} // namespace gui_forms
