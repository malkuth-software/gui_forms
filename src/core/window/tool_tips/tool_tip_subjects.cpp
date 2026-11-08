#include "tool_tip_subjects.hpp"

#include <string>
#include <vector>

namespace gui_forms::detail {

namespace {

// The deepest node under `point` among `nodes` and their children, or null.
// The pointer is borrowed from `nodes`.
[[nodiscard]] const SemanticNode* node_at(const std::vector<SemanticNode>& nodes, Point point) {
    for (const SemanticNode& node : nodes) {
        if (!contains_point(node.bounds, point)) continue;
        const SemanticNode* deeper = node_at(node.children, point);
        if (deeper != nullptr) return deeper;
        return &node;
    }
    return nullptr;
}

const SemanticNode* node_at(std::vector<SemanticNode>&& nodes, Point point) = delete;

// The first node among `nodes` and their children whose states hold `state`,
// or null. The pointer is borrowed from `nodes`.
[[nodiscard]] const SemanticNode* node_with_state(const std::vector<SemanticNode>& nodes,
                                                  SemanticState state) {
    for (const SemanticNode& node : nodes) {
        if (has_semantic_state(node.states, state)) return &node;
        const SemanticNode* deeper = node_with_state(node.children, state);
        if (deeper != nullptr) return deeper;
    }
    return nullptr;
}

const SemanticNode* node_with_state(std::vector<SemanticNode>&& nodes, SemanticState state) = delete;

[[nodiscard]] ToolTipSubject nothing_over(Rect region) {
    ToolTipSubject subject{};
    subject.anchor = region;
    return subject;
}

} // namespace

bool contains_point(Rect bounds, Point point) noexcept {
    const bool inside = !bounds.empty() && point.x >= bounds.x && point.y >= bounds.y &&
                        point.x < bounds.x + bounds.width && point.y < bounds.y + bounds.height;
    return inside;
}

ToolTipSubject tool_tip_subject_of(const Control::Ptr& control, std::string item_id, std::string text,
                                        Rect anchor) {
    ToolTipSubject subject{};
    subject.control = control;
    subject.item_id = std::move(item_id);
    subject.text = std::move(text);
    subject.anchor = anchor;
    return subject;
}

// A cover of the control itself covers every part of it too.
bool covered(const ToolTipCovers& covers, const Control& control, std::string_view item_id) {
    const std::pair<std::uint64_t, std::string> whole{control.runtime_id().value, std::string{}};
    if (covers.find(whole) != covers.end()) return true;
    if (item_id.empty()) return false;
    const std::pair<std::uint64_t, std::string> part{control.runtime_id().value, std::string(item_id)};
    const bool found = covers.find(part) != covers.end();
    return found;
}

bool available(const ToolTipSettings& settings, const Control& control) noexcept {
    if (!control.effectively_visible()) return false;
    const bool shown = settings.show_when_disabled || control.effectively_enabled();
    return shown;
}

// The thing under the pointer: from the control under it outwards, a part
// of a control that publishes part tooltips, else the control's own text
// (a control whose tooltip shows only over its owner's item is passed
// over). A part's tooltip only overrides where it exists: over a part
// without one, the control's own text shows, as anywhere else on it
// (coordinator decision, 2026-11-02). A disabled control, unless disabled
// tooltips show, and a disabled or covered part end the search with
// nothing.
ToolTipSubject resolve_pointer_subject(const Control::Ptr& hovered, Point position,
                                       const ToolTipSettings& settings, const ToolTipCovers& covers) {
    for (Control::Ptr current = hovered; current; current = (*current).parent()) {
        const Rect bounds = (*current).absolute_bounds();
        if (!available(settings, *current)) return nothing_over(bounds);
        if ((*current).publishes_item_tool_tips()) {
            const std::vector<SemanticNode> nodes = (*current).tool_tip_items();
            const SemanticNode* node = node_at(nodes, position);
            if (node != nullptr) {
                const bool enabled = settings.show_when_disabled ||
                                     has_semantic_state((*node).states, SemanticState::enabled);
                if (!(*node).tool_tip.empty()) {
                    if (!enabled || covered(covers, *current, (*node).stable_id)) return nothing_over((*node).bounds);
                    return tool_tip_subject_of(current, (*node).stable_id, (*node).tool_tip, (*node).bounds);
                }
            }
        }
        if ((*current).tool_tip_presentation() != ToolTipPresentation::owner_item &&
            !(*current).tool_tip_text.read().empty()) {
            if (covered(covers, *current, {})) return nothing_over(bounds);
            return tool_tip_subject_of(current, {}, (*current).tool_tip_text.read(), bounds);
        }
    }
    const Rect region = hovered ? (*hovered).absolute_bounds() : Rect{};
    return nothing_over(region);
}

// The thing under keyboard focus: from the focused control outwards, the
// part the keyboard is on (the focused part, or the selected one of the
// focused control) of a control that publishes part tooltips, else the
// control's own text. A typed-text field shows its tooltip as a hint inside
// itself, so it ends the search with nothing.
ToolTipSubject resolve_focus_subject(const Control::Ptr& focused, const ToolTipSettings& settings,
                                     const ToolTipCovers& covers) {
    for (Control::Ptr current = focused; current; current = (*current).parent()) {
        const Rect bounds = (*current).absolute_bounds();
        if ((*current).tool_tip_presentation() == ToolTipPresentation::field_hint ||
            !available(settings, *current)) return nothing_over(bounds);
        if ((*current).publishes_item_tool_tips()) {
            const std::vector<SemanticNode> nodes = (*current).tool_tip_items();
            const SemanticNode* node = node_with_state(nodes, SemanticState::focused);
            if (node == nullptr && current == focused) node = node_with_state(nodes, SemanticState::selected);
            if (node != nullptr && !(*node).tool_tip.empty() && !(*node).bounds.empty()) {
                if (covered(covers, *current, (*node).stable_id)) return nothing_over((*node).bounds);
                return tool_tip_subject_of(current, (*node).stable_id, (*node).tool_tip, (*node).bounds);
            }
        }
        if ((*current).tool_tip_presentation() != ToolTipPresentation::owner_item &&
            !(*current).tool_tip_text.read().empty()) {
            if (covered(covers, *current, {})) return nothing_over(bounds);
            return tool_tip_subject_of(current, {}, (*current).tool_tip_text.read(), bounds);
        }
    }
    return nothing_over({});
}

bool same_tool_tip_thing(const ToolTipSubject& left, const ToolTipSubject& right) noexcept {
    const bool same = left.control.lock() == right.control.lock() && left.item_id == right.item_id &&
                      left.text.empty() == right.text.empty();
    return same;
}

} // namespace gui_forms::detail
