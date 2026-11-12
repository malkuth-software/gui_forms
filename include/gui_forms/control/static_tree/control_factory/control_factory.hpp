#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gui_forms {

// A control description and its children. A description built in code has
// no depth limit, so copying and destroying one walk it in explicit work
// storage instead of recursing per level (HOUSE_STYLE section 1). Moving
// moves the child list's storage.
struct GUI_FORMS_API StaticNode {
    std::string type;
    std::string stable_id;
    Rect requested_bounds{};
    std::vector<StaticNode> children;

    StaticNode() = default;
    // Brace-initializable as the aggregate it was:
    // StaticNode{"panel", "main.panel", bounds, {{"button", "main.ok", ok}}}.
    StaticNode(std::string node_type, std::string node_stable_id, Rect node_requested_bounds,
               std::vector<StaticNode> node_children = {});
    // A deep copy, level by level in a work list. On failure (allocation)
    // the partial copy is destroyed and the exception propagates.
    StaticNode(const StaticNode& other);
    StaticNode(StaticNode&& other) noexcept = default;
    // Copies into a new description first, so on failure this one is
    // unchanged.
    StaticNode& operator=(const StaticNode& other);
    StaticNode& operator=(StaticNode&& other) noexcept = default;
    // Tears the children down iteratively.
    ~StaticNode();
};

class GUI_FORMS_API ControlFactory {
public:
    using Creator = std::function<Control::Ptr(StableId)>;

    void register_type(std::string type, Creator creator);
    [[nodiscard]] Control::Ptr create(std::string_view type, StableId stable_id) const;

private:
    using CreatorMap = std::unordered_map<std::string, Creator>;
    CreatorMap creators_;
};

[[nodiscard]] GUI_FORMS_API Control::Ptr build_static_tree(const StaticNode& node,
                                             const ControlFactory& factory);

} // namespace gui_forms
