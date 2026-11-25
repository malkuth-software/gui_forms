#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/component.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/text/localizable/localizable_property.hpp"
#include "gui_forms/controls/menu_strip_capabilities.hpp"

#include <stxlib/optional.hpp>

namespace gui_forms {

namespace detail {
struct MenuStripMergeHistory;
class StripMergeAccess;
}

struct GUI_FORMS_API MenuStripItemSpec final {
    std::string stable_id{};
    GUI_FORMS_LOCALIZABLE(text) std::string text{};
    std::vector<MenuItemSpec> items{};
    bool enabled{true};
    bool visible{true};
    // How the menu joins another menu strip when its strip merges
    // (MergeAction, merge_strips), and its position there; none matches by
    // text only.
    MergeAction merge_action{MergeAction::append};
    stxlib::Optional<std::size_t> merge_index{};
    // Whether this menu lists the window's MDI children when it opens
    // (WinForms' MdiWindowListItem; MenuStrip::mdi_window_list_item): a
    // separator, then one checkable row per child of the window's document
    // area ("&1 Title" to "&9 Title", the active child checked), then "More
    // Windows..." when there are more than nine. Choosing a row activates
    // its child (Window::activate_mdi_child) and raises no item_invoked;
    // "More Windows..." raises item_invoked with the item ID
    // "mdi-window-list.more", for the application's own chooser.
    bool window_list{};
};

// The caption buttons of a maximized MDI child, shown at the end of its
// window's main menu strip (MenuStrip::document_frame_buttons).
enum class MenuStripFrameButton : std::uint8_t { minimize, restore, close };

struct GUI_FORMS_API MenuStripInvocation final {
    std::string menu_strip_id{};
    std::string top_level_id{};
    MenuItemInvocation item{};
};

// Retained top-level application menu. The bar stays in ordinary layout; its
// active menu is presented by ContextMenu as a separate window overlay root.
// Pointer, keyboard, and semantic activation converge on the same shared
// Command instances contained by MenuItemSpec.
// ic: gui_forms::MenuStrip: generated from include/gui_forms/controls/menu_strip.ic.toml by tools/ic_declarations.py;
// ic: edit the declaration, not these lines. Thread: ui, unless a call says otherwise.
class GUI_FORMS_API MenuStrip final : public Control, public MenuStripCapabilities<MenuStrip> {
public:
    explicit MenuStrip(StableId stable_id);
    ~MenuStrip() override;

    [[nodiscard]] const std::vector<MenuStripItemSpec>& items() const& noexcept {
        return items_;
    }
    const std::vector<MenuStripItemSpec>& items() const&& = delete;
    void set_items(std::vector<MenuStripItemSpec> items);
    [[nodiscard]] stxlib::Optional<std::size_t> active_index() const noexcept {
        return active_index_;
    }
    // Unknown modes throw invalid_argument before reusing or replacing an overlay.
    bool open(const std::size_t index, const MenuOpenMode mode = MenuOpenMode::keyboard);
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;

    [[nodiscard]] Event<const MenuStripInvocation&>& item_invoked() noexcept {
        return item_invoked_;
    }
    [[nodiscard]] Event<stxlib::Optional<std::size_t>>& open_changed() noexcept {
        return open_changed_;
    }

    // -- the multiple document interface (DK decisions 60 to 63) --
    // The menu the window list joins: the first visible item whose spec sets
    // window_list, or none (WinForms' MdiWindowListItem). The list is read
    // from the attached window's document area (Window::mdi_children) each
    // time the menu opens.
    [[nodiscard]] stxlib::Optional<std::size_t> mdi_window_list_item() const noexcept;
    // Whether the strip shows a maximized MDI child's caption buttons
    // (minimize, restore, close) at its end, as WinForms' MDI shows them on
    // the main menu strip. The docking site turns them on while a document
    // frame is maximized and the strip is its window's main menu strip
    // (Window::main_menu_strip), and listens to frame_button_pressed.
    void set_document_frame_buttons(bool shown);
    [[nodiscard]] bool document_frame_buttons() const noexcept { return frame_buttons_; }
    // Raised when a shown frame button is pressed and released (pointer) or
    // pressed (semantic press).
    [[nodiscard]] Event<MenuStripFrameButton>& frame_button_pressed() noexcept { return frame_button_pressed_; }
    // Where a frame button shows, in the strip's coordinates; empty while
    // the buttons are off.
    [[nodiscard]] Rect document_frame_button_bounds(MenuStripFrameButton button) const;

    [[nodiscard]] Size measure(Size available) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id,
                                  SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(
        std::size_t item) const override;
    // Stage T1 anatomy, as dot members; its states are Control's.
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry face;
        PartGeometry divider;
        PartGeometry focus;
        // The icon gutter down the left of each menu the strip opens (a
        // drop-down's band behind the icons and check marks). The strip
        // itself has none, so its geometry here is empty; the drop-down
        // panels paint it as `menu-strip::part(gutter)`.
        PartGeometry gutter;
    };
    [[nodiscard]] Parts parts() const;
    struct GUI_FORMS_API ItemParts final {
        PartGeometry menu_item;
        PartGeometry menu_label;
        PartGeometry menu_focus;
    };
    struct GUI_FORMS_API ItemStates final {
        bool hot{};
        bool open{};
        bool selected{};
        bool disabled{};
    };
    struct GUI_FORMS_API Item final {
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    // The items currently realized, in paint order (owner decision 5).
    [[nodiscard]] std::vector<Item> realized_items() const;

protected:
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_key_down(KeyEventArgs& arguments) override;
    // In menu-bar mode (the strip focused, no menu open) a letter opens the
    // item it marks as mnemonic, or else the item it starts, as Windows'
    // menu bar does.
    void on_key_press(KeyPressEventArgs& arguments) override;
    void on_attached_to_window() override;
    void on_detached_from_window() noexcept override;

    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_dispose() noexcept override;
    [[nodiscard]] bool mnemonic_matches(
        char32_t character) const noexcept override;
    bool process_mnemonic_self(char32_t character) override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item,
                                        std::span<PartGeometry> geometry) const override;
    // The model position of a realized (visible) top-level item. Throws
    // std::out_of_range for an item past realized_item_count().
    [[nodiscard]] std::size_t realized_item_index(std::size_t item) const;

private:
    friend class detail::StripMergeAccess;
    // set_items' work; a merge may leave a menu empty.
    void apply_items(std::vector<MenuStripItemSpec> items, bool merged);
    [[nodiscard]] std::vector<Rect> item_bounds() const;
    [[nodiscard]] double preferred_height() const;
    [[nodiscard]] stxlib::Optional<std::size_t> index_at(Point window_point) const;
    // Whether `held` (active_index_ or hot_index_) holds item `index`.
    [[nodiscard]] static bool holds(const stxlib::Optional<std::size_t>& held, std::size_t index) noexcept;
    [[nodiscard]] stxlib::Optional<std::size_t> next_enabled(
        std::size_t start, int direction) const;
    bool navigate_root(int direction);
    bool handle_overlay_pointer(const PointerEvent& event);
    void on_overlay_item_invoked(const MenuItemInvocation& invocation);
    void on_overlay_open_changed(bool open_state);
    void set_hot(stxlib::Optional<std::size_t> index);
    // The frame button at a point in the strip's coordinates; 0 for none,
    // else the button's value plus one (frame_hot_ and frame_pressed_ hold
    // the same coding).
    [[nodiscard]] std::uint8_t frame_button_at(Point local) const;
    void set_frame_hot(std::uint8_t button);
    // Menu-bar keyboard entry (Windows field report D13), as Windows and
    // Windows Forms give a window's menu bar: Alt pressed and released with
    // no other key between, or F10, focuses the window's menu bar with its
    // first item hot; the same again, or Escape with no menu open, gives
    // the focus back to the control that had it, as does a command chosen
    // from a menu so entered (or opened by its mnemonic). The window's menu
    // bar is its main menu strip (Window::main_menu_strip), or without one
    // the first visible strip in the window's tree order. The raw key
    // states come from the window (SurfaceRoot::key_state_changed), so no
    // control's handling of Alt or F10 is needed.
    void on_window_key_state(const KeyStateChange& change);
    [[nodiscard]] bool is_window_menu_bar() const;
    void enter_menu_bar();
    void leave_menu_bar();
    // Whether the strip underlines its mnemonics: while Alt is held, and
    // while the strip has the focus from the keyboard (Windows' keyboard
    // cues).
    [[nodiscard]] bool keyboard_cues_shown() const;

    std::vector<MenuStripItemSpec> items_{};
    std::unique_ptr<ContextMenu> overlay_{};
    stxlib::Optional<std::size_t> active_index_{};
    stxlib::Optional<std::size_t> hot_index_{};
    std::string selected_item_id_{};
    bool focused_{};
    bool switching_{};
    // The merges into this strip, oldest first; none until the first.
    std::unique_ptr<detail::MenuStripMergeHistory> merge_history_{};
    SubscriptionToken overlay_invoked_{};
    SubscriptionToken overlay_changed_{};
    // The window's raw key states (on_window_key_state), while attached.
    SubscriptionToken key_state_subscription_{};
    // The control that had the focus when the strip took it from the
    // keyboard (enter_menu_bar, a mnemonic), given it back by
    // leave_menu_bar; observes only, and is dropped when the strip loses
    // the focus some other way.
    Control::WeakPtr focus_return_{};
    // Alt went down alone and nothing else has gone down since.
    bool alt_tap_armed_{};
    Event<const MenuStripInvocation&> item_invoked_{};
    Event<stxlib::Optional<std::size_t>> open_changed_{};
    // The maximized MDI child's buttons: shown, hot and pressed.
    bool frame_buttons_{};
    std::uint8_t frame_hot_{};
    std::uint8_t frame_pressed_{};
    Event<MenuStripFrameButton> frame_button_pressed_{};
    // property_codemod begin MenuStrip (tools/property_families/menu_strip.json; generated, do not edit)
private:
    friend struct MenuStripUseMnemonicCapability<MenuStrip>;
    friend struct MenuStripItemPaddingCapability<MenuStrip>;
    friend struct FontCapability<MenuStrip>;
    friend struct MenuStripAllowMergeCapability<MenuStrip>;
    friend struct MenuStripSelectedItemIdCapability<MenuStrip>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(use_mnemonic)& cell, bool& incoming);
    void written(const decltype(use_mnemonic)& cell);
    // ic: Requires finite(incoming) and incoming >= 4 and incoming <= 64, else failure invalid_argument, unchanged.
    void prepare_write(const decltype(item_padding)& cell, double& incoming);
    void written(const decltype(item_padding)& cell);
    void prepare_write(const decltype(font)& cell, FontSpec& incoming);
    void written(const decltype(font)& cell);
    void prepare_write(const decltype(allow_merge)& cell, bool& incoming);
    void written(const decltype(allow_merge)& cell);
    void write_view(const decltype(selected_item_id)& cell, std::string incoming);
    [[nodiscard]] std::string read_view(const decltype(selected_item_id)& cell) const;
    // property_codemod end MenuStrip
};

} // namespace gui_forms
