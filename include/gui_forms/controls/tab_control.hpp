#pragma once

#include "gui_forms/controls/container_control.hpp"
#include "gui_forms/controls/tab_page.hpp"
#include "gui_forms/controls/tab_control_values.hpp"
#include "gui_forms/pages/page_set/page_set.hpp"
#include "gui_forms/theme/owner_draw/owner_draw.hpp"
#include "gui_forms/controls/tab_control_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gui_forms {

class ImageList;
class PageStrip;
struct ImageListChange;
struct PageInvocation;

struct TabSelectionChange final {
    std::optional<std::size_t> old_index;
    std::optional<std::size_t> new_index;
};

// Which step of a change of page an event reports (Windows Forms'
// TabControlAction).
enum class TabControlAction : std::uint8_t {
    deselecting,
    selecting,
    deselected,
    selected,
};

// A page about to be left or chosen (Windows Forms'
// TabControlCancelEventArgs): `page` at `index`. A handler sets `cancel` to
// keep the selected page.
struct TabControlCancelEvent final {
    std::shared_ptr<TabPage> page{};
    std::size_t index{};
    TabControlAction action{TabControlAction::selecting};
    bool cancel{};
};

// A page left or chosen (Windows Forms' TabControlEventArgs).
struct TabControlEvent final {
    std::shared_ptr<TabPage> page{};
    std::size_t index{};
    TabControlAction action{TabControlAction::selected};
};

// A set of TabPages shown one at a time, chosen from a row of tabs (the
// Windows Forms surface). The tabs are a PageStrip (owner decision DK-14,
// phase P8): the control keeps one record per page in a PageSet the strip
// shows, so the strip's geometry, painting by role, overflow (scroll and
// list buttons) and keys serve the tabs. The strip is an internal child: it
// takes no focus (the control keeps it, as one tab stop) and publishes no
// semantics of its own (the control publishes its tabs as before).
class TabControl final : public ContainerControl, public TabControlCapabilities<TabControl> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    // The most pages a tab control holds: its strip's set's capacity.
    static constexpr std::size_t maximum_pages = PageSetLimits::maximum_capacity;

    explicit TabControl(StableId stable_id);
    ~TabControl() override;
    // Creates the tab strip. `make_control` calls it; do not call it yourself.
    void initialize_control_tree();

    void add_page(std::shared_ptr<TabPage> page);
    [[nodiscard]] std::shared_ptr<TabPage> remove_page(const TabPage& page);
    [[nodiscard]] std::vector<std::shared_ptr<TabPage>> pages() const;
    [[nodiscard]] std::size_t page_count() const;
    [[nodiscard]] std::shared_ptr<TabPage> page_at(std::size_t index) const;

    [[nodiscard]] const BasicControlStyle& style() const noexcept { return style_; }
    void set_style(BasicControlStyle style);
    [[nodiscard]] Rect tab_bounds(std::size_t index) const;
    [[nodiscard]] Rect display_bounds() const noexcept;
    // Whether the tabs do not all fit, so the strip shows its scroll and list
    // buttons.
    [[nodiscard]] bool overflowing() const;
    [[nodiscard]] Event<ItemContentPaint&>& draw_item_content() noexcept { return draw_item_content_; }
    [[nodiscard]] Event<const TabSelectionChange&>& selected_index_changed() noexcept {
        return selected_index_changed_;
    }
    // A change of page, by the user (a click, a key, the strip's list) or by
    // set_selected_index, raises deselecting for the selected page and then
    // selecting for the new one, before anything changes; either handler
    // may cancel it. Once the page has changed, deselected and selected
    // follow, then selected_index_changed. The first page added, and the
    // page that replaces a removed selected page, raise only
    // selected_index_changed. A handler that disposes the control, or
    // removes the new page, ends the change.
    [[nodiscard]] Event<TabControlCancelEvent&>& deselecting() noexcept { return deselecting_; }
    [[nodiscard]] Event<TabControlCancelEvent&>& selecting() noexcept { return selecting_; }
    [[nodiscard]] Event<const TabControlEvent&>& deselected() noexcept { return deselected_; }
    [[nodiscard]] Event<const TabControlEvent&>& selected() noexcept { return selected_; }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_key_preview(KeyEvent& event) override;
    void on_key(KeyEvent& event) override;
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
    // Stage T1 anatomy, as dot members.
    struct Parts final {
        ScrollableControl::Parts scrollable;
        PartGeometry face;
        PartGeometry display;
    };
    [[nodiscard]] Parts parts() const;
    // A tab's parts, in the control's coordinates: the strip's tab, label,
    // focus, seam and icon parts of the same names (the strip paints them).
    struct ItemParts final {
        PartGeometry tab;
        PartGeometry tab_label;
        PartGeometry tab_focus;
        PartGeometry tab_seam;
        PartGeometry tab_icon;
    };
    struct ItemStates final {
        bool selected{};
    };
    struct Item final {
        std::size_t index{};
        std::string stable_id;
        ItemParts parts;
        ItemStates states;
    };
    // The items currently realized, in paint order (owner decision 5).
    [[nodiscard]] std::vector<Item> realized_items() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item,
                                        std::span<PartGeometry> geometry) const override;
    void on_dispose() noexcept override;

private:
    using RememberedFocusMap =
        std::unordered_map<std::uint64_t, Control::WeakPtr>;
    [[nodiscard]] std::optional<std::size_t> index_of(
        const std::shared_ptr<TabPage>& page) const;
    void select_relative(int delta);
    // Raises deselecting and selecting for a change to `target`; false when
    // a handler cancelled it, disposed the control or removed the page.
    [[nodiscard]] bool selection_allowed(const std::shared_ptr<TabPage>& target);
    void show_selected(std::size_t index, const std::vector<std::shared_ptr<TabPage>>& live);
    void remember_page_focus(const std::shared_ptr<TabPage>& page);
    void restore_page_focus(const std::shared_ptr<TabPage>& page,
                            bool selection_owned_focus);
    void reconcile_pages();
    // -- the strip's set --
    void synchronize_tab_set();
    void select_in_tab_set(const std::shared_ptr<TabPage>& page);
    void configure_strip();
    [[nodiscard]] Rect header_bounds() const noexcept;
    [[nodiscard]] ItemParts tab_parts(const TabPage& page) const;
    // The key of the page's image in the image list: its image_key, or the
    // key at its image_index; empty when it names none the list has.
    [[nodiscard]] std::string tab_image_key(const TabPage& page) const;
    void on_image_list_changed(const ImageListChange& change);
    [[nodiscard]] bool nested_tab_control_has_focus() const;
    void on_set_selecting(PageSelectingEvent& event);
    void on_set_selection_changed(const PageSelectionChange& change);
    void on_strip_invoked(const PageInvocation& invocation);
    // Raises draw_item_content for the strip's owner-draw event.
    void on_strip_draw_item_content(ItemContentPaint& item);

    std::vector<std::weak_ptr<TabPage>> pages_;
    std::weak_ptr<TabPage> selected_page_;
    RememberedFocusMap remembered_focus_;
    BasicControlStyle style_;
    // The strip's pages: one record per live page in tab order, the page's
    // stable ID its PageId and its text its title; the set's selection is
    // the control's. The strip borrows the set and is released before the
    // set is destroyed.
    PageSet tab_set_;
    std::shared_ptr<PageStrip> strip_;
    // Set while the control edits `tab_set_` itself, so that its handlers
    // ignore the set's events about those edits.
    bool editing_tab_set_{};
    SubscriptionToken selecting_token_;
    SubscriptionToken selection_token_;
    SubscriptionToken invoked_token_;
    SubscriptionToken draw_item_content_token_;
    // The list's changes move the keys image_index names.
    SubscriptionToken image_list_token_;
    Event<const TabSelectionChange&> selected_index_changed_;
    Event<TabControlCancelEvent&> deselecting_;
    Event<TabControlCancelEvent&> selecting_;
    Event<const TabControlEvent&> deselected_;
    Event<const TabControlEvent&> selected_;
    Event<ItemContentPaint&> draw_item_content_;
    // property_codemod begin TabControl (tools/property_families/tab_control.json; generated, do not edit)
private:
    friend struct TabControlAlignmentCapability<TabControl>;
    friend struct TabControlAppearanceCapability<TabControl>;
    friend struct TabControlSizeModeCapability<TabControl>;
    friend struct TabControlItemSizeCapability<TabControl>;
    friend struct TabControlImageListCapability<TabControl>;
    friend struct TabControlDrawModeCapability<TabControl>;
    friend struct TabControlSelectedIndexCapability<TabControl>;
    friend struct TabControlSelectedTabCapability<TabControl>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(alignment)& cell, TabAlignment& incoming);
    void written(const decltype(alignment)& cell);
    void prepare_write(const decltype(appearance)& cell, TabAppearance& incoming);
    void written(const decltype(appearance)& cell);
    void prepare_write(const decltype(size_mode)& cell, TabSizeMode& incoming);
    void written(const decltype(size_mode)& cell);
    void prepare_write(const decltype(item_size)& cell, Size& incoming);
    void written(const decltype(item_size)& cell);
    void prepare_write(const decltype(image_list)& cell, std::shared_ptr<ImageList>& incoming);
    void written(const decltype(image_list)& cell);
    void write_view(const decltype(draw_mode)& cell, ItemDrawMode incoming);
    [[nodiscard]] ItemDrawMode read_view(const decltype(draw_mode)& cell) const;
    void write_view(const decltype(selected_index)& cell, std::optional<std::size_t> incoming);
    [[nodiscard]] std::optional<std::size_t> read_view(const decltype(selected_index)& cell) const;
    void write_view(const decltype(selected_tab)& cell, std::shared_ptr<TabPage> incoming);
    [[nodiscard]] std::shared_ptr<TabPage> read_view(const decltype(selected_tab)& cell) const;
    // property_codemod end TabControl
};

} // namespace gui_forms
