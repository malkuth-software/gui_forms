#pragma once

#include "gui_forms/basic_controls.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/text.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>

namespace gui_forms::menu_detail {

inline constexpr double menu_width = 268.0;
inline constexpr double command_row_height = 28.0;
inline constexpr double separator_row_height = 9.0;
inline constexpr double menu_border = 2.0;

[[nodiscard]] inline std::string menu_display_text(std::string_view text) {
    return parse_mnemonic_text(text).display_text;
}

struct MenuSnapshot final {
    std::string stable_id;
    MenuItemKind kind{MenuItemKind::command};
    std::shared_ptr<Command> command;
    CommandState command_state;
    std::string text;
    std::vector<MenuSnapshot> children;
    bool italic{};
};

struct ConsecutiveSeparators final {
    [[nodiscard]] bool operator()(const MenuSnapshot& left,
                                  const MenuSnapshot& right) const noexcept {
        return left.kind == MenuItemKind::separator &&
               right.kind == MenuItemKind::separator;
    }
};

inline double row_height(const MenuSnapshot& item,
                         double text_scale) noexcept {
    return (item.kind == MenuItemKind::separator ? separator_row_height
                                                 : command_row_height) *
           text_scale;
}

// A merge (merge_strips) may leave a submenu without items: with
// `allow_empty_submenus` such a submenu is valid, and shows only once it has
// items again (prune_empty_submenus).
inline void validate_specs(const std::vector<MenuItemSpec>& items,
                           std::unordered_set<std::string>& identities,
                           std::size_t depth, std::size_t& count,
                           bool allow_empty_submenus = false) {
    if (depth > maximum_menu_depth) {
        throw std::invalid_argument(
            "ContextMenu nesting exceeds the bounded depth");
    }
    for (const MenuItemSpec& item : items) {
        ++count;
        if (count > maximum_menu_items) {
            throw std::invalid_argument(
                "ContextMenu exceeds the bounded item count");
        }
        // Each row's StableId ends in its item ID, so the ID follows the
        // StableId segment rule.
        if (!valid_stable_id(item.stable_id) || !validate_utf8(item.text).valid() ||
            !identities.insert(item.stable_id).second) {
            throw std::invalid_argument(
                "ContextMenu items require unique stable IDs of '.'-separated lowercase dash segments "
                "and valid UTF-8");
        }
        if (item.kind == MenuItemKind::separator) {
            if (item.command || !item.children.empty()) {
                throw std::invalid_argument(
                    "ContextMenu separator may not own a command or children");
            }
        } else if (item.kind == MenuItemKind::submenu) {
            if (item.command || item.text.empty() ||
                (item.children.empty() && !allow_empty_submenus)) {
                throw std::invalid_argument(
                    "ContextMenu submenu requires text and children but no command");
            }
        } else if (!item.command || !item.children.empty()) {
            throw std::invalid_argument(
                "ContextMenu command/check/radio item requires one shared command");
        }
        validate_specs(item.children, identities, depth + 1U, count,
                       allow_empty_submenus);
    }
}

// The menu to show: `items` without the submenus a merge left empty, at
// every depth (a submenu emptied by pruning goes too).
[[nodiscard]] inline std::vector<MenuItemSpec> prune_empty_submenus(
    const std::vector<MenuItemSpec>& items) {
    std::vector<MenuItemSpec> shown;
    shown.reserve(items.size());
    for (const MenuItemSpec& item : items) {
        if (item.kind != MenuItemKind::submenu) {
            shown.push_back(item);
            continue;
        }
        MenuItemSpec kept = item;
        kept.children = prune_empty_submenus(item.children);
        if (!kept.children.empty()) shown.push_back(std::move(kept));
    }
    return shown;
}

inline std::vector<MenuSnapshot> snapshot_items(
    const std::vector<MenuItemSpec>& specs) {
    std::vector<MenuSnapshot> result;
    result.reserve(specs.size());
    for (const MenuItemSpec& spec : specs) {
        MenuSnapshot snapshot;
        snapshot.stable_id = spec.stable_id;
        snapshot.kind = spec.kind;
        snapshot.command = spec.command;
        snapshot.text = spec.text;
        snapshot.italic = spec.italic;
        if (spec.command) {
            snapshot.command_state = (*spec.command).state();
            if (!snapshot.command_state.visible) continue;
            if (snapshot.text.empty()) snapshot.text = snapshot.command_state.text;
        }
        snapshot.children = snapshot_items(spec.children);
        if (snapshot.kind == MenuItemKind::submenu && snapshot.children.empty()) {
            continue;
        }
        result.push_back(std::move(snapshot));
    }
    while (!result.empty() && result.front().kind == MenuItemKind::separator) {
        result.erase(result.begin());
    }
    while (!result.empty() && result.back().kind == MenuItemKind::separator) {
        result.pop_back();
    }
    result.erase(std::unique(result.begin(), result.end(),
                             ConsecutiveSeparators{}),
                 result.end());
    return result;
}

[[nodiscard]] inline bool snapshot_enabled(
    const MenuSnapshot& item) noexcept;

struct EnabledSnapshot final {
    [[nodiscard]] bool operator()(const MenuSnapshot& child) const noexcept {
        return snapshot_enabled(child);
    }
};

[[nodiscard]] inline bool snapshot_enabled(
    const MenuSnapshot& item) noexcept {
    if (item.kind == MenuItemKind::separator) return false;
    if (item.kind != MenuItemKind::submenu) return item.command_state.enabled;
    return std::any_of(item.children.begin(), item.children.end(),
                       EnabledSnapshot{});
}

inline Color menu_ink(const MenuSnapshot& item,
                      const BasicControlStyle& style) noexcept {
    if (!snapshot_enabled(item)) {
        return style.disabled_text;
    }
    if (item.command_state.destructive) return Color::rgba(145, 44, 42);
    return style.text;
}

} // namespace gui_forms::menu_detail
