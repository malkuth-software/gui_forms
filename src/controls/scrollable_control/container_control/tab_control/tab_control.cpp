#include "gui_forms/controls/tab_control.hpp"
#include "gui_forms/controls/page_strip.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/image_list.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"
#include "../../scrollable_anatomy.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace gui_forms {

namespace {

// Whether `control` is `ancestor` or inside it.
[[nodiscard]] bool within(const Control::Ptr& control, const Control::Ptr& ancestor) {
    if (!control || !ancestor) return false;
    for (Control::Ptr current = control; current; current = (*current).parent()) {
        if (current == ancestor) return true;
    }
    return false;
}

// A page's record in the strip's set is named by the page's stable ID.
[[nodiscard]] PageId tab_page_id(const TabPage& page) {
    const PageId id{std::string(page.stable_id().value())};
    return id;
}

// The tab strip of a TabControl: a PageStrip that takes no focus (the
// control holds it, and passes the strip its focus and keys), publishes no
// semantics (the control publishes the tabs), and draws its tabs in the
// family the control's appearance names.
class TabControlStrip final : public PageStrip {
public:
    TabControlStrip(StableId stable_id, PageSet& pages) : PageStrip(std::move(stable_id), pages) {
        focusable = false;
    }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor{};
        descriptor.exposed = false;
        descriptor.include_descendants = false;
        return descriptor;
    }

    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override {
        const std::vector<SemanticNode> none;
        return none;
    }

protected:
    [[nodiscard]] ControlVisualRole tab_family() const noexcept override {
        const std::shared_ptr<TabControl> owner = std::dynamic_pointer_cast<TabControl>(parent());
        if (!owner) return ControlVisualRole::menu_item;
        if ((*owner).appearance.read() == TabAppearance::buttons) return ControlVisualRole::button;
        if ((*owner).appearance.read() == TabAppearance::flat_buttons) return ControlVisualRole::command_button;
        return ControlVisualRole::menu_item;
    }
};

// Marks the control's own edits of its strip's set for their duration, on
// every exit.
class TabSetEditScope final {
public:
    explicit TabSetEditScope(bool& editing) noexcept : editing_(editing), outer_(editing) { editing_ = true; }
    ~TabSetEditScope() { editing_ = outer_; }
    TabSetEditScope(const TabSetEditScope&) = delete;
    TabSetEditScope& operator=(const TabSetEditScope&) = delete;

private:
    bool& editing_;
    bool outer_{};
};

[[nodiscard]] PageSetOptions tab_set_options() noexcept {
    PageSetOptions options{};
    options.capacity = TabControl::maximum_pages;
    options.fallback = PageSelectionFallback::neighbour;
    return options;
}

} // namespace

TabControl::TabControl(StableId stable_id)
    : ContainerControl(std::move(stable_id)), tab_set_(tab_set_options()) {
    focusable = true;
    // The tab strip and the page frame are drawn to the control's size
    // (ScrollableControl clears resize_redraw).
    Control::set_style(ControlStyles::resize_redraw, true);
    // A page's tooltip shows over its tab header (owner, 2026-10-28).
    set_publishes_item_tool_tips(true);
    // Registered before the strip's handlers: the control shows the page
    // before the strip relays out.
    selecting_token_ = tab_set_.selecting().subscribe(
        *this, Delegate<PageSelectingEvent&>::bind<TabControl, &TabControl::on_set_selecting>(*this));
    selection_token_ = tab_set_.selected_page_changed().subscribe(
        *this,
        Delegate<const PageSelectionChange&>::bind<TabControl, &TabControl::on_set_selection_changed>(*this));
}

void TabControl::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    strip_ = make_control<TabControlStrip>(StableId(prefix + ".strip"), tab_set_);
    // Captions are shown as written: `&` has no mnemonic meaning here.
    (*strip_).mnemonics_enabled = false;
    configure_strip();
    invoked_token_ = (*strip_).page_invoked().subscribe(
        *this, Delegate<const PageInvocation&>::bind<TabControl, &TabControl::on_strip_invoked>(*this));
    draw_item_content_token_ = (*strip_).draw_item_content().subscribe(
        *this, Delegate<ItemContentPaint&>::bind<TabControl, &TabControl::on_strip_draw_item_content>(*this));
    add_child(strip_);
}

// draw_mode is the strip's; before the strip exists it reads as normal.
void TabControl::write_view(const decltype(draw_mode)&, ItemDrawMode incoming) {
    require_mutable();
    if (!strip_) throw std::logic_error("TabControl has no tab strip yet");
    (*strip_).draw_mode = incoming;
}

ItemDrawMode TabControl::read_view(const decltype(draw_mode)&) const {
    const ItemDrawMode mode = strip_ ? (*strip_).draw_mode.read() : ItemDrawMode::normal;
    return mode;
}

void TabControl::on_strip_draw_item_content(ItemContentPaint& item) {
    draw_item_content_.emit(item);
}

TabControl::~TabControl() {
    if (strip_) (*strip_).release_page_set();
}

void TabControl::on_dispose() noexcept {
    if (strip_) (*strip_).release_page_set();
    selecting_token_.disconnect();
    selection_token_.disconnect();
    invoked_token_.disconnect();
    image_list_token_.disconnect();
    ContainerControl::on_dispose();
}

std::vector<std::shared_ptr<TabPage>> TabControl::pages() const {
    std::vector<std::shared_ptr<TabPage>> result{};
    result.reserve(pages_.size());
    for (const std::weak_ptr<TabPage>& weak : pages_) {
        const std::shared_ptr<TabPage> page = weak.lock();
        if (page && (*page).is_alive() && (*page).parent().get() == this) {
            result.push_back(page);
        }
    }
    return result;
}

std::size_t TabControl::page_count() const {
    return pages().size();
}

std::shared_ptr<TabPage> TabControl::page_at(std::size_t index) const {
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    if (index >= live.size()) throw std::out_of_range("TabControl page index");
    return live[index];
}

std::optional<std::size_t> TabControl::index_of(
    const std::shared_ptr<TabPage>& page) const {
    if (!page) return std::nullopt;
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    const std::vector<std::shared_ptr<TabPage>>::const_iterator found =
        std::find(live.begin(), live.end(), page);
    if (found == live.end()) return std::nullopt;
    return static_cast<std::size_t>(found - live.begin());
}

std::optional<std::size_t> TabControl::read_view(const decltype(selected_index)&) const {
    return index_of(selected_page_.lock());
}

// The page's record joins the strip's set before the page is added, so a
// refusal (a stable ID another page has, the page limit) leaves the control
// unchanged.
void TabControl::add_page(std::shared_ptr<TabPage> page) {
    require_mutable();
    if (!page) throw std::invalid_argument("TabControl page may not be null");
    if (index_of(page)) return;
    synchronize_tab_set();
    PageRecord record{};
    record.id = tab_page_id(*page);
    record.title = (*page).text.read();
    record.image_key = tab_image_key(*page);
    PageEditStatus inserted = PageEditStatus::applied;
    {
        const TabSetEditScope editing(editing_tab_set_);
        inserted = tab_set_.insert(record, PagePlacement::end_of_group());
    }
    if (inserted == PageEditStatus::duplicate_page) {
        throw std::invalid_argument("TabControl pages need distinct stable IDs");
    }
    if (inserted == PageEditStatus::capacity_reached) {
        throw std::length_error("TabControl holds at most maximum_pages pages");
    }
    if (inserted != PageEditStatus::applied) {
        throw std::logic_error("TabControl could not add the page's tab");
    }
    (*page).visible = false;
    try {
        add_child(page);
    } catch (...) {
        const TabSetEditScope editing(editing_tab_set_);
        static_cast<void>(tab_set_.remove(record.id));
        throw;
    }
    pages_.push_back(page);
    // The strip paints over the pages' top edge, where the selected tab
    // joins its page.
    if (strip_) (*strip_).bring_to_front();
    if (!selected_page_.lock()) {
        selected_page_ = page;
        (*page).visible = true;
        select_in_tab_set(page);
        const TabSelectionChange change{std::nullopt, 0U};
        publish_change(selected_index_changed_, change);
    } else {
        select_in_tab_set(selected_page_.lock());
    }
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

std::shared_ptr<TabPage> TabControl::remove_page(const TabPage& page) {
    require_mutable();
    const std::vector<std::shared_ptr<TabPage>> live_before = pages();
    std::size_t removed_index = 0U;
    while (removed_index < live_before.size() &&
           live_before[removed_index].get() != &page) {
        ++removed_index;
    }
    if (removed_index == live_before.size()) return {};
    const std::optional<std::size_t> old_selected = selected_index.read();
    const bool removing_selected = selected_page_.lock().get() == &page;
    std::vector<std::weak_ptr<TabPage>>::iterator candidate = pages_.begin();
    while (candidate != pages_.end()) {
        const std::shared_ptr<TabPage> retained = (*candidate).lock();
        if (!retained || retained.get() == &page) {
            candidate = pages_.erase(candidate);
        } else {
            ++candidate;
        }
    }
    const Control::Ptr removed = remove_child(page.runtime_id());
    if (!removed) return {};

    const std::vector<std::shared_ptr<TabPage>> live_after = pages();
    if (removing_selected) {
        selected_page_.reset();
        if (!live_after.empty()) {
            const std::size_t next = std::min(removed_index, live_after.size() - 1U);
            selected_page_ = live_after[next];
            (*live_after[next]).visible = true;
        }
    }
    synchronize_tab_set();
    const std::optional<std::size_t> new_selected = selected_index.read();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
    if (old_selected != new_selected || removing_selected) {
        const TabSelectionChange change{old_selected, new_selected};
        publish_change(selected_index_changed_, change);
    }
    return std::dynamic_pointer_cast<TabPage>(removed);
}

void TabControl::remember_page_focus(const std::shared_ptr<TabPage>& page) {
    if (!page || window() == nullptr) return;
    const Control::Ptr focused = (*window()).focused_control();
    if (within(focused, page)) remembered_focus_[(*page).runtime_id().value] = focused;
}

void TabControl::restore_page_focus(const std::shared_ptr<TabPage>& page,
                                    bool selection_owned_focus) {
    if (!selection_owned_focus || !page || window() == nullptr) return;
    const RememberedFocusMap::iterator found =
        remembered_focus_.find((*page).runtime_id().value);
    if (found != remembered_focus_.end()) {
        const Control::Ptr candidate = (*found).second.lock();
        if (candidate && (*candidate).eligible_for_input() && within(candidate, page) &&
            (*window()).request_focus(candidate)) {
            return;
        }
    }
    static_cast<void>((*window()).request_focus(shared_from_this()));
}

// Hides the old page and shows the new one, remembering the focus the old
// page held, makes the strip's set agree, moves the focus into the new page
// when the old page held it, then raises selected_index_changed.
void TabControl::show_selected(std::size_t index, const std::vector<std::shared_ptr<TabPage>>& live) {
    const std::optional<std::size_t> old_index = selected_index.read();
    const std::shared_ptr<TabPage> old_page = selected_page_.lock();
    bool selection_owned_focus{};
    if (old_page && window() != nullptr) {
        const Control::Ptr focused = (*window()).focused_control();
        selection_owned_focus = within(focused, old_page);
        remember_page_focus(old_page);
        (*old_page).visible = false;
    }
    selected_page_ = live[index];
    (*live[index]).visible = true;
    select_in_tab_set(live[index]);
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test |
               Dirty::semantics);
    restore_page_focus(live[index], selection_owned_focus);
    if (old_page && old_index) {
        const TabControlEvent left{old_page, *old_index, TabControlAction::deselected};
        publish_change(deselected_, left);
    }
    const TabControlEvent chosen{live[index], index, TabControlAction::selected};
    publish_change(selected_, chosen);
    const TabSelectionChange change{old_index, index};
    publish_change(selected_index_changed_, change);
}

bool TabControl::selection_allowed(const std::shared_ptr<TabPage>& target) {
    const Control::Ptr lifetime = shared_from_this();
    const std::shared_ptr<TabPage> current = selected_page_.lock();
    const std::optional<std::size_t> current_index = index_of(current);
    if (current && current_index) {
        TabControlCancelEvent leaving{current, *current_index, TabControlAction::deselecting, false};
        deselecting_.emit(leaving);
        if (!is_alive() || leaving.cancel) return false;
    }
    const std::optional<std::size_t> target_index = index_of(target);
    if (!target_index) return false;
    TabControlCancelEvent choosing{target, *target_index, TabControlAction::selecting, false};
    selecting_.emit(choosing);
    if (!is_alive() || choosing.cancel) return false;
    const bool result = index_of(target).has_value();
    return result;
}

void TabControl::write_view(const decltype(selected_index)&, std::optional<std::size_t> incoming) {
    require_mutable();
    if (!incoming) throw std::invalid_argument("TabControl selection cannot be cleared");
    const std::size_t index = *incoming;
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    if (index >= live.size()) throw std::out_of_range("TabControl selected index");
    const std::optional<std::size_t> old_index = index_of(selected_page_.lock());
    if (old_index && *old_index == index) return;
    const std::shared_ptr<TabPage> target = live[index];
    if (!selection_allowed(target)) return;
    // The handlers may have moved the page or selected it themselves.
    const std::vector<std::shared_ptr<TabPage>> now = pages();
    const std::optional<std::size_t> placed = index_of(target);
    if (!placed || selected_page_.lock() == target) return;
    show_selected(*placed, now);
}

std::shared_ptr<TabPage> TabControl::read_view(const decltype(selected_tab)&) const {
    std::shared_ptr<TabPage> page = selected_page_.lock();
    return page;
}

void TabControl::write_view(const decltype(selected_tab)&, std::shared_ptr<TabPage> incoming) {
    const std::optional<std::size_t> index = index_of(incoming);
    if (!index) throw std::invalid_argument("TabPage does not belong to TabControl");
    selected_index = index;
}

void TabControl::set_style(BasicControlStyle style) {
    require_mutable();
    if (style_ == style) return;
    style_ = std::move(style);
    invalidate(Dirty::paint | Dirty::semantics);
}

void TabControl::prepare_write(const decltype(alignment)&, TabAlignment& incoming) {
    require_mutable();
    if (incoming != TabAlignment::top && incoming != TabAlignment::bottom && incoming != TabAlignment::left &&
        incoming != TabAlignment::right) {
        throw std::invalid_argument("TabControl alignment is invalid");
    }
}

void TabControl::written(const decltype(alignment)&) {
    configure_strip();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

void TabControl::prepare_write(const decltype(appearance)&, TabAppearance& incoming) {
    require_mutable();
    if (incoming != TabAppearance::normal && incoming != TabAppearance::buttons &&
        incoming != TabAppearance::flat_buttons) {
        throw std::invalid_argument("TabControl appearance is invalid");
    }
}

void TabControl::written(const decltype(appearance)&) {
    if (strip_) (*strip_).invalidate(Dirty::paint);
    invalidate(Dirty::paint | Dirty::semantics);
}

void TabControl::prepare_write(const decltype(size_mode)&, TabSizeMode& incoming) {
    require_mutable();
    if (incoming != TabSizeMode::normal && incoming != TabSizeMode::fill_to_right &&
        incoming != TabSizeMode::fixed) {
        throw std::invalid_argument("TabControl size mode is invalid");
    }
}

void TabControl::written(const decltype(size_mode)&) {
    configure_strip();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void TabControl::prepare_write(const decltype(item_size)&, Size& incoming) {
    require_mutable();
    if (!std::isfinite(incoming.width) || !std::isfinite(incoming.height) ||
        incoming.width < 24.0 || incoming.height < 18.0 ||
        incoming.width > PageItemWidths::maximum_width || incoming.height > PageItemWidths::maximum_width) {
        throw std::invalid_argument(
            "TabControl item size must be finite, at least 24 by 18 and at most 1024 by 1024");
    }
}

void TabControl::written(const decltype(item_size)&) {
    configure_strip();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint |
               Dirty::hit_test | Dirty::semantics);
}

// The strip checks the list first, so a refused list changes neither; an
// unchanged list is an unchanged write on the strip as well.
void TabControl::prepare_write(const decltype(image_list)&, std::shared_ptr<ImageList>& incoming) {
    require_mutable();
    if (strip_) (*strip_).image_list = incoming;
}

void TabControl::written(const decltype(image_list)&) {
    image_list_token_.disconnect();
    if (image_list.read()) {
        image_list_token_ = (*image_list.read()).changed().subscribe(
            *this, Delegate<const ImageListChange&>::bind<TabControl, &TabControl::on_image_list_changed>(*this));
    }
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test);
}

void TabControl::on_image_list_changed(const ImageListChange&) {
    // Arranging again maps each image_index to the key now at that index.
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test);
}

std::string TabControl::tab_image_key(const TabPage& page) const {
    if (!page.image_key.read().empty()) return page.image_key.read();
    const int index = page.image_index.read();
    if (index < 0 || !image_list.read() || !(*image_list.read()).is_alive()) return {};
    const std::size_t position = static_cast<std::size_t>(index);
    if (position >= (*image_list.read()).count()) return {};
    std::string key((*image_list.read()).key_at(position));
    return key;
}

// -- the strip -----------------------------------------------------------------

// The strip's edge, sizing and fixed width follow the control's alignment,
// size mode and item size. A fixed tab is `item_size` as the control lays it
// out: its width along a top or bottom row, its height down a side column.
void TabControl::configure_strip() {
    if (!strip_) return;
    PageStripPlacement placement = PageStripPlacement::top;
    if (alignment.read() == TabAlignment::bottom) placement = PageStripPlacement::bottom;
    if (alignment.read() == TabAlignment::left) placement = PageStripPlacement::left;
    if (alignment.read() == TabAlignment::right) placement = PageStripPlacement::right;
    PageItemSizing sizing = PageItemSizing::content;
    if (size_mode.read() == TabSizeMode::fill_to_right) sizing = PageItemSizing::fill;
    if (size_mode.read() == TabSizeMode::fixed) sizing = PageItemSizing::fixed;
    const bool row = alignment.read() == TabAlignment::top || alignment.read() == TabAlignment::bottom;
    PageItemWidths widths = (*strip_).item_widths.read();
    widths.fixed = row ? item_size.read().width : item_size.read().height;
    (*strip_).placement = placement;
    (*strip_).item_sizing = sizing;
    (*strip_).item_widths = widths;
}

// The band the strip occupies: `item_size`'s height across a top or bottom
// row, its width across a side column, at the window's text scale.
Rect TabControl::header_bounds() const noexcept {
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const double scale = effective_text_scale();
    const double row = std::min(item_size.read().height * scale, bounds.height);
    const double column = std::min(item_size.read().width * scale, bounds.width);
    switch (alignment.read()) {
    case TabAlignment::top:
        return {0.0, 0.0, bounds.width, row};
    case TabAlignment::bottom:
        return {0.0, bounds.height - row, bounds.width, row};
    case TabAlignment::left:
        return {0.0, 0.0, column, bounds.height};
    case TabAlignment::right:
        return {bounds.width - column, 0.0, column, bounds.height};
    }
    return bounds;
}

// Brings the strip's set in line with the live pages: the records of pages
// that left (a page disposed directly, or removed with remove_child) go,
// titles follow the pages' text, and the set selects the control's page.
//
// The set's order is the pages' order (pages are only appended and removed),
// so the records of departed pages are the ones that do not match the live
// page at the same position; the common case, nothing departed, is one pass.
void TabControl::synchronize_tab_set() {
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    const TabSetEditScope editing(editing_tab_set_);
    std::size_t position = 0U;
    while (position < tab_set_.count()) {
        const PageId id = tab_set_.record_at(PagePosition{position}).id;
        const bool matches = position < live.size() && (*live[position]).stable_id().value() == id.value;
        if (matches) {
            ++position;
            continue;
        }
        const PageEditStatus removed = tab_set_.remove(id);
        // A refusal (the set busy selecting) leaves the record for the next
        // layout; stepping past it keeps the loop finite.
        if (removed != PageEditStatus::applied) ++position;
    }
    for (std::size_t index = 0; index < live.size() && index < tab_set_.count(); ++index) {
        const PageRecord& record = tab_set_.record_at(PagePosition{index});
        const PageId id = record.id;
        if (record.title != (*live[index]).text.read()) {
            static_cast<void>(tab_set_.set_title(id, (*live[index]).text.read()));
        }
        std::string image_key = tab_image_key(*live[index]);
        if (tab_set_.record_at(PagePosition{index}).image_key != image_key) {
            static_cast<void>(tab_set_.set_image_key(id, std::move(image_key)));
        }
    }
    select_in_tab_set(selected_page_.lock());
}

void TabControl::select_in_tab_set(const std::shared_ptr<TabPage>& page) {
    if (!page) return;
    const PageId id = tab_page_id(*page);
    const std::optional<PageId> current = tab_set_.selected_page();
    if (current && *current == id) return;
    const TabSetEditScope editing(editing_tab_set_);
    static_cast<void>(tab_set_.select(id, PageSelectionCause::programmatic));
}

// A press on a tab focuses the control before the page changes, so the old
// page's focus is not remembered (the order Windows Forms users know).
void TabControl::on_set_selecting(PageSelectingEvent& event) {
    if (editing_tab_set_ || !is_alive()) return;
    if (event.cause == PageSelectionCause::pointer && window() != nullptr) {
        static_cast<void>((*window()).request_focus(shared_from_this()));
    }
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    for (const std::shared_ptr<TabPage>& page : live) {
        if (tab_page_id(*page) != event.new_page) continue;
        if (page == selected_page_.lock()) return;
        if (!selection_allowed(page)) event.cancel = true;
        return;
    }
}

// The strip selected a page (a click, a key, the page list): show it. A
// change that agrees with the control's selection (its own edits, published
// late) does nothing.
void TabControl::on_set_selection_changed(const PageSelectionChange& change) {
    if (editing_tab_set_ || !change.new_page || !is_alive()) return;
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    for (std::size_t index = 0; index < live.size(); ++index) {
        if (tab_page_id(*live[index]) != *change.new_page) continue;
        if (live[index] == selected_page_.lock()) return;
        show_selected(index, live);
        return;
    }
}

// A click on the selected tab (or a page chosen from the list) focuses the
// control too.
void TabControl::on_strip_invoked(const PageInvocation& invocation) {
    if (invocation.cause != PageSelectionCause::pointer || window() == nullptr || !is_alive()) return;
    static_cast<void>((*window()).request_focus(shared_from_this()));
}

// -- geometry ------------------------------------------------------------------

TabControl::ItemParts TabControl::tab_parts(const TabPage& page) const {
    ItemParts record{};
    if (!strip_) return record;
    const PageStrip::ItemParts strip_parts = (*strip_).tab_parts_of(tab_page_id(page));
    // The strip's parts are in its coordinates; the control's lie one
    // offset away.
    const Rect strip_bounds = (*strip_).absolute_bounds();
    const Rect own = absolute_bounds();
    const double dx = strip_bounds.x - own.x;
    const double dy = strip_bounds.y - own.y;
    const std::array<const PartGeometry*, 5> sources{&strip_parts.tab, &strip_parts.tab_label,
                                                     &strip_parts.tab_focus, &strip_parts.tab_seam,
                                                     &strip_parts.tab_icon};
    const std::array<PartGeometry*, 5> targets{&record.tab, &record.tab_label, &record.tab_focus,
                                               &record.tab_seam, &record.tab_icon};
    for (std::size_t part = 0; part < sources.size(); ++part) {
        if (!(*sources[part]).present) continue;
        const Rect bounds = (*sources[part]).bounds;
        const Rect moved{bounds.x + dx, bounds.y + dy, bounds.width, bounds.height};
        *targets[part] = PartGeometry::from_bounds(moved);
    }
    return record;
}

// Empty for a tab the strip has not laid out (scrolled out of view).
Rect TabControl::tab_bounds(std::size_t index) const {
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    if (index >= live.size()) throw std::out_of_range("TabControl tab index");
    const ItemParts parts = tab_parts(*live[index]);
    return parts.tab.bounds;
}

bool TabControl::overflowing() const {
    if (!strip_) return false;
    const bool result = (*strip_).overflowing();
    return result;
}

Rect TabControl::display_bounds() const noexcept {
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    const Size item{item_size.read().width * effective_text_scale(),
                    item_size.read().height * effective_text_scale()};
    switch (alignment.read()) {
    case TabAlignment::top:
        return {0.0, std::min(bounds.height, item.height - 1.0),
                bounds.width,
                std::max(0.0, bounds.height - item.height + 1.0)};
    case TabAlignment::bottom:
        return {0.0, 0.0, bounds.width,
                std::max(0.0, bounds.height - item.height + 1.0)};
    case TabAlignment::left:
        return {std::min(bounds.width, item.width - 1.0), 0.0,
                std::max(0.0, bounds.width - item.width + 1.0),
                bounds.height};
    case TabAlignment::right:
        return {0.0, 0.0,
                std::max(0.0, bounds.width - item.width + 1.0),
                bounds.height};
    }
    return bounds;
}

// The requested size where one is set, else all there is.
Size TabControl::measure(Size available) {
    const Rect requested = requested_bounds();
    const double width = requested.width > 0.0 ? requested.width : available.width;
    const double height = requested.height > 0.0 ? requested.height : available.height;
    const Size wanted{std::min(available.width, width), std::min(available.height, height)};
    return wanted;
}

void TabControl::reconcile_pages() {
    std::vector<std::weak_ptr<TabPage>>::iterator candidate = pages_.begin();
    while (candidate != pages_.end()) {
        const std::shared_ptr<TabPage> page = (*candidate).lock();
        if (!page || !(*page).is_alive() || (*page).parent().get() != this) {
            candidate = pages_.erase(candidate);
        } else {
            ++candidate;
        }
    }
    const std::shared_ptr<TabPage> selected = selected_page_.lock();
    if (selected && (*selected).is_alive() && (*selected).parent().get() == this) return;
    selected_page_.reset();
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    if (!live.empty()) {
        selected_page_ = live.front();
        (*live.front()).visible = true;
    }
}

void TabControl::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    reconcile_pages();
    synchronize_tab_set();
    if (strip_) set_child_layout(strip_, header_bounds());
    const Rect display = display_bounds();
    const std::shared_ptr<TabPage> selected = selected_page_.lock();
    for (const std::shared_ptr<TabPage>& page : pages()) {
        set_child_layout(page, display);
        (*page).visible = page == selected;
    }
}

// The control paints its face, the band behind the tabs, and the display
// the pages sit on; the strip, a child, paints the tabs. The display is a
// card's surface until stage T4 gives the tab page its own material. The
// face covers the header band only (sweep defect D13: a face over the whole
// control drew its square frame around the display's rounded corners,
// leaving notches there).
void TabControl::on_paint(Painter& painter, Rect) {
    PartPaint part{};
    part.family = ControlVisualRole::panel;
    part.role = PartRole::face;
    part.bounds = header_bounds();
    paint_part(painter, *this, part);
    part.family = ControlVisualRole::card;
    part.bounds = display_bounds();
    paint_part(painter, *this, part);
}

// -- keys ----------------------------------------------------------------------

// Whether the focus lies inside another TabControl inside this one: that
// control is more local and takes the tab keys (owner decision DK-30, G1-2).
bool TabControl::nested_tab_control_has_focus() const {
    if (window() == nullptr) return false;
    const Control::Ptr focused = (*window()).focused_control();
    for (Control::Ptr current = focused; current; current = (*current).parent()) {
        if (current.get() == this) return false;
        if (std::dynamic_pointer_cast<TabControl>(current)) return true;
    }
    return false;
}

void TabControl::select_relative(int delta) {
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    if (live.empty()) return;
    const std::size_t current = selected_index.read().value_or(0U);
    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(live.size());
    const std::ptrdiff_t next = (static_cast<std::ptrdiff_t>(current) + delta + count) % count;
    selected_index = static_cast<std::size_t>(next);
}

// From anywhere inside the control, before the focused control sees them:
// cycle_forward and cycle_backward (Ctrl+Tab, Ctrl+Shift+Tab; a focused
// TabControl keeps them, owner decision DK-30, G1-2) and next_tab and
// previous_tab (Ctrl+PageDown, Ctrl+PageUp) select the next or previous page,
// wrapping. A TabControl nested inside this one, holding the focus, takes
// them instead.
void TabControl::on_key_preview(KeyEvent& event) {
    if (event.action != KeyAction::down || !enabled.read() || page_count() == 0U ||
        window() == nullptr) return;
    if (nested_tab_control_has_focus()) return;
    const KeyMap& keys = effective_key_map();
    int direction = 0;
    if (keys.matches(KeyCommand::cycle_forward, event) || keys.matches(KeyCommand::next_tab, event)) {
        direction = 1;
    } else if (keys.matches(KeyCommand::cycle_backward, event) || keys.matches(KeyCommand::previous_tab, event)) {
        direction = -1;
    }
    if (direction == 0) return;
    select_relative(direction);
    event.handled = true;
}

// While the control has focus its strip has the keyboard: the arrows along
// the row, Home and End select (the strip's automatic activation). The
// commands a tab control does not offer (closing, moving a tab) are left to
// the controls around it, as is a context request (none reaches the strip).
void TabControl::on_key_down(KeyEventArgs& arguments) {
    ContainerControl::on_key_down(arguments);
    if (arguments.handled || !strip_ || window() == nullptr ||
        (*window()).focused_control().get() != this) return;
    const KeyMap& keys = effective_key_map();
    // The key command table matches the routed form.
    const KeyEvent pressed = key_event_of(arguments, KeyAction::down);
    const bool not_offered = keys.matches(KeyCommand::close_document, pressed) ||
                             keys.matches(KeyCommand::move_tab_later, pressed) ||
                             keys.matches(KeyCommand::move_tab_earlier, pressed);
    if (not_offered) return;
    (*strip_).on_key_down(arguments);
}

// The strip shows the keyboard's tab while the control has focus.
void TabControl::on_focus_changed(bool focused) {
    if (strip_) (*strip_).on_focus_changed(focused);
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- semantics -----------------------------------------------------------------

SemanticDescriptor TabControl::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::tab_group;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    const std::shared_ptr<TabPage> selected = selected_page_.lock();
    if (selected) descriptor.value = (*selected).text.read();
    const bool row = alignment.read() == TabAlignment::top || alignment.read() == TabAlignment::bottom;
    descriptor.orientation = row ? SemanticOrientation::horizontal : SemanticOrientation::vertical;
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

// A tab scrolled out of view is published without bounds and without the
// visible state, so every tab can still be reached.
std::vector<SemanticNode> TabControl::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    nodes.reserve(live.size());
    const Rect absolute = absolute_bounds();
    const std::shared_ptr<TabPage> selected = selected_page_.lock();
    for (std::size_t index = 0U; index < live.size(); ++index) {
        SemanticNode node{};
        node.stable_id = std::string((*live[index]).stable_id().value()) + ".tab";
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::tab;
        node.name = (*live[index]).text.read();
        node.tool_tip = (*live[index]).tool_tip_text.read();
        const Rect local = tab_parts(*live[index]).tab.bounds;
        if (!local.empty()) {
            node.bounds = {absolute.x + local.x, absolute.y + local.y,
                           local.width, local.height};
            node.states |= SemanticState::visible;
        }
        if (effectively_enabled()) node.states |= SemanticState::enabled;
        node.states |= SemanticState::focusable;
        if (live[index] == selected) node.states |= SemanticState::selected;
        // The page is the tab's panel (owner decision DK-40). Tabs publish
        // select, not press (DK-35); a press sent anyway still selects.
        node.controlled_panel = std::string((*live[index]).stable_id().value());
        node.actions = {SemanticAction::focus, SemanticAction::select};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool TabControl::on_semantic_child_action(std::string_view child_stable_id,
                                          SemanticAction action,
                                          std::string_view) {
    if (action != SemanticAction::focus && action != SemanticAction::select &&
        action != SemanticAction::press) return false;
    const std::vector<std::shared_ptr<TabPage>> live = pages();
    for (std::size_t index = 0U; index < live.size(); ++index) {
        if (child_stable_id !=
            std::string((*live[index]).stable_id().value()) + ".tab") continue;
        if (window() != nullptr) {
            static_cast<void>((*window()).request_focus(shared_from_this()));
        }
        selected_index = index;
        return true;
    }
    return false;
}

namespace {

constexpr std::array<PartField<TabControl::Parts>, 2> tab_control_extra_part_fields{{
    {.declaration = {.name = "face", .role = PartRole::face, .composed_from = {}},
     .member = &TabControl::Parts::face},
    {.declaration = {.name = "display", .role = PartRole::face, .composed_from = {}},
     .member = &TabControl::Parts::display},
}};

constexpr std::array<PartField<TabControl::ItemParts>, 5> tab_control_item_part_fields{{
    {.declaration = {.name = "tab", .role = PartRole::face, .composed_from = {}},
     .member = &TabControl::ItemParts::tab},
    {.declaration = {.name = "tab_label", .role = PartRole::label, .composed_from = {}},
     .member = &TabControl::ItemParts::tab_label},
    {.declaration = {.name = "tab_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &TabControl::ItemParts::tab_focus},
    {.declaration = {.name = "tab_seam", .role = PartRole::seam, .composed_from = {}},
     .member = &TabControl::ItemParts::tab_seam},
    {.declaration = {.name = "tab_icon", .role = PartRole::icon, .composed_from = {}},
     .member = &TabControl::ItemParts::tab_icon},
}};

constexpr std::array<StateField<TabControl::ItemStates>, 1> tab_control_item_state_fields{{
    {.state = AnatomyState::selected, .member = &TabControl::ItemStates::selected},
}};

constexpr std::array<PartDeclaration, 5> tab_control_item_parts =
    part_declarations(tab_control_item_part_fields);
constexpr std::array<AnatomyState, 1> tab_control_item_states =
    state_declarations(tab_control_item_state_fields);

constexpr std::array<PartDeclaration, scrollable_detail::part_count + 2>
    tab_control_parts = joined_declarations(scrollable_detail::parts,
                                         part_declarations(tab_control_extra_part_fields));

constexpr std::array<ItemSystemDeclaration, 1> tab_control_item_systems{{
    {.name = "item", .parts = tab_control_item_parts, .states = tab_control_item_states,
     .custom_states = {}},
}};

constexpr ControlAnatomy tab_control_anatomy{
    .control_id = "tab_control",
    .parts = tab_control_parts,
    .states = scrollable_detail::states,
    .custom_states = {},
    .item_systems = tab_control_item_systems,
};

} // namespace

const ControlAnatomy& TabControl::anatomy() const noexcept {
    return tab_control_anatomy;
}

void TabControl::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record{};
    record.scrollable = scrollable_parts();

    // The band behind the tabs.
    record.face = PartGeometry::from_bounds(header_bounds());
    // The paper area the pages sit on.
    record.display = PartGeometry::from_bounds(display_bounds());
    write_parts(anatomy(), scrollable_detail::part_fields, record.scrollable, geometry);
    write_parts(anatomy(), tab_control_extra_part_fields, record, geometry);
}

TabControl::Parts TabControl::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record{};
    read_parts(anatomy(), scrollable_detail::part_fields, published.parts(),
               record.scrollable);
    read_parts(anatomy(), tab_control_extra_part_fields, published.parts(), record);
    return record;
}

// Every page is an item, as before the strip; a tab scrolled out of view
// publishes no parts.
std::size_t TabControl::realized_item_count() const {
    return page_count();
}

std::string TabControl::realized_item_stable_id(std::size_t item) const {
    const std::shared_ptr<TabPage> page = page_at(item);
    const std::string stable_id = std::string((*page).stable_id().value()) + ".tab";
    return stable_id;
}

void TabControl::publish_realized_item_geometry(std::size_t item,
                                          std::span<PartGeometry> geometry) const {
    const std::shared_ptr<TabPage> page = page_at(item);
    const ItemParts record = tab_parts(*page);
    write_item_parts(anatomy().item_systems.front(), tab_control_item_part_fields, record, geometry);
}

AnatomyStateSet TabControl::realized_item_anatomy_states(std::size_t item) const {
    AnatomyStateSet states{};
    const std::optional<std::size_t> selected = selected_index.read();
    if (selected && *selected == item) {
        states.insert(AnatomyState::selected);
    }
    return states;
}

std::vector<TabControl::Item> TabControl::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records{};
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record{};
        record.index = item;
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), tab_control_item_part_fields, published.parts(), record.parts);
        read_states(tab_control_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events TabControl adds to those of ContainerControl, in declaration order.
constexpr std::array<InteractionDeclaration, 6> tab_control_interactions{{
    {.name = "draw_item_content", .payload = "ItemContentPaint", .mutable_payload = true},
    {.name = "selected_index_changed", .payload = "TabSelectionChange", .mutable_payload = false},
    {.name = "deselecting", .payload = "TabControlCancelEvent", .mutable_payload = true},
    {.name = "selecting", .payload = "TabControlCancelEvent", .mutable_payload = true},
    {.name = "deselected", .payload = "TabControlEvent", .mutable_payload = false},
    {.name = "selected", .payload = "TabControlEvent", .mutable_payload = false},
}};

} // namespace

void TabControl::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    ContainerControl::append_interactions(interactions);
    interactions.insert(interactions.end(), tab_control_interactions.begin(), tab_control_interactions.end());
}

// property_codemod begin TabControl (tools/property_families/tab_control.json; generated, do not edit)

// property_codemod end TabControl

} // namespace gui_forms
