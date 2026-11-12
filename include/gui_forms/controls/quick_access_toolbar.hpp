#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/controls/tool_strip.hpp"
#include "gui_forms/controls/quick_access_toolbar_values.hpp"
#include "gui_forms/controls/quick_access_toolbar_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// One command the toolbar may show. Every declared command is listed, with a
// check, in the customize menu; `shown` is whether the toolbar shows it
// before the user customizes. `image` is the button's icon: a command with
// one shows only its icon, a command without one its text.
struct GUI_FORMS_API QuickAccessToolbarCommand final {
    std::shared_ptr<Command> command{};
    ImageId image{};
    bool shown{true};
};

// The user showed or hid a command from the customize menu, or the
// application did through set_command_shown.
struct GUI_FORMS_API QuickAccessToolbarCustomization final {
    std::string toolbar_id{};
    std::string command_id{};
    bool shown{};
};

struct GUI_FORMS_API QuickAccessToolbarPlacementChange final {
    std::string toolbar_id{};
    QuickAccessToolbarPlacement placement{QuickAccessToolbarPlacement::above_ribbon};
};

// The Quick Access Toolbar of a ribbon window (owner decisions RB-12 and
// RB-24): a ToolStrip of flat command buttons with a customize button at its
// end. The customize menu lists every declared command with a check that
// shows or hides it, then the placement choice. Buttons that do not fit move
// to the strip's overflow menu, opened from the overflow button left of the
// customize button, which stays visible.
//
// It is a ToolStrip, so it keeps ToolStrip's items, overflow, keyboard model
// and item anatomy; it admits only command buttons (commands only, D2-2), and
// set_items with any other item throws std::invalid_argument. The
// application declares commands with set_commands instead.
class GUI_FORMS_API QuickAccessToolbar final : public ToolStrip, public QuickAccessToolbarCapabilities<QuickAccessToolbar> {
public:
    // The most commands the toolbar may declare.
    static constexpr std::size_t maximum_commands = 20U;

    explicit QuickAccessToolbar(StableId stable_id);
    ~QuickAccessToolbar() override;

    [[nodiscard]] const std::vector<QuickAccessToolbarCommand>& commands() const noexcept {
        return commands_;
    }
    // Declares the commands, in toolbar order, and shows those marked shown,
    // replacing any customization. Throws std::invalid_argument for a missing
    // command, an empty or repeated command ID, or more than
    // maximum_commands.
    void set_commands(std::vector<QuickAccessToolbarCommand> commands);
    // The IDs of the commands the toolbar shows, in toolbar order. After
    // set_shown_command_ids this may include IDs that name no declared
    // command: they are kept in place and show nothing, so a saved state
    // from a later version survives a round trip.
    [[nodiscard]] const std::vector<std::string>& shown_command_ids() const noexcept {
        return shown_ids_;
    }
    // Shows exactly these commands, in this order (restoring saved state).
    // Emits no customized events. Throws std::invalid_argument for an empty,
    // invalid UTF-8 or repeated ID, or more than maximum_commands IDs.
    void set_shown_command_ids(std::vector<std::string> ids);
    // Whether a declared command is shown. Throws std::out_of_range for an
    // undeclared ID.
    [[nodiscard]] bool command_shown(std::string_view command_id) const;
    // Shows a declared command (at the toolbar's end) or hides it, and emits
    // customized when that changes anything. Throws std::out_of_range for an
    // undeclared ID.
    void set_command_shown(std::string_view command_id, bool shown);

    // Opens the customize menu below the customize button, or closes it.
    bool show_customize_menu(MenuOpenMode mode = MenuOpenMode::keyboard);
    void close_customize_menu() noexcept;
    [[nodiscard]] bool customize_menu_open() const noexcept;
    // The customize menu's rows: one check row per declared command (ID
    // "<toolbar>.customize.<command>"), a separator, and the placement row
    // (ID "<toolbar>.customize.placement").
    [[nodiscard]] const std::vector<MenuItemSpec>& customize_menu_items() const noexcept;

    [[nodiscard]] Event<const QuickAccessToolbarCustomization&>& customized() noexcept {
        return customized_;
    }
    [[nodiscard]] Event<const QuickAccessToolbarPlacementChange&>& placement_changed() noexcept {
        return placement_changed_;
    }

    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

    // Stage T1 anatomy, as dot members: ToolStrip's parts and the customize
    // button. Its states are Control's; its item system is ToolStrip's.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry grip;
        PartGeometry overflow_button;
        PartGeometry overflow_glyph;
        PartGeometry customize_button;
        PartGeometry customize_glyph;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void validate_items(const std::vector<ToolStripItemSpec>& items) const override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // The customize button and its glyph, from the strip's one layout pass;
    // on_paint, hit testing and publish_part_geometry all read it.
    struct CustomizeGeometry final {
        Rect button{};
        Rect glyph{};
    };
    [[nodiscard]] CustomizeGeometry customize_geometry() const;
    [[nodiscard]] bool customize_at(Point window_point) const;
    [[nodiscard]] std::size_t declared_index(std::string_view command_id) const;
    [[nodiscard]] std::string customize_row_id(std::string_view command_id) const;
    void rebuild_items();
    void rebuild_menu();
    void on_menu_invoked(const MenuItemInvocation& invocation);
    void on_menu_open_changed(bool open);

    std::vector<QuickAccessToolbarCommand> commands_{};
    std::vector<std::string> shown_ids_{};
    // One private check Command per declared command, for its menu row: the
    // row's check shows the command's presence on the toolbar, and choosing
    // the row must not execute the command itself.
    std::vector<std::shared_ptr<Command>> row_commands_{};
    std::shared_ptr<Command> placement_command_{};
    std::vector<MenuItemSpec> menu_items_{};
    std::unique_ptr<ContextMenu> menu_{};
    SubscriptionToken menu_invoked_{};
    SubscriptionToken menu_changed_{};
    bool customize_hot_{};
    bool customize_open_{};
    bool focused_{};
    Event<const QuickAccessToolbarCustomization&> customized_{};
    Event<const QuickAccessToolbarPlacementChange&> placement_changed_{};
    // property_codemod begin QuickAccessToolbar (tools/property_families/quick_access_toolbar.json; generated, do not edit)
private:
    friend struct QuickAccessToolbarPlacementPropertyCapability<QuickAccessToolbar>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(placement)& cell, QuickAccessToolbarPlacement& incoming);
    void written(const decltype(placement)& cell);
    // property_codemod end QuickAccessToolbar
};

} // namespace gui_forms
