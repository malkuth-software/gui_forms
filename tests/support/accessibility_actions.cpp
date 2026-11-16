#include "accessibility_actions.hpp"

#include <cstdio>

namespace gui_forms::test_support {

bool has_semantic_action(const SemanticNode& node, SemanticAction action) {
    for (const SemanticAction published : node.actions) {
        if (published == action) return true;
    }
    return false;
}

std::string semantic_number_text(double value) {
    char buffer[64] = {};
    std::snprintf(buffer, sizeof(buffer), "%g", value);
    return std::string(buffer);
}

const SemanticNode* find_semantic_node(const std::vector<SemanticNode>& roots,
                                       std::string_view stable_id) {
    for (const SemanticNode& node : roots) {
        if (node.stable_id == stable_id) return &node;
        const SemanticNode* found = find_semantic_node(node.children, stable_id);
        if (found != nullptr) return found;
    }
    return nullptr;
}

namespace {

constexpr SemanticAction default_action_order[] = {
    SemanticAction::press, SemanticAction::select, SemanticAction::increment,
    SemanticAction::set_value, SemanticAction::show_menu, SemanticAction::expand,
    SemanticAction::collapse,
};

bool default_action_of(const SemanticNode& node, SemanticAction& action) {
    if (!has_semantic_state(node.states, SemanticState::enabled)) return false;
    const bool at_maximum = node.numeric_value.present() && node.maximum_value.present() &&
        *node.numeric_value.value() >= *node.maximum_value.value();
    for (const SemanticAction candidate : default_action_order) {
        const bool already_selected = candidate == SemanticAction::select &&
            has_semantic_state(node.states, SemanticState::selected);
        const bool cannot_grow = candidate == SemanticAction::increment && at_maximum;
        if (has_semantic_action(node, candidate) && !already_selected && !cannot_grow) {
            action = candidate;
            return true;
        }
    }
    return false;
}

bool is_iso_date(const std::string& text) {
    if (text.size() != 10U || text[4] != '-' || text[7] != '-') return false;
    for (std::size_t index = 0U; index < text.size(); ++index) {
        if (index == 4U || index == 7U) continue;
        if (text[index] < '0' || text[index] > '9') return false;
    }
    return true;
}

bool has_letter(const std::string& text) {
    for (const char character : text) {
        if ((character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) return true;
    }
    return false;
}

void append_signature(const SemanticNode& node, std::string& signature) {
    signature += node.stable_id;
    signature += '|';
    signature += std::to_string(static_cast<std::uint32_t>(node.states));
    signature += '|';
    signature += node.value;
    signature += '|';
    if (node.numeric_value.present()) signature += semantic_number_text(*node.numeric_value.value());
    signature += '{';
    for (const SemanticNode& child : node.children) append_signature(child, signature);
    signature += '}';
}

} // namespace

const SemanticNode* default_action_node(const SemanticNode& control, SemanticAction& action) {
    if (default_action_of(control, action)) return &control;
    for (const SemanticNode& child : control.children) {
        const SemanticNode* found = default_action_node(child, action);
        if (found != nullptr) return found;
    }
    return nullptr;
}

std::string default_action_value(const SemanticNode& node) {
    if (node.role == SemanticRole::calendar || node.role == SemanticRole::date_picker) {
        return node.value.find("15") == std::string::npos ? "2026-10-15" : "2026-10-16";
    }
    if (node.numeric_value.present()) {
        const double target =
            node.maximum_value.present() ? *node.maximum_value.value() : *node.numeric_value.value() + 1.0;
        return semantic_number_text(target);
    }
    if (is_iso_date(node.value)) {
        std::string next = node.value;
        next[8] = next[8] == '1' ? '2' : '1';
        next[9] = '5';
        return next;
    }
    if (has_letter(node.value)) return node.value + " (changed)";
    return "12345";
}

std::string semantic_subtree_signature(const std::vector<SemanticNode>& roots,
                                       std::string_view stable_id) {
    std::string signature;
    const SemanticNode* node = find_semantic_node(roots, stable_id);
    if (node != nullptr) append_signature(*node, signature);
    return signature;
}

bool same_semantic_roots(const SemanticSnapshot& left, const SemanticSnapshot& right) {
    if (left.roots.size() != right.roots.size()) return false;
    for (std::size_t index = 0U; index < left.roots.size(); ++index) {
        if (left.roots[index].stable_id != right.roots[index].stable_id) return false;
    }
    return true;
}

} // namespace gui_forms::test_support
