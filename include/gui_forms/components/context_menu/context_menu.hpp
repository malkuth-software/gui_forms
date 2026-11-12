#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/components/context_menu/context_menu_capabilities/context_menu_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {

enum class MenuItemKind : std::uint8_t {
    command,
    check,
    radio,
    separator,
    submenu,
};

// Pointer opening keeps the overlay focused without selecting a command until
// hover or keyboard navigation. Keyboard opening selects the first enabled row.
enum class MenuOpenMode : std::uint8_t { keyboard, pointer };

// Declarative presentation of shared command authority. Command state is
// snapshotted immediately before the menu opens; one invocation still flows
// through Command::execute regardless of pointer, keyboard, or semantic input.
// How an item of a strip merged into another strip joins it (Windows Forms'
// MergeAction; merge_strips). A source item is matched in the target's
// collection at its own level by text (ASCII case-insensitive), else by its
// merge_index as a position.
enum class MergeAction : std::uint8_t {
    // Moves to the end of the target's collection; no match is needed.
    append,
    // With a merge_index, moves into the target's collection
    // at merge_index (at the end when past it).
    insert,
    // Takes the matched item's place; the matched item leaves the target.
    replace,
    // The matched item leaves the target; the source item stays.
    remove,
    // Stays; when both it and the matched item have menus, its menu's items
    // merge into the matched item's menu.
    match_only,
};

struct GUI_FORMS_API MenuItemSpec final {
    MenuItemSpec() = default;
    MenuItemSpec(std::string stable_identity, MenuItemKind item_kind,
                 std::shared_ptr<Command> item_command = {},
                 GUI_FORMS_LOCALIZABLE(text) std::string item_text = {},
                 std::vector<MenuItemSpec> item_children = {})
        : stable_id(std::move(stable_identity)), kind(item_kind),
          command(std::move(item_command)), text(std::move(item_text)),
          children(std::move(item_children)) {}

    std::string stable_id;
    MenuItemKind kind{MenuItemKind::command};
    // The shared Command a command, check or radio item runs and follows
    // (text, enabled, checked, shortcut). Optional (GAPS E9): an item
    // without one shows its own text, `enabled` and `checked`, and choosing
    // it raises item_invoked with an empty command_id; the application
    // updates `checked` itself (set_items).
    std::shared_ptr<Command> command;
    GUI_FORMS_LOCALIZABLE(text) std::string text;
    std::vector<MenuItemSpec> children;
    // Used only by an item without a Command: whether it can be chosen, and
    // whether a check or radio item shows its mark.
    bool enabled{true};
    bool checked{};
    // The row's text is set in the italic style. A presentational bridge:
    // no anatomy state names why a row is set apart (PageStrip's list sets
    // the pages it does not show in italic, owner decision DK-42, PT6).
    bool italic{};
    // The item's icon, drawn in the row's gutter (an image of the window's
    // registry, at 16 x 16 logical units); none draws no icon. A checked
    // check or radio item shows its mark there instead.
    ImageId image{};
    // How the item joins another strip's menu when its strip merges
    // (MergeAction), and its position there; none matches by text only.
    MergeAction merge_action{MergeAction::append};
    std::optional<std::size_t> merge_index{};
};

struct GUI_FORMS_API MenuItemInvocation final {
    std::string menu_id;
    std::string item_id;
    std::string command_id;
    std::string source_id;
};

inline constexpr std::size_t maximum_menu_depth = 8U;
inline constexpr std::size_t maximum_menu_items = 1024U;

// Retained overlay-menu controller. Its visual rows are temporary overlay
// presentations, while MenuItemSpec identities and bound Command instances are
// stable application state. Click-away/Escape revoke the overlay and restore the
// invoker's focus scope deterministically.
class GUI_FORMS_API ContextMenu final : public Component, public ContextMenuCapabilities<ContextMenu> {
public:
    explicit ContextMenu(std::string stable_id);
    ~ContextMenu() override;

    [[nodiscard]] const std::string& stable_id() const& noexcept {
        return stable_id_;
    }
    const std::string& stable_id() const&& = delete;
    [[nodiscard]] const std::vector<MenuItemSpec>& items() const& noexcept {
        return items_;
    }
    const std::vector<MenuItemSpec>& items() const&& = delete;
    void set_items(std::vector<MenuItemSpec> items);

    // Unknown modes throw invalid_argument without closing an existing overlay.
    void show(const Control::Ptr& owner, const Point window_position,
              const MenuOpenMode mode = MenuOpenMode::keyboard);
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

    [[nodiscard]] Event<const MenuItemInvocation&>& item_invoked() noexcept {
        return item_invoked_;
    }
    [[nodiscard]] Event<bool>& open_changed() noexcept { return open_changed_; }

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;

private:
    friend class MenuStrip;
    // The ribbon closes its two-pane menu on its submenu's outside press.
    friend class Ribbon;
    struct Impl;

    void set_root_navigation_handler(std::function<bool(int)> handler);
    void set_outside_pointer_handler(
        std::function<bool(const PointerEvent&)> handler);

    std::string stable_id_;
    std::vector<MenuItemSpec> items_;
    std::unique_ptr<Impl> impl_;
    Event<const MenuItemInvocation&> item_invoked_;
    Event<bool> open_changed_;
    // property_codemod begin ContextMenu (tools/property_families/context_menu.json; generated, do not edit)
private:
    friend struct ContextMenuPreferredWidthCapability<ContextMenu>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(preferred_width)& cell, double& incoming);
    void written(const decltype(preferred_width)& cell);
    // property_codemod end ContextMenu
};

} // namespace gui_forms
