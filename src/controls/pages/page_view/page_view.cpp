#include "gui_forms/controls/pages/page_view/page_view.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// The gap between the content area's edge and a page's control, in logical
// units at text scale 1, so the content well's edge shows around it.
constexpr double content_inset = 1.0;

// Whether `control` is `ancestor` or inside it.
bool within(const Control::Ptr& control, const Control::Ptr& ancestor) {
    if (!control || !ancestor) return false;
    for (Control::Ptr current = control; current; current = (*current).parent()) {
        if (current == ancestor) return true;
    }
    return false;
}

// The panel a page's control sits in: published as the page's tab panel
// (owner decision DK-40), named by its tab, filled by the control. It paints
// nothing; the view paints the content well behind it. It is private to the
// view and publishes the base anatomy.
class PagePanel final : public Control {
public:
    explicit PagePanel(StableId stable_id) : Control(std::move(stable_id)) {}

    void set_title(std::string title) {
        if (title_ == title) return;
        title_ = std::move(title);
        invalidate(Dirty::semantics);
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        const Rect fill{0.0, 0.0, final_bounds.width, final_bounds.height};
        const std::span<const Control::Ptr> hosted = children();
        for (const Control::Ptr& child : hosted) set_child_layout(child, fill);
    }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor{};
        descriptor.role = SemanticRole::tab_panel;
        descriptor.name = title_;
        descriptor.exposed = true;
        return descriptor;
    }

private:
    std::string title_{};
};

// The view's strip: a PageStrip that names the panel each tab controls and
// applies the view's close policy to a close request nobody handled.
class PageViewStrip final : public PageStrip {
public:
    PageViewStrip(StableId stable_id, PageSet& pages) : PageStrip(std::move(stable_id), pages) {}

protected:
    [[nodiscard]] std::string page_panel_identity(const PageId& page) const override {
        const std::shared_ptr<PageView> view = std::dynamic_pointer_cast<PageView>(parent());
        if (!view) return {};
        const std::string identity = (*view).panel_identity(page);
        return identity;
    }

    void close_unhandled(PageCloseRequest& request) override {
        const std::shared_ptr<PageView> view = std::dynamic_pointer_cast<PageView>(parent());
        if (!view || (*view).close_policy() != PageViewClosePolicy::remove_when_unhandled) return;
        const PageEditStatus removed = (*view).pages().remove(request.page);
        request.handled = removed == PageEditStatus::applied;
    }
};

} // namespace

PageControlContent::PageControlContent(Control::Ptr control) : control_(std::move(control)) {
    if (!control_) throw std::invalid_argument("PageControlContent requires a control");
}

PageControlContent::~PageControlContent() = default;

PageSetOptions page_set_options(const PageViewOptions& options) noexcept {
    PageSetOptions result{};
    result.pinning_enabled = options.pinning_enabled;
    result.capacity = options.capacity;
    result.fallback = options.preset == PageViewPreset::document ? PageSelectionFallback::most_recent
                                                                 : PageSelectionFallback::neighbour;
    return result;
}

PageView::PageView(StableId stable_id, PageViewOptions options)
    : PageView(std::move(stable_id), page_set_options(options)) {
    if (options.preset != PageViewPreset::strip && options.preset != PageViewPreset::document) {
        throw std::invalid_argument("PageView preset is invalid");
    }
    preset_ = options.preset;
}

PageView::PageView(StableId stable_id, PageSetOptions options)
    : Control(std::move(stable_id)), pages_(options) {
    set_paint_plane(PaintPlane::control);
    // Registered first, before the strip's and the application's handlers,
    // so the view shows the new page before anyone else hears of it.
    pages_changed_token_ = pages_.pages_changed().subscribe(
        *this, Delegate<const PageSetChange&>::bind<PageView, &PageView::on_pages_changed>(*this));
    selection_token_ = pages_.selected_page_changed().subscribe(
        *this, Delegate<const PageSelectionChange&>::bind<PageView, &PageView::on_selection_changed>(*this));
}

void PageView::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    strip_ = make_control<PageViewStrip>(StableId(prefix + ".strip"), pages_);
    if (preset_ == PageViewPreset::document) {
        (*strip_).close_buttons = PageCloseButtons::selected_and_hot;
        (*strip_).overflow = PageStripOverflow::shrink_then_list;
        (*strip_).mnemonics_enabled = false;
        (*strip_).reorder_enabled = true;
    }
    add_child(strip_);
}

PageView::~PageView() {
    if (strip_) (*strip_).release_page_set();
}

void PageView::on_dispose() noexcept {
    if (strip_) (*strip_).release_page_set();
    pages_changed_token_.disconnect();
    selection_token_.disconnect();
    Control::on_dispose();
}

// -- pages ---------------------------------------------------------------------

PageEditStatus PageView::add_page(const PageRecord& record, Control::Ptr content, PagePlacement placement) {
    require_mutable();
    if (!content) {
        const PageEditStatus inserted = pages_.insert(record, placement);
        return inserted;
    }
    TakenPage page{record, std::make_unique<PageControlContent>(std::move(content))};
    const PageEditStatus put = pages_.put(page, placement);
    return put;
}

PageEditStatus PageView::set_page_content(const PageId& page, Control::Ptr content) {
    require_mutable();
    if (!content) return PageEditStatus::unchanged;
    std::unique_ptr<PageContent> record = std::make_unique<PageControlContent>(std::move(content));
    const PageEditStatus status = pages_.set_content(page, record);
    return status;
}

Control::Ptr PageView::page_content(const PageId& page) const {
    const PageControlContent* const content = dynamic_cast<const PageControlContent*>(pages_.content(page));
    if (content == nullptr) return {};
    return (*content).control();
}

std::string PageView::panel_identity(const PageId& page) const {
    const std::size_t index = slot_index(page);
    if (index >= slots_.size() || !slots_[index].panel) return {};
    const std::string identity((*slots_[index].panel).stable_id().value());
    return identity;
}

std::size_t PageView::slot_index(const PageId& page) const {
    for (std::size_t index = 0; index < slots_.size(); ++index) {
        if (slots_[index].page == page) return index;
    }
    return slots_.size();
}

// -- settings ------------------------------------------------------------------

void PageView::set_close_policy(PageViewClosePolicy policy) {
    require_mutable();
    if (policy != PageViewClosePolicy::remove_when_unhandled && policy != PageViewClosePolicy::request_only) {
        throw std::invalid_argument("PageView close policy is invalid");
    }
    close_policy_ = policy;
}

void PageView::set_ctrl_tab(PageViewCtrlTab policy) {
    require_mutable();
    if (policy != PageViewCtrlTab::select_next && policy != PageViewCtrlTab::pass) {
        throw std::invalid_argument("PageView Ctrl+Tab policy is invalid");
    }
    ctrl_tab_ = policy;
}

void PageView::on_attachment_committed() noexcept {
    Control::on_attachment_committed();
    Window* const host = attached_window();
    if (!focus_region_ || host == nullptr) return;
    // A view nested in another region, or already registered, stays out.
    try {
        static_cast<void>((*host).add_focus_region(shared_from_this()));
    } catch (...) {
    }
}

void PageView::set_focus_region(bool region) {
    require_mutable();
    if (focus_region_ == region) return;
    focus_region_ = region;
    Window* const host = attached_window();
    if (host == nullptr) return;
    const Control::Ptr self = shared_from_this();
    if (region) {
        static_cast<void>((*host).add_focus_region(self));
        return;
    }
    std::vector<Control::Ptr> remaining = (*host).focus_regions();
    remaining.erase(std::remove(remaining.begin(), remaining.end(), self), remaining.end());
    (*host).set_focus_regions(std::move(remaining));
}

// -- panels --------------------------------------------------------------------

void PageView::detach_panel(PageSlot& slot) {
    if (slot.panel) {
        if (slot.content && (*slot.content).parent() == slot.panel) {
            static_cast<void>((*slot.panel).remove_child((*slot.content).runtime_id()));
        }
        static_cast<void>(remove_child((*slot.panel).runtime_id()));
    }
    slot.panel.reset();
    slot.content.reset();
}

// Makes `content` the child of a new panel for the slot's page. A control
// still in another view's panel (a page put here before that view
// reconciled its take) leaves it first.
void PageView::attach_panel(PageSlot& slot, const Control::Ptr& content) {
    const Control::Ptr former = (*content).parent();
    if (former) static_cast<void>((*former).remove_child((*content).runtime_id()));
    const std::string identity = std::string(stable_id().value()) + ".panel." + slot.page.value;
    std::shared_ptr<PagePanel> panel = make_control<PagePanel>(StableId(identity));
    (*panel).visible = false;
    (*panel).add_child(content);
    add_child(panel);
    slot.panel = panel;
    slot.content = content;
}

// Brings the panels in line with the set: a slot per page, a panel for each
// page whose content is a control, its title from the page, and only the
// selected page's panel visible.
void PageView::reconcile_panels() {
    std::size_t index = 0U;
    while (index < slots_.size()) {
        PageSlot& slot = slots_[index];
        if (!pages_.find(slot.page)) {
            detach_panel(slot);
            slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(index));
            continue;
        }
        const Control::Ptr content = page_content(slot.page);
        if (slot.panel && content != slot.content) detach_panel(slot);
        ++index;
    }
    const std::optional<PageId>& selected = pages_.selected_page();
    const std::size_t count = pages_.count();
    for (std::size_t position = 0; position < count; ++position) {
        const PageRecord& record = pages_.record_at(PagePosition{position});
        std::size_t found = slot_index(record.id);
        if (found == slots_.size()) {
            PageSlot added{};
            added.page = record.id;
            slots_.push_back(std::move(added));
        }
        PageSlot& slot = slots_[found];
        const Control::Ptr content = page_content(slot.page);
        if (!slot.panel && content) attach_panel(slot, content);
        if (!slot.panel) continue;
        PagePanel& panel = static_cast<PagePanel&>(*slot.panel);
        panel.set_title(parse_mnemonic_text(record.title).display_text);
        const bool shown = selected && *selected == slot.page;
        if ((*slot.panel).visible.read() != shown) (*slot.panel).visible = shown;
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void PageView::on_pages_changed(const PageSetChange&) {
    reconcile_panels();
}

// Focus moves into the page: to the control it last had there, while that
// control can still take focus, else to the strip.
void PageView::restore_focus(const PageId& page) {
    Window* const host = window();
    if (host == nullptr) return;
    const std::size_t index = slot_index(page);
    if (index < slots_.size()) {
        const Control::Ptr remembered = slots_[index].remembered_focus.lock();
        if (remembered && (*remembered).eligible_for_input() && within(remembered, slots_[index].panel) &&
            (*host).request_focus(remembered)) {
            return;
        }
    }
    if (strip_) static_cast<void>((*host).request_focus(strip_));
}

void PageView::on_selection_changed(const PageSelectionChange& change) {
    Window* const host = window();
    const Control::Ptr focused = host != nullptr ? (*host).focused_control() : Control::Ptr{};
    bool focus_was_in_old_page = false;
    if (change.old_page) {
        const std::size_t old_index = slot_index(*change.old_page);
        if (old_index < slots_.size() && within(focused, slots_[old_index].panel)) {
            slots_[old_index].remembered_focus = focused;
            focus_was_in_old_page = true;
        }
    }
    if (change.new_page && !pages_.content(*change.new_page)) {
        std::size_t index = slot_index(*change.new_page);
        if (index == slots_.size()) {
            PageSlot added{};
            added.page = *change.new_page;
            slots_.push_back(std::move(added));
        }
        if (!slots_[index].content_requested) {
            slots_[index].content_requested = true;
            const PageContentRequest request{*change.new_page};
            const Control::Ptr keep = shared_from_this();
            content_requested_.emit(request);
            if (!is_alive()) return;
        }
    }
    reconcile_panels();
    // A click on a tab focuses the strip itself after selecting.
    if (focus_was_in_old_page && change.new_page && change.cause != PageSelectionCause::pointer) {
        restore_focus(*change.new_page);
    }
}

// -- layout and paint ----------------------------------------------------------

PageView::Areas PageView::areas_for(Size surface) const {
    Areas areas{};
    // A hidden strip (a docking tool stack holding one panel hides it)
    // leaves the whole surface to the content.
    if (!strip_ || !(*strip_).visible.read()) {
        areas.content = {0.0, 0.0, surface.width, surface.height};
        return areas;
    }
    const Size wanted = (*strip_).measure(surface);
    const PageStripPlacement placement = (*strip_).placement.read();
    if (placement == PageStripPlacement::left || placement == PageStripPlacement::right) {
        const double width = std::min(surface.width, wanted.width);
        const double remaining = std::max(0.0, surface.width - width);
        if (placement == PageStripPlacement::right) {
            areas.content = {0.0, 0.0, remaining, surface.height};
            areas.strip = {remaining, 0.0, width, surface.height};
        } else {
            areas.strip = {0.0, 0.0, width, surface.height};
            areas.content = {width, 0.0, remaining, surface.height};
        }
        return areas;
    }
    const double height = std::min(surface.height, wanted.height);
    const double rest = std::max(0.0, surface.height - height);
    if (placement == PageStripPlacement::bottom) {
        areas.content = {0.0, 0.0, surface.width, rest};
        areas.strip = {0.0, rest, surface.width, height};
    } else {
        areas.strip = {0.0, 0.0, surface.width, height};
        areas.content = {0.0, height, surface.width, rest};
    }
    return areas;
}

Rect PageView::panel_bounds(Rect content) const {
    const double inset = content_inset * effective_text_scale();
    const Rect bounds{content.x + inset, content.y + inset, std::max(0.0, content.width - inset * 2.0),
                      std::max(0.0, content.height - inset * 2.0)};
    return bounds;
}

Size PageView::measure(Size available) {
    return available;
}

void PageView::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Areas areas = areas_for(Size{final_bounds.width, final_bounds.height});
    if (strip_) set_child_layout(strip_, areas.strip);
    const Rect panel = panel_bounds(areas.content);
    for (const PageSlot& slot : slots_) {
        if (slot.panel) set_child_layout(slot.panel, panel);
    }
}

void PageView::on_paint(Painter& painter, Rect) {
    const Rect surface = client_rectangle();
    const Areas areas = areas_for(Size{surface.width, surface.height});
    PartPaint part{};
    part.family = ControlVisualRole::panel;
    part.role = PartRole::well;
    part.bounds = areas.content;
    paint_part(painter, *this, part);
}

// -- keys ----------------------------------------------------------------------

// The bubble phase reaches the innermost view around the focus first, so a
// nested view handles these keys before the view around it. The keys are the
// window's named commands (owner decision DK-30): close_document (Ctrl+F4,
// Cmd+W on macOS) closes the selected closable page, move_tab_later and
// move_tab_earlier (Ctrl+Shift+PageDown and PageUp) move it within its group,
// next_tab and previous_tab (Ctrl+PageDown and PageUp) select (owner decision
// DK-38), and cycle_forward and cycle_backward (Ctrl+Tab and Ctrl+Shift+Tab)
// select under the select_next policy. Delete is left to the focused
// control: it closes nothing.
void PageView::on_key_bubble(KeyEvent& event) {
    if (event.handled || event.action != KeyAction::down || !strip_ || pages_.count() == 0U) return;
    const KeyMap& keys = effective_key_map();
    if (keys.matches(KeyCommand::close_document, event)) {
        const std::optional<PageId> selected = pages_.selected_page();
        if (selected) static_cast<void>((*strip_).request_close(*selected, PageCloseCause::keyboard));
    } else if (keys.matches(KeyCommand::move_tab_later, event)) {
        static_cast<void>((*strip_).move_selected(1));
    } else if (keys.matches(KeyCommand::move_tab_earlier, event)) {
        static_cast<void>((*strip_).move_selected(-1));
    } else if (keys.matches(KeyCommand::next_tab, event)) {
        static_cast<void>((*strip_).select_relative(1, PageSelectionCause::keyboard));
    } else if (keys.matches(KeyCommand::previous_tab, event)) {
        static_cast<void>((*strip_).select_relative(-1, PageSelectionCause::keyboard));
    } else if (ctrl_tab_ == PageViewCtrlTab::select_next && keys.matches(KeyCommand::cycle_forward, event)) {
        static_cast<void>((*strip_).select_relative(1, PageSelectionCause::keyboard));
    } else if (ctrl_tab_ == PageViewCtrlTab::select_next && keys.matches(KeyCommand::cycle_backward, event)) {
        static_cast<void>((*strip_).select_relative(-1, PageSelectionCause::keyboard));
    } else {
        return;
    }
    event.handled = true;
}

// -- semantics -----------------------------------------------------------------

// The view adds no node of its own unless it is named: its strip is the tab
// list and the selected page's panel the tab panel.
SemanticDescriptor PageView::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.exposed = !accessible_name.read().empty();
    return descriptor;
}

// -- anatomy -------------------------------------------------------------------

namespace {

constexpr std::array<PartField<PageView::Parts>, 4> page_view_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &PageView::Parts::region},
    {.declaration = {.name = "strip", .role = PartRole::region, .composed_from = "page_strip"},
     .member = &PageView::Parts::strip},
    {.declaration = {.name = "content", .role = PartRole::well, .composed_from = {}},
     .member = &PageView::Parts::content},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &PageView::Parts::focus},
}};

constexpr std::array<PartDeclaration, 4> page_view_parts = part_declarations(page_view_part_fields);
constexpr std::array<AnatomyState, 5> page_view_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr ControlAnatomy page_view_anatomy{
    .control_id = "page_view",
    .parts = page_view_parts,
    .states = page_view_states,
    .custom_states = {},
    .item_systems = {},
};

// The events PageView adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 1> page_view_interactions{{
    {.name = "content_requested", .payload = "PageContentRequest", .mutable_payload = false},
}};

} // namespace

const ControlAnatomy& PageView::anatomy() const noexcept {
    return page_view_anatomy;
}

void PageView::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Rect surface = client_rectangle();
    const Areas areas = areas_for(Size{surface.width, surface.height});
    Parts record{};
    record.region = PartGeometry::from_bounds(surface);
    record.strip = PartGeometry::from_bounds(areas.strip);
    record.content = PartGeometry::from_bounds(areas.content);
    record.focus = PartGeometry::from_bounds(surface);
    write_parts(anatomy(), page_view_part_fields, record, geometry);
}

PageView::Parts PageView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record{};
    read_parts(anatomy(), page_view_part_fields, published.parts(), record);
    return record;
}

void PageView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), page_view_interactions.begin(), page_view_interactions.end());
}

} // namespace gui_forms
