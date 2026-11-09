#pragma once

#include "gui_forms/commands.hpp"
#include "gui_forms/components/context_menu/context_menu.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/image_list.hpp"
#include "gui_forms/pages/page_set/page_set.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms/controls/pages/navigation_pane/navigation_pane_capabilities/navigation_pane_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// The part of a navigation pane's state a program saves with its settings
// (owner decision DK-33: the framework never saves on its own). Pages are
// named by identity; `page_order` is the pane's page order and
// `hidden_pages` the pages the user hid from the buttons, in page order.
// `button_count` is the number of large buttons the user chose, which the
// pane shows as far as its height allows.
struct NavigationPaneState final {
    bool collapsed{};
    std::size_t button_count{};
    std::vector<PageId> hidden_pages{};
    std::vector<PageId> page_order{};

    friend bool operator==(const NavigationPaneState& left, const NavigationPaneState& right) {
        const bool equal = left.collapsed == right.collapsed && left.button_count == right.button_count &&
                           left.hidden_pages == right.hidden_pages && left.page_order == right.page_order;
        return equal;
    }
    friend bool operator!=(const NavigationPaneState& left, const NavigationPaneState& right) {
        const bool different = !(left == right);
        return different;
    }
};

// What applying a saved state found: identities the pane's set does not hold
// (a page the program no longer adds), which were skipped.
struct NavigationPaneRestore final {
    std::size_t unknown_pages{};
};

// "Navigation pane options..." was chosen from the configure menu. The pane
// has no options dialog of its own (owner decision DK-39, question P30); the
// program shows its own.
struct NavigationPaneOptionsRequest final {
    std::string pane_id{};
};

// Whether a realized page item is a large button or an icon (in the overflow
// strip, or in the collapsed bar).
enum class NavigationPaneItemKind : std::uint8_t { large_button, icon };

// The Outlook-style navigation pane (owner decision DK-39; PageView study
// 3.6): a header with the selected page's title and a collapse button, the
// selected page's content, a grip, one large button per page and an overflow
// strip of icons for the pages that do not fit as large buttons, ending in a
// configure button whose menu shows more or fewer buttons, lists the pages
// the user hid and raises options_requested. Collapsed, the pane is a narrow
// bar with an expand button, the selected page's title read bottom to top
// (decision DK-42) and the large buttons as icons; the title opens the
// content in an overlay over the window, which an outside click closes and
// passes on (decision DK-27, E2-1) and Escape closes.
//
// The pane owns its PageSet (pinning off, neighbour fallback): pages are
// added, removed, renamed and selected through pages(). A page's content is
// a control given as a PageControlContent (the PageView family's content
// record, so a page taken from a PageView can be put into the pane with its
// control); the pane shows the selected page's control as the only child of
// its content panel and removes it when the selection moves. A page without
// content shows an empty content area.
//
// Large buttons and icons are tabs of one tab list (decision DK-39, P34-1)
// whose selected tab controls the content panel (a tab_panel); the grip is a
// split pane whose value is the number of large buttons. Every part is
// painted through paint_part; the buttons use the menu_item family until the
// default theme (T4) draws Plan Paint's tab material (decision DK-42).
class NavigationPane : public Control, public NavigationPaneCapabilities<NavigationPane> {
public:
    static constexpr bool initialize_tree_after_construction = true;

    explicit NavigationPane(StableId stable_id);
    ~NavigationPane() override;

    // Creates the content panel, ``<stable_id>.content``. make_control calls
    // it; do not call it yourself.
    void initialize_control_tree();

    // The pages. Edits go through the set; the pane follows its events.
    [[nodiscard]] PageSet& pages() noexcept { return pages_; }
    [[nodiscard]] const PageSet& pages() const noexcept { return pages_; }

    // The large buttons shown after layout: the chosen count, limited by the
    // pages not hidden and by the height. Zero while collapsed or unlaid.
    [[nodiscard]] std::size_t visible_button_count() const;
    // One more or one fewer large button than are shown, as the grip's
    // keyboard steps and the configure menu do. False when nothing changes.
    bool show_more_buttons();
    bool show_fewer_buttons();
    [[nodiscard]] bool page_hidden(const PageId& page) const;
    // Hides a page from the buttons and icons (it stays in the set and in
    // the configure menu's hidden pages), or shows it again. False, changing
    // nothing, for a page not in the set or already in that state.
    bool set_page_hidden(const PageId& page, bool hidden);
    // The hidden pages, in page order.
    [[nodiscard]] std::vector<PageId> hidden_pages() const;

    // The collapsed pane's content overlay. Opening is refused (false) unless
    // the pane is collapsed and attached to a window.
    [[nodiscard]] bool overlay_open() const noexcept { return overlay_open_; }
    bool open_overlay();
    void close_overlay() noexcept;
    // The configure menu. Opening is refused (false) while collapsed or
    // detached. Throws std::invalid_argument for an unknown mode.
    [[nodiscard]] bool configure_menu_open() const noexcept;
    bool show_configure_menu(MenuOpenMode mode = MenuOpenMode::keyboard);
    void close_configure_menu() noexcept;

    // The saved part of the state, and its restoration: the order is applied
    // to the set in one batch, unknown identities are skipped and counted,
    // and a hidden page the set does not hold is not kept. Apply it after the
    // pages are added. Throws std::invalid_argument for a repeated identity
    // and std::out_of_range for a button count above
    // PageSetLimits::maximum_capacity, changing nothing.
    [[nodiscard]] NavigationPaneState state() const;
    NavigationPaneRestore apply_state(const NavigationPaneState& state);

    // Raised by "Navigation pane options..." in the configure menu.
    [[nodiscard]] Event<const NavigationPaneOptionsRequest&>& options_requested() noexcept {
        return options_requested_;
    }
    // Raised after any part of state() changed: collapsing or expanding, the
    // button count, a page hidden or shown, or the page order.
    [[nodiscard]] Event<const NavigationPaneState&>& state_changed() noexcept { return state_changed_; }
    // Raised after the overlay opened (true) or closed (false).
    [[nodiscard]] Event<bool>& overlay_open_changed() noexcept { return overlay_open_changed_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // Stage T1 anatomy, as dot members; its states are Control's. The overlay
    // parts are in the pane's coordinates although the overlay lies outside
    // it, and are present only while it is open.
    struct Parts final {
        PartGeometry region;
        PartGeometry header;
        PartGeometry header_icon;
        PartGeometry header_label;
        PartGeometry collapse_button;
        PartGeometry collapse_glyph;
        PartGeometry content;
        PartGeometry grip_bar;
        PartGeometry grip;
        PartGeometry button_area;
        PartGeometry overflow_area;
        PartGeometry configure_button;
        PartGeometry configure_glyph;
        PartGeometry collapsed_bar;
        PartGeometry collapsed_label;
        PartGeometry overlay_shadow;
        PartGeometry overlay_face;
        PartGeometry overlay_header;
        PartGeometry overlay_label;
        PartGeometry overlay_content;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;
    // Item system nav_page: a large button.
    struct PageButtonParts final {
        PartGeometry nav_page_face;
        PartGeometry nav_page_icon;
        PartGeometry nav_page_label;
    };
    // Item system nav_overflow: an icon. A page without an image shows the
    // first character of its title in nav_overflow_label instead.
    struct PageIconParts final {
        PartGeometry nav_overflow_face;
        PartGeometry nav_overflow_icon;
        PartGeometry nav_overflow_label;
    };
    struct ItemStates final {
        bool hot{};
        bool pressed{};
        bool selected{};
        bool focus_visible{};
    };
    struct Item final {
        NavigationPaneItemKind kind{NavigationPaneItemKind::large_button};
        PagePosition position{};
        PageId page{};
        std::string stable_id;
        // The parts of the item's own system; the other record is absent.
        PageButtonParts button;
        PageIconParts icon;
        ItemStates states;
    };
    // The large buttons, then the icons, in page order.
    [[nodiscard]] std::vector<Item> realized_items() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attachment_committed() noexcept override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    [[nodiscard]] ItemSystemIndex publish_realized_item_system(std::size_t item) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;

private:
    class ContentHost;
    class Flyout;

    // A place the pointer or the keyboard can be on.
    enum class TargetKind : std::uint8_t { none, collapse, caption, grip, configure, tab };
    struct Target final {
        TargetKind kind{TargetKind::none};
        // The tab's page; empty for the other kinds.
        PageId page{};

        friend bool operator==(const Target& left, const Target& right) {
            const bool equal = left.kind == right.kind && left.page == right.page;
            return equal;
        }
        friend bool operator!=(const Target& left, const Target& right) {
            const bool different = !(left == right);
            return different;
        }
    };
    // One realized tab: a large button or an icon.
    struct TabSlot final {
        NavigationPaneItemKind kind{NavigationPaneItemKind::large_button};
        std::size_t position{};
        Rect bounds{};
    };
    // Where everything sits, from one layout pass over the pane's rect.
    struct Layout final {
        bool collapsed{};
        Rect header{};
        Rect header_icon{};
        Rect header_label{};
        Rect collapse_button{};
        Rect collapse_glyph{};
        Rect content{};
        Rect grip_bar{};
        Rect grip{};
        Rect button_area{};
        Rect overflow_area{};
        Rect configure_button{};
        Rect configure_glyph{};
        Rect collapsed_bar{};
        Rect collapsed_label{};
        // The large buttons, then the icons, in page order.
        std::vector<TabSlot> tabs{};
        // Shown pages with neither a large button nor an icon, by position.
        std::vector<std::size_t> unfit{};
        // Pages not hidden, and the large buttons the height allows.
        std::size_t shown_count{};
        std::size_t button_capacity{};
        std::size_t large_count{};
    };
    // The overlay, in window coordinates, and its inner parts in its own.
    struct FlyoutLayout final {
        Rect window_bounds{};
        Rect face{};
        Rect header{};
        Rect label{};
        Rect content{};
    };

    [[nodiscard]] Layout layout() const;
    [[nodiscard]] Layout layout_for(Rect surface) const;
    // The two halves of layout_for: each fills `placed` for a non-empty
    // surface from the pages shown (positions, in order).
    void layout_collapsed(Rect surface, std::span<const std::size_t> shown, Layout& placed) const;
    void layout_expanded(Rect surface, std::span<const std::size_t> shown, Layout& placed) const;
    [[nodiscard]] FlyoutLayout flyout_layout() const;
    [[nodiscard]] PageButtonParts button_parts(const TabSlot& slot) const;
    [[nodiscard]] PageIconParts icon_parts(const TabSlot& slot) const;
    [[nodiscard]] Rect target_bounds(const Layout& placed, const Target& target) const;
    [[nodiscard]] Target target_at(const Layout& placed, Point local) const;
    [[nodiscard]] AnatomyStateSet target_states(const Target& target) const;
    [[nodiscard]] AnatomyStateSet tab_states(std::size_t position) const;
    [[nodiscard]] ImageId page_image(std::size_t position) const;
    [[nodiscard]] std::string page_text(std::size_t position) const;
    [[nodiscard]] std::string selected_text() const;
    [[nodiscard]] std::optional<std::size_t> selected_position() const;
    [[nodiscard]] bool hidden_at(std::size_t position) const;
    [[nodiscard]] std::vector<Target> stops(const Layout& placed) const;
    [[nodiscard]] Target default_stop(const Layout& placed) const;
    // The side of a page's image: the image list's height, at most 32,
    // or 16 without a list; scaled by the text scale.
    [[nodiscard]] double icon_side() const;
    [[nodiscard]] FontSpec label_font() const;
    [[nodiscard]] FontSpec header_font() const;

    void paint_tab(Painter& painter, const TabSlot& slot) const;
    void paint_flyout(Painter& painter, const Control& surface) const;
    void set_hot(const Target& target);
    void set_stop(const Target& target);
    void activate(const Target& target);
    void select_page(std::size_t position, PageSelectionCause cause);
    bool step_buttons(int direction);
    void move_along_tabs(const Layout& placed, int direction, bool to_end);
    void begin_grip_drag(Point local);
    void continue_grip_drag(Point local);
    void refresh_content();
    void move_host_to_overlay();
    void move_host_to_pane();
    void release_overlay(FocusScopeCloseReason reason) noexcept;
    void finish_overlay_close() noexcept;
    void notify_state();
    void on_selected_page_changed(const PageSelectionChange& change);
    void on_pages_changed(const PageSetChange& change);
    void on_window_pointer(const PointerEvent& event);
    void on_overlay_revoked();
    void on_menu_item_invoked(const MenuItemInvocation& invocation);
    void on_menu_open_changed(bool open);
    void on_image_list_changed(const ImageListChange& change);

    PageSet pages_{};
    bool collapsed_{};
    std::size_t button_count_{4U};
    // The hidden pages, by identity, in the order they were hidden; state()
    // and hidden_pages() report them in page order.
    std::vector<PageId> hidden_{};
    // Set while apply_state reorders the set, which then raises one
    // state_changed for the whole restoration.
    bool applying_state_{};
    bool focused_{};
    Target hot_{};
    Target pressed_{};
    // The keyboard's place while the pane has focus.
    Target stop_{};
    // The grip drag: where the press was and the large-button count then.
    bool grip_captured_{};
    double grip_origin_y_{};
    std::size_t grip_origin_count_{};
    // The content panel, a child of the pane or (while the overlay is open) of
    // the overlay; it holds the selected page's control.
    std::shared_ptr<ContentHost> host_{};
    // The overlay's root while it is open, its window lease and the window
    // whose root the pane watches for outside presses.
    std::shared_ptr<Flyout> flyout_{};
    OverlayToken overlay_token_{};
    FocusScopeId overlay_scope_{};
    bool overlay_open_{};
    bool overlay_closing_{};
    SubscriptionToken overlay_revoked_{};
    SubscriptionToken window_pointer_{};
    std::unique_ptr<ContextMenu> menu_{};
    SubscriptionToken menu_invoked_{};
    SubscriptionToken menu_changed_{};
    // The rows' commands: the menu requires one per command row. The page
    // rows' commands are rebuilt each time the menu opens.
    std::shared_ptr<Command> show_more_command_{};
    std::shared_ptr<Command> show_fewer_command_{};
    std::shared_ptr<Command> hidden_heading_command_{};
    std::shared_ptr<Command> options_command_{};
    std::vector<std::shared_ptr<Command>> page_commands_{};
    SubscriptionToken selection_token_{};
    SubscriptionToken pages_token_{};
    SubscriptionToken image_list_token_{};
    Event<const NavigationPaneOptionsRequest&> options_requested_{};
    Event<const NavigationPaneState&> state_changed_{};
    Event<bool> overlay_open_changed_{};
    // property_codemod begin NavigationPane (tools/property_families/navigation_pane.json; generated, do not edit)
private:
    friend struct NavigationPaneCollapsedCapability<NavigationPane>;
    friend struct NavigationPaneButtonCountCapability<NavigationPane>;
    friend struct NavigationPaneExpandedWidthCapability<NavigationPane>;
    friend struct NavigationPaneImageListCapability<NavigationPane>;
    friend struct NavigationPaneFocusRegionCapability<NavigationPane>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void write_view(const decltype(collapsed)& cell, bool incoming);
    [[nodiscard]] bool read_view(const decltype(collapsed)& cell) const;
    void write_view(const decltype(button_count)& cell, std::size_t incoming);
    [[nodiscard]] std::size_t read_view(const decltype(button_count)& cell) const;
    void prepare_write(const decltype(expanded_width)& cell, double& incoming);
    void written(const decltype(expanded_width)& cell);
    void prepare_write(const decltype(image_list)& cell, std::shared_ptr<ImageList>& incoming);
    void written(const decltype(image_list)& cell);
    void prepare_write(const decltype(focus_region)& cell, bool& incoming);
    void written(const decltype(focus_region)& cell);
    // property_codemod end NavigationPane
};

} // namespace gui_forms
