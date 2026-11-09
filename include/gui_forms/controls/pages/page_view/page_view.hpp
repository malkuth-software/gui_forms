#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/controls/pages/page_strip/page_strip.hpp"
#include "gui_forms/controls/pages/page_view/page_view_values/page_view_values.hpp"
#include "gui_forms/event.hpp"
#include "gui_forms/pages.hpp"
#include "gui_forms/controls/pages/page_view/page_view_capabilities/page_view_capabilities.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace gui_forms {

// The content of one page of a PageView: the application's control, shown
// in the view's content area while its page is selected. The set owns this
// record and the record holds the control; the view makes the control a
// child of the page's panel while the page is in the view's set, so `take`
// and `put` move a page's control between views with its state, and
// removing the page releases it.
class PageControlContent final : public PageContent {
public:
    // Throws std::invalid_argument for a null control.
    explicit PageControlContent(Control::Ptr control);
    ~PageControlContent() override;

    [[nodiscard]] const Control::Ptr& control() const noexcept { return control_; }

private:
    Control::Ptr control_;
};

// The presentation a view starts from (owner decision DK-15). `strip`:
// content-sized tabs, scroll and list overflow, mnemonics, no close buttons,
// selection after a close by neighbour, no live reorder. `document` (DK-31,
// DK-38, DK-39): close buttons on the selected and hot tabs, shrink then
// list overflow, no mnemonics (titles are data), live reorder by dragging,
// selection after a close by most recent activation. The navigation preset is phase P8's.
enum class PageViewPreset : std::uint8_t { strip, document };

struct PageViewOptions final {
    PageViewPreset preset{PageViewPreset::strip};
    // Pinned tabs (owner decision DK-31) are opt-in.
    bool pinning_enabled{false};
    // As PageSetOptions::capacity.
    std::size_t capacity{PageSetLimits::default_capacity};
};

// The set options a view's options give: pinning and capacity as stated, and
// the preset's selection after a close (strip: neighbour; document: most
// recent, owner decision DK-37).
[[nodiscard]] PageSetOptions page_set_options(const PageViewOptions& options) noexcept;

// A selected page has no content yet (owner decision DK-16). A handler
// supplies it with `set_page_content` (or the set's `set_content`); a page
// left without content shows an empty content area and is not asked again.
struct PageContentRequest final {
    PageId page{};
};

// A PageStrip over its own PageSet, and a content area showing the selected
// page's control (owner decisions DK-13 to DK-16), in the strip or document
// preset (see PageViewPreset). The preset sets the strip's settings once,
// when the view is made; they may be changed on the strip afterwards.
//
// Closing (owner decision DK-38): the strip raises `page_close_requested`
// for the close button, a middle click, Ctrl+F4 (Cmd+W on macOS: the
// close_document key command) and the semantic close action; the command
// is handled anywhere inside the view, as Ctrl+PageDown is.
// When no handler sets `handled`, the close policy decides. Removing the
// selected page selects another by the set's fallback.
//
// The view owns its set and the strip borrows it; the view releases the
// strip when it is destroyed. Each page with content is shown in a panel the
// view owns (`<view id>.panel.<page id>`), published to accessibility as the
// page's tab panel, named by its tab and linked from it; only the selected
// page's panel is visible.
//
// Event order when the selection changes (the set's events, then the
// view's work inside the first `selected_page_changed` handler, which the
// view registers before anyone else can): the set's `selecting` (a handler
// may cancel; then nothing else happens); the view remembers the control
// that had focus in the old page; `content_requested` if the new page has
// no content and was never asked; the new page's panel is shown and the old
// one hidden; focus moves into the new page (its remembered control, else
// the strip) if it was inside the old page and the selection did not come
// from a click on a tab (the strip then takes focus itself); then the other
// `selected_page_changed` handlers; then the strip's `page_invoked` when a
// tab was chosen.
class PageView : public Control, public PageViewCapabilities<PageView> {
public:
    static constexpr bool initialize_tree_after_construction = true;
    // The strip preset over a set made with `options`.
    explicit PageView(StableId stable_id, PageSetOptions options = {});
    // The preset, over a set made with the options `page_set_options` gives.
    // Throws std::invalid_argument for an invalid preset.
    PageView(StableId stable_id, PageViewOptions options);
    ~PageView() override;
    // Creates the strip. `make_control` calls it; do not call it yourself.
    void initialize_control_tree();

    [[nodiscard]] PageSet& pages() noexcept { return pages_; }
    [[nodiscard]] const PageSet& pages() const noexcept { return pages_; }
    [[nodiscard]] const std::shared_ptr<PageStrip>& strip() const noexcept { return strip_; }

    // Adds a page with `content` (or none, for late content) at `placement`,
    // as PageSet::put. The set's refusals are returned; on a refusal the
    // view is unchanged.
    [[nodiscard]] PageEditStatus add_page(const PageRecord& record, Control::Ptr content,
                                          PagePlacement placement = PagePlacement::end_of_group());
    // Attaches late content, as PageSet::set_content. Refuses a null control
    // with `unchanged`, as the set does for null content.
    [[nodiscard]] PageEditStatus set_page_content(const PageId& page, Control::Ptr content);
    // The page's control, or null when it has none or is not in the set.
    [[nodiscard]] Control::Ptr page_content(const PageId& page) const;
    // The stable identity of the page's tab panel, or empty when the page has
    // no content shown by this view.
    [[nodiscard]] std::string panel_identity(const PageId& page) const;

    [[nodiscard]] PageViewPreset preset() const noexcept { return preset_; }

    [[nodiscard]] Event<const PageContentRequest&>& content_requested() noexcept {
        return content_requested_;
    }

    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_key_bubble(KeyEvent& event) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;

    // Stage T1 anatomy, as dot members; its states are Control's.
    struct Parts final {
        PartGeometry region;
        // The child strip's bounds.
        PartGeometry strip;
        // The selected page's content area.
        PartGeometry content;
        PartGeometry focus;
    };
    [[nodiscard]] Parts parts() const;

protected:
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    void on_attachment_committed() noexcept override;
    void on_dispose() noexcept override;
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;

private:
    // One page of the set, as the view shows it. `panel` exists while the
    // page has a PageControlContent; `content` is that control, a child of
    // the panel. `remembered_focus` observes the control that last had focus
    // in the page when another page was selected.
    struct PageSlot final {
        PageId page{};
        Control::Ptr panel{};
        Control::Ptr content{};
        Control::WeakPtr remembered_focus{};
        bool content_requested{};
    };
    struct Areas final {
        Rect strip{};
        Rect content{};
    };

    [[nodiscard]] Areas areas_for(Size surface) const;
    [[nodiscard]] Rect panel_bounds(Rect content) const;
    [[nodiscard]] std::size_t slot_index(const PageId& page) const;
    void reconcile_panels();
    void detach_panel(PageSlot& slot);
    void attach_panel(PageSlot& slot, const Control::Ptr& content);
    void restore_focus(const PageId& page);
    void on_pages_changed(const PageSetChange& change);
    void on_selection_changed(const PageSelectionChange& change);

    PageSet pages_;
    PageViewPreset preset_{PageViewPreset::strip};
    std::shared_ptr<PageStrip> strip_{};
    // One per page of the set (reconciled on every change), in no order.
    std::vector<PageSlot> slots_{};
    SubscriptionToken pages_changed_token_{};
    SubscriptionToken selection_token_{};
    Event<const PageContentRequest&> content_requested_{};
    // property_codemod begin PageView (tools/property_families/page_view.json; generated, do not edit)
private:
    friend struct PageViewClosePolicyPropertyCapability<PageView>;
    friend struct PageViewCtrlTabPropertyCapability<PageView>;
    friend struct PageViewFocusRegionCapability<PageView>;
    // The capabilities' hooks, chosen by the cell's type (study section 14).
    void prepare_write(const decltype(close_policy)& cell, PageViewClosePolicy& incoming);
    void written(const decltype(close_policy)& cell);
    void prepare_write(const decltype(ctrl_tab)& cell, PageViewCtrlTab& incoming);
    void written(const decltype(ctrl_tab)& cell);
    void prepare_write(const decltype(focus_region)& cell, bool& incoming);
    void written(const decltype(focus_region)& cell);
    // property_codemod end PageView
};

} // namespace gui_forms
