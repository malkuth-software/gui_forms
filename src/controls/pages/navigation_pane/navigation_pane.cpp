#include "gui_forms/controls/pages/navigation_pane/navigation_pane.hpp"

#include "gui_forms/controls/pages/page_view/page_view.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/binding/property_cell_entry/property_cell_entry.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <unordered_set>
#include <utility>

namespace gui_forms {
namespace {

// Pane geometry in logical units at text scale 1, after the look studies'
// PT7 sample (docking_look_studies_source.html, `nav-tabs`): a 28-unit
// header, a 7-unit grip, 31-unit large buttons, a 30-unit overflow strip
// and a 32-unit collapsed bar.
constexpr double header_height = 28.0;
constexpr double header_padding = 8.0;
constexpr double header_button_side = 20.0;
constexpr double header_button_inset = 4.0;
constexpr double arrow_glyph_side = 9.0;
constexpr double grip_height = 7.0;
// The grip's dots: two-unit dots on a four-unit pitch (the grip_dots mark),
// five across and one row.
constexpr double grip_dots_width = 20.0;
constexpr double grip_dots_height = 4.0;
constexpr double button_height = 31.0;
constexpr double button_padding = 9.0;
constexpr double icon_label_gap = 8.0;
constexpr double overflow_height = 30.0;
constexpr double overflow_padding = 6.0;
constexpr double overflow_cell = 24.0;
constexpr double configure_width = 16.0;
constexpr double configure_inset = 3.0;
constexpr double collapsed_width = 32.0;
constexpr double collapsed_button_height = 25.0;
constexpr double collapsed_icon_height = 29.0;
constexpr double caption_padding = 10.0;
// The least the collapsed caption keeps before icons give way.
constexpr double minimum_caption_height = 48.0;
constexpr double minimum_content_height = 40.0;
constexpr double default_icon_side = 16.0;
constexpr double maximum_icon_side = 32.0;
constexpr double minimum_expanded_width = 80.0;
constexpr double maximum_expanded_width = 1024.0;

constexpr std::string_view show_more_text = "Show more buttons";
constexpr std::string_view show_fewer_text = "Show fewer buttons";
constexpr std::string_view hidden_heading_text = "Hidden pages";
constexpr std::string_view options_text = "Navigation pane options\xE2\x80\xA6";

// A square of `side` centred in `area`, on whole units; empty when it does
// not fit.
Rect centred_square(Rect area, double side) noexcept {
    if (area.width < side || area.height < side) return {};
    const Rect square{std::floor(area.x + (area.width - side) * 0.5),
                      std::floor(area.y + (area.height - side) * 0.5), side, side};
    return square;
}

Rect offset_rect(Rect bounds, double dx, double dy) noexcept {
    const Rect moved{bounds.x + dx, bounds.y + dy, bounds.width, bounds.height};
    return moved;
}

// The states of `all` that `declared` lists, so a published part carries only
// the states its declaration names.
AnatomyStateSet declared_states(AnatomyStateSet all, std::span<const AnatomyState> declared) noexcept {
    AnatomyStateSet kept{};
    for (const AnatomyState state : declared) {
        if (all.contains(state)) kept.insert(state);
    }
    return kept;
}

PartGeometry part_with_states(Rect bounds, AnatomyStateSet all, std::span<const AnatomyState> declared) noexcept {
    PartGeometry part = PartGeometry::from_bounds(bounds);
    part.states = declared_states(all, declared);
    return part;
}

// The first character of `text`, by its UTF-8 lead byte, for an icon whose
// page has no image. Text cut short keeps the bytes it has.
std::string first_character(std::string_view text) {
    if (text.empty()) return {};
    const unsigned char lead = static_cast<unsigned char>(text[0]);
    std::size_t length = 1U;
    if (lead >= 0xF0U) {
        length = 4U;
    } else if (lead >= 0xE0U) {
        length = 3U;
    } else if (lead >= 0xC0U) {
        length = 2U;
    }
    const std::size_t taken = std::min(length, text.size());
    const std::string character(text.substr(0U, taken));
    return character;
}

Rect window_rect(Rect absolute, Rect local) noexcept {
    const Rect result{absolute.x + local.x, absolute.y + local.y, local.width, local.height};
    return result;
}

} // namespace

// -- the content panel and the overlay ------------------------------------------

// The content panel: a tab_panel named by the selected page, holding that
// page's control as its only child and sizing it to fill the panel.
class NavigationPane::ContentHost final : public Control {
public:
    explicit ContentHost(StableId stable_id) : Control(std::move(stable_id)) {}

    void set_panel_name(std::string name) {
        if (panel_name_ == name) return;
        panel_name_ = std::move(name);
        invalidate(Dirty::semantics);
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        const Rect fill{0.0, 0.0, final_bounds.width, final_bounds.height};
        const std::vector<Control::Ptr> placed = snapshot_layout_children();
        for (const Control::Ptr& child : placed) {
            if (!is_current_layout_child(child)) continue;
            set_child_layout_collapsed(child, false);
            set_child_layout(child, fill);
        }
    }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor{};
        descriptor.role = SemanticRole::tab_panel;
        descriptor.name = panel_name_;
        descriptor.states = SemanticState::visible;
        descriptor.exposed = true;
        return descriptor;
    }

private:
    std::string panel_name_{};
};

// The collapsed pane's overlay: a window overlay root holding the content
// panel while it is open. It observes the pane, which owns it and releases
// it (release_pane) before letting it go.
class NavigationPane::Flyout final : public Control {
public:
    Flyout(StableId stable_id, NavigationPane& pane) : Control(std::move(stable_id)), pane_(&pane) {
        set_paint_plane(PaintPlane::overlay);
    }

    void release_pane() noexcept { pane_ = nullptr; }

    // Takes the content panel and shows it at once: the panel arrives
    // collapsed from the collapsed pane, and the focus scope that opens next
    // looks for a visible Tab stop inside it.
    void adopt(const Control::Ptr& panel) {
        add_child(panel);
        set_child_layout_collapsed(panel, false);
        if (pane_ != nullptr) set_child_layout(panel, (*pane_).flyout_layout().content);
    }

    void arrange(Rect final_bounds) override {
        arrange_self(final_bounds);
        if (pane_ == nullptr) return;
        const FlyoutLayout placed = (*pane_).flyout_layout();
        const std::vector<Control::Ptr> children = snapshot_layout_children();
        for (const Control::Ptr& child : children) {
            if (!is_current_layout_child(child)) continue;
            set_child_layout_collapsed(child, false);
            set_child_layout(child, placed.content);
        }
    }

    void on_paint(Painter& painter, Rect) override {
        if (pane_ != nullptr) (*pane_).paint_flyout(painter, *this);
    }

    [[nodiscard]] Insets visual_outsets() const noexcept override {
        const Insets shadow = overlay_shadow_outsets(*this);
        return shadow;
    }

    // Escape closes the overlay when nothing inside took it.
    void on_key(KeyEvent& event) override { close_on_escape(event); }
    void on_key_bubble(KeyEvent& event) override { close_on_escape(event); }

    void on_pointer(PointerEvent& event) override {
        // A press on the overlay's own surface stays in the overlay.
        if (event.action == PointerAction::down) event.handled = true;
    }

    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override {
        SemanticDescriptor descriptor{};
        descriptor.exposed = false;
        descriptor.include_descendants = true;
        return descriptor;
    }

private:
    void close_on_escape(KeyEvent& event) {
        if (event.handled || event.action != KeyAction::down || event.physical_key != PhysicalKey::escape) return;
        if (pane_ == nullptr) return;
        (*pane_).close_overlay();
        event.handled = true;
    }

    NavigationPane* pane_{};
};

// -- construction --------------------------------------------------------------

NavigationPane::NavigationPane(StableId stable_id) : Control(std::move(stable_id)) {
    focusable = true;
    cursor = CursorKind::arrow;
    set_paint_plane(PaintPlane::control);
    // The pane sizes itself: its width follows collapsing (measure).
    auto_size = true;
    const std::string prefix((*this).stable_id().value());
    menu_ = std::make_unique<ContextMenu>(prefix + ".menu");
    menu_invoked_ = (*menu_).item_invoked().subscribe(
        *this, Delegate<const MenuItemInvocation&>::bind<NavigationPane, &NavigationPane::on_menu_item_invoked>(*this));
    menu_changed_ = (*menu_).open_changed().subscribe(
        *this, Delegate<bool>::bind<NavigationPane, &NavigationPane::on_menu_open_changed>(*this));
    show_more_command_ = std::make_shared<Command>(prefix + ".show-more", std::string(show_more_text));
    show_fewer_command_ = std::make_shared<Command>(prefix + ".show-fewer", std::string(show_fewer_text));
    hidden_heading_command_ = std::make_shared<Command>(prefix + ".hidden-heading", std::string(hidden_heading_text));
    (*hidden_heading_command_).set_enabled(false);
    options_command_ = std::make_shared<Command>(prefix + ".options", std::string(options_text));
    selection_token_ = pages_.selected_page_changed().subscribe(
        *this,
        Delegate<const PageSelectionChange&>::bind<NavigationPane, &NavigationPane::on_selected_page_changed>(*this));
    pages_token_ = pages_.pages_changed().subscribe(
        *this, Delegate<const PageSetChange&>::bind<NavigationPane, &NavigationPane::on_pages_changed>(*this));
}

void NavigationPane::initialize_control_tree() {
    host_ = make_control<ContentHost>(StableId(std::string(stable_id().value()) + ".content"));
    add_child(host_);
}

NavigationPane::~NavigationPane() {
    // The overlay's root may outlive the pane inside its window until the
    // lease is dropped; it must not reach back.
    window_pointer_.disconnect();
    overlay_revoked_.disconnect();
    if (flyout_) (*flyout_).release_pane();
    overlay_token_.disconnect();
    if (menu_) (*menu_).close();
}

void NavigationPane::on_dispose() noexcept {
    close_overlay();
    close_configure_menu();
    if (menu_) (*menu_).dispose();
    Control::on_dispose();
}

void NavigationPane::on_attachment_committed() noexcept {
    Control::on_attachment_committed();
    Window* const host = attached_window();
    if (!focus_region.read() || host == nullptr) return;
    // A pane nested in another region, or already registered, stays out.
    try {
        static_cast<void>((*host).add_focus_region(shared_from_this()));
    } catch (...) {
    }
}

void NavigationPane::prepare_write(const decltype(focus_region)&, bool&) {
    require_mutable();
}

// The attached window's focus regions follow the cell.
void NavigationPane::written(const decltype(focus_region)&) {
    Window* const host = attached_window();
    if (host == nullptr) return;
    const Control::Ptr self = shared_from_this();
    if (focus_region.read()) {
        static_cast<void>((*host).add_focus_region(self));
        return;
    }
    std::vector<Control::Ptr> remaining = (*host).focus_regions();
    remaining.erase(std::remove(remaining.begin(), remaining.end(), self), remaining.end());
    (*host).set_focus_regions(std::move(remaining));
}

// -- properties ----------------------------------------------------------------

// collapsed and button_count are views: restore_state, the collapse
// button, the grip and the keyboard steps write their fields directly.
void NavigationPane::write_view(const decltype(collapsed)&, bool incoming) {
    require_mutable();
    if (collapsed_ == incoming) return;
    if (incoming) {
        close_configure_menu();
    } else {
        close_overlay();
    }
    collapsed_ = incoming;
    hot_ = {};
    pressed_ = {};
    grip_captured_ = false;
    stop_ = {};
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    notify_state();
}

bool NavigationPane::read_view(const decltype(collapsed)&) const {
    return collapsed_;
}

void NavigationPane::write_view(const decltype(button_count)&, std::size_t incoming) {
    require_mutable();
    if (incoming > PageSetLimits::maximum_capacity) {
        throw std::out_of_range("NavigationPane button count is above PageSetLimits::maximum_capacity");
    }
    if (button_count_ == incoming) return;
    button_count_ = incoming;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    notify_state();
}

std::size_t NavigationPane::read_view(const decltype(button_count)&) const {
    return button_count_;
}

std::size_t NavigationPane::visible_button_count() const {
    const Layout placed = layout();
    return placed.large_count;
}

bool NavigationPane::step_buttons(int direction) {
    const Layout placed = layout();
    if (placed.collapsed) return false;
    const std::size_t maximum = std::min(placed.shown_count, placed.button_capacity);
    const std::size_t current = placed.large_count;
    if (direction < 0 && current == 0U) return false;
    const std::size_t next = direction < 0 ? current - 1U : current + 1U;
    if (next > maximum) return false;
    button_count_ = next;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    notify_state();
    return true;
}

bool NavigationPane::show_more_buttons() {
    require_mutable();
    const bool changed = step_buttons(1);
    return changed;
}

bool NavigationPane::show_fewer_buttons() {
    require_mutable();
    const bool changed = step_buttons(-1);
    return changed;
}

bool NavigationPane::page_hidden(const PageId& page) const {
    const bool hidden = std::find(hidden_.begin(), hidden_.end(), page) != hidden_.end();
    return hidden;
}

bool NavigationPane::hidden_at(std::size_t position) const {
    const bool hidden = page_hidden(pages_.record_at(PagePosition{position}).id);
    return hidden;
}

bool NavigationPane::set_page_hidden(const PageId& page, bool hidden) {
    require_mutable();
    if (!pages_.find(page) || page_hidden(page) == hidden) return false;
    if (hidden) {
        hidden_.push_back(page);
    } else {
        hidden_.erase(std::remove(hidden_.begin(), hidden_.end(), page), hidden_.end());
    }
    hot_ = {};
    pressed_ = {};
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    notify_state();
    return true;
}

std::vector<PageId> NavigationPane::hidden_pages() const {
    std::vector<PageId> ordered{};
    ordered.reserve(hidden_.size());
    for (std::size_t position = 0; position < pages_.count(); ++position) {
        const PageId& id = pages_.record_at(PagePosition{position}).id;
        if (page_hidden(id)) ordered.push_back(id);
    }
    return ordered;
}

void NavigationPane::prepare_write(const decltype(image_list)&, std::shared_ptr<ImageList>& incoming) {
    require_mutable();
    if (incoming && !(*incoming).is_alive()) {
        throw std::invalid_argument("NavigationPane requires a live ImageList");
    }
    const Window* const host = attached_window();
    if (incoming && host != nullptr && !(*incoming).belongs_to(*host)) {
        throw std::invalid_argument("NavigationPane and ImageList must belong to the same Window");
    }
}

// The change subscription follows the list.
void NavigationPane::written(const decltype(image_list)&) {
    image_list_token_.disconnect();
    if (image_list.read()) {
        image_list_token_ = (*image_list.read()).changed().subscribe(
            *this,
            Delegate<const ImageListChange&>::bind<NavigationPane, &NavigationPane::on_image_list_changed>(*this));
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void NavigationPane::on_image_list_changed(const ImageListChange&) {
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- state ---------------------------------------------------------------------

NavigationPaneState NavigationPane::state() const {
    NavigationPaneState saved{};
    saved.collapsed = collapsed_;
    saved.button_count = button_count_;
    saved.hidden_pages = hidden_pages();
    saved.page_order.reserve(pages_.count());
    for (std::size_t position = 0; position < pages_.count(); ++position) {
        saved.page_order.push_back(pages_.record_at(PagePosition{position}).id);
    }
    return saved;
}

NavigationPaneRestore NavigationPane::apply_state(const NavigationPaneState& saved) {
    require_mutable();
    if (saved.button_count > PageSetLimits::maximum_capacity) {
        throw std::out_of_range("NavigationPane button count is above PageSetLimits::maximum_capacity");
    }
    std::unordered_set<std::string> ordered{};
    for (const PageId& id : saved.page_order) {
        if (!ordered.insert(id.value).second) {
            throw std::invalid_argument("NavigationPane state repeats a page in its order");
        }
    }
    std::unordered_set<std::string> hidden{};
    for (const PageId& id : saved.hidden_pages) {
        if (!hidden.insert(id.value).second) {
            throw std::invalid_argument("NavigationPane state repeats a hidden page");
        }
    }
    // Validated: from here the state is applied.
    NavigationPaneRestore restore{};
    std::unordered_set<std::string> unknown{};
    for (const PageId& id : saved.page_order) {
        if (!pages_.find(id)) unknown.insert(id.value);
    }
    for (const PageId& id : saved.hidden_pages) {
        if (!pages_.find(id)) unknown.insert(id.value);
    }
    restore.unknown_pages = unknown.size();
    applying_state_ = true;
    {
        // The known pages take the saved order at the front, in one
        // publication; pages the state does not name keep their order after
        // them. The pane's set has no pinned group, so no move is refused.
        PageSetBatch batch(pages_);
        std::size_t next = 0U;
        for (const PageId& id : saved.page_order) {
            if (!pages_.find(id)) continue;
            static_cast<void>(pages_.move(id, PagePosition{next}));
            ++next;
        }
    }
    applying_state_ = false;
    hidden_.clear();
    for (const PageId& id : saved.hidden_pages) {
        if (pages_.find(id)) hidden_.push_back(id);
    }
    if (saved.collapsed) {
        close_configure_menu();
    } else {
        close_overlay();
    }
    collapsed_ = saved.collapsed;
    button_count_ = saved.button_count;
    hot_ = {};
    pressed_ = {};
    grip_captured_ = false;
    stop_ = {};
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    notify_state();
    return restore;
}

void NavigationPane::notify_state() {
    const NavigationPaneState current = state();
    state_changed_.emit(current);
}

// -- the set's events -----------------------------------------------------------

void NavigationPane::on_selected_page_changed(const PageSelectionChange&) {
    refresh_content();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void NavigationPane::on_pages_changed(const PageSetChange& change) {
    if (change.membership) {
        // A page that left the set is no longer hidden.
        std::vector<PageId> kept{};
        kept.reserve(hidden_.size());
        for (const PageId& id : hidden_) {
            if (pages_.find(id)) kept.push_back(id);
        }
        hidden_ = std::move(kept);
    }
    if (change.membership || change.content) refresh_content();
    if (change.records && host_) (*host_).set_panel_name(selected_text());
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    if ((change.membership || change.order) && !applying_state_) notify_state();
}

// Makes the content panel hold exactly the selected page's control.
void NavigationPane::refresh_content() {
    if (!host_) return;
    const std::optional<std::size_t> selected = selected_position();
    Control::Ptr wanted{};
    if (selected) {
        // The content's derived type is the presenter family's; RTTI is the
        // boundary between the set's erased content and the control it holds.
        const PageControlContent* const content =
            dynamic_cast<const PageControlContent*>(pages_.content(pages_.record_at(PagePosition{*selected}).id));
        if (content != nullptr) wanted = (*content).control();
    }
    (*host_).set_panel_name(selected_text());
    const std::span<const Control::Ptr> current = (*host_).children();
    const std::vector<Control::Ptr> present(current.begin(), current.end());
    for (const Control::Ptr& child : present) {
        if (child != wanted) static_cast<void>((*host_).remove_child((*child).runtime_id()));
    }
    if (wanted && (*wanted).parent() != host_) {
        // A control parented elsewhere, or the root of a window, is not ours
        // to show.
        const bool detached = !(*wanted).parent() && !(*wanted).attached();
        if (detached) (*host_).add_child(wanted);
    }
    invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
}

std::optional<std::size_t> NavigationPane::selected_position() const {
    const std::optional<PageId>& selected = pages_.selected_page();
    if (!selected) return std::nullopt;
    const std::optional<PagePosition> position = pages_.find(*selected);
    if (!position) return std::nullopt;
    return (*position).value;
}

std::string NavigationPane::page_text(std::size_t position) const {
    const PageRecord& record = pages_.record_at(PagePosition{position});
    std::string text = parse_mnemonic_text(record.title).display_text;
    return text;
}

std::string NavigationPane::selected_text() const {
    const std::optional<std::size_t> selected = selected_position();
    if (!selected) return {};
    std::string text = page_text(*selected);
    return text;
}

ImageId NavigationPane::page_image(std::size_t position) const {
    const PageRecord& record = pages_.record_at(PagePosition{position});
    const Window* const host = attached_window();
    if (record.image_key.empty() || !image_list.read() || !(*image_list.read()).is_alive() || host == nullptr ||
        !(*image_list.read()).belongs_to(*host)) {
        return {};
    }
    const ImageListResolution resolved = (*image_list.read()).resolve(record.image_key, ImageVisualState::normal,
                                                               (*host).scale());
    return resolved.image;
}

double NavigationPane::icon_side() const {
    const double scale = effective_text_scale();
    double side = default_icon_side;
    if (image_list.read()) side = std::min((*image_list.read()).image_size().height, maximum_icon_side);
    const double scaled = side * scale;
    return scaled;
}

FontSpec NavigationPane::label_font() const {
    const FontSpec font{FontRole::control, 10.5, 400, false, 0.12};
    return effective_font(font);
}

FontSpec NavigationPane::header_font() const {
    const FontSpec font{FontRole::control, 12.0, 600, false, 0.12};
    return effective_font(font);
}

// -- layout ----------------------------------------------------------------------

Size NavigationPane::measure(Size available) {
    const double scale = effective_text_scale();
    const double width = collapsed_ ? collapsed_width * scale : expanded_width.read();
    // The height is the requested one, or all there is (docked left or right).
    const double requested = requested_bounds().height;
    const double height = requested > 0.0 ? requested : available.height;
    const Size result{.width = std::min(available.width, width), .height = std::min(available.height, height)};
    return result;
}

NavigationPane::Layout NavigationPane::layout() const {
    return layout_for(client_rectangle());
}

// One layout pass: the pages the buttons and icons show (every page not
// hidden, in order), then the collapsed bar or the expanded pane.
NavigationPane::Layout NavigationPane::layout_for(Rect surface) const {
    Layout placed{};
    placed.collapsed = collapsed_;
    std::vector<std::size_t> shown{};
    shown.reserve(pages_.count());
    for (std::size_t position = 0; position < pages_.count(); ++position) {
        if (!hidden_at(position)) shown.push_back(position);
    }
    placed.shown_count = shown.size();
    if (surface.empty()) return placed;
    if (collapsed_) {
        layout_collapsed(surface, shown, placed);
    } else {
        layout_expanded(surface, shown, placed);
    }
    return placed;
}

// The bar: the expand button at the top, the large buttons' pages as icons
// at the bottom, and the turned title between.
void NavigationPane::layout_collapsed(Rect surface, std::span<const std::size_t> shown, Layout& placed) const {
    const double scale = effective_text_scale();
    const double width = surface.width;
    const double height = surface.height;
    placed.collapsed_bar = surface;
    placed.collapse_button = {0.0, 0.0, width, collapsed_button_height * scale};
    placed.collapse_glyph = centred_square(placed.collapse_button, arrow_glyph_side * scale);
    const double icon_extent = collapsed_icon_height * scale;
    const double room = height - placed.collapse_button.height - minimum_caption_height * scale;
    const std::size_t fit = room > 0.0 ? static_cast<std::size_t>(std::floor(room / icon_extent)) : 0U;
    const std::size_t wanted = std::min(button_count_, shown.size());
    const std::size_t icons = std::min(wanted, fit);
    placed.button_capacity = fit;
    const double icons_top = height - static_cast<double>(icons) * icon_extent;
    placed.tabs.reserve(icons);
    for (std::size_t index = 0; index < icons; ++index) {
        TabSlot slot{};
        slot.kind = NavigationPaneItemKind::icon;
        slot.position = shown[index];
        slot.bounds = {0.0, icons_top + static_cast<double>(index) * icon_extent, width, icon_extent};
        placed.tabs.push_back(slot);
    }
    for (std::size_t index = icons; index < shown.size(); ++index) placed.unfit.push_back(shown[index]);
    const double caption_top = placed.collapse_button.height + caption_padding * scale;
    const double caption_height = std::max(0.0, icons_top - caption_top - caption_padding * scale);
    placed.collapsed_label = {0.0, caption_top, width, caption_height};
}

void NavigationPane::layout_expanded(Rect surface, std::span<const std::size_t> shown, Layout& placed) const {
    const double scale = effective_text_scale();
    const double width = surface.width;
    const double height = surface.height;
    const double side = icon_side();
    // Expanded, from the top: the header; from the bottom: the overflow strip,
    // the large buttons and the grip; the content takes what is left.
    const double header_extent = header_height * scale;
    placed.header = {0.0, 0.0, width, header_extent};
    const double button_side = header_button_side * scale;
    const double button_x = width - header_button_inset * scale - button_side;
    placed.collapse_button = {button_x, std::floor((header_extent - button_side) * 0.5), button_side, button_side};
    if (placed.collapse_button.x < 0.0) placed.collapse_button = {};
    placed.collapse_glyph = centred_square(placed.collapse_button, arrow_glyph_side * scale);
    double label_x = header_padding * scale;
    const std::optional<std::size_t> selected = selected_position();
    if (selected && page_image(*selected).value != 0U) {
        placed.header_icon = {label_x, std::floor((header_extent - side) * 0.5), side, side};
        label_x += side + icon_label_gap * scale * 0.5;
    }
    const double label_end = placed.collapse_button.empty() ? width - header_padding * scale
                                                            : placed.collapse_button.x - header_button_inset * scale;
    placed.header_label = {label_x, 0.0, std::max(0.0, label_end - label_x), header_extent};

    const double strip_extent = overflow_height * scale;
    placed.overflow_area = {0.0, std::max(header_extent, height - strip_extent), width, strip_extent};
    const double configure_extent = configure_width * scale;
    const double inset = configure_inset * scale;
    placed.configure_button = {width - overflow_padding * scale - configure_extent, placed.overflow_area.y + inset,
                               configure_extent, std::max(0.0, strip_extent - inset * 2.0)};
    if (placed.configure_button.x < 0.0) placed.configure_button = {};
    placed.configure_glyph = centred_square(placed.configure_button, arrow_glyph_side * scale);

    const double large_extent = button_height * scale;
    const double grip_extent = grip_height * scale;
    const double fixed = header_extent + grip_extent + strip_extent + minimum_content_height * scale;
    const std::size_t capacity =
        height > fixed ? static_cast<std::size_t>(std::floor((height - fixed) / large_extent)) : 0U;
    const std::size_t large = std::min({button_count_, shown.size(), capacity});
    placed.button_capacity = capacity;
    placed.large_count = large;
    const double buttons_top = placed.overflow_area.y - static_cast<double>(large) * large_extent;
    placed.button_area = {0.0, buttons_top, width, static_cast<double>(large) * large_extent};
    placed.tabs.reserve(shown.size());
    for (std::size_t index = 0; index < large; ++index) {
        TabSlot slot{};
        slot.kind = NavigationPaneItemKind::large_button;
        slot.position = shown[index];
        slot.bounds = {0.0, buttons_top + static_cast<double>(index) * large_extent, width, large_extent};
        placed.tabs.push_back(slot);
    }
    placed.grip_bar = {0.0, std::max(header_extent, buttons_top - grip_extent), width, grip_extent};
    const double dots_width = grip_dots_width * scale;
    const double dots_height = grip_dots_height;
    if (width >= dots_width) {
        placed.grip = {std::floor((width - dots_width) * 0.5),
                       placed.grip_bar.y + std::floor((grip_extent - dots_height) * 0.5), dots_width, dots_height};
    }
    placed.content = {0.0, header_extent, width, std::max(0.0, placed.grip_bar.y - header_extent)};

    // The overflow icons sit in page order at the strip's end, before the
    // configure button; pages past the last that fits are in the menu only.
    const double cell = overflow_cell * scale;
    const double strip_end = placed.configure_button.empty() ? width - overflow_padding * scale
                                                             : placed.configure_button.x;
    const double room = strip_end - overflow_padding * scale;
    const std::size_t cells = room > 0.0 ? static_cast<std::size_t>(std::floor(room / cell)) : 0U;
    const std::size_t rest = shown.size() - large;
    const std::size_t icons = std::min(rest, cells);
    const double icons_left = strip_end - static_cast<double>(icons) * cell;
    const double icons_top = placed.overflow_area.y + std::floor((strip_extent - cell) * 0.5);
    for (std::size_t index = 0; index < icons; ++index) {
        TabSlot slot{};
        slot.kind = NavigationPaneItemKind::icon;
        slot.position = shown[large + index];
        slot.bounds = {icons_left + static_cast<double>(index) * cell, icons_top, cell, cell};
        placed.tabs.push_back(slot);
    }
    for (std::size_t index = large + icons; index < shown.size(); ++index) placed.unfit.push_back(shown[index]);
}

NavigationPane::PageButtonParts NavigationPane::button_parts(const TabSlot& slot) const {
    const double scale = effective_text_scale();
    PageButtonParts parts{};
    const Rect bounds = slot.bounds;
    if (bounds.empty()) return parts;
    parts.nav_page_face = PartGeometry::from_bounds(bounds);
    double x = bounds.x + button_padding * scale;
    if (page_image(slot.position).value != 0U) {
        const double side = icon_side();
        const Rect icon{x, std::floor(bounds.y + (bounds.height - side) * 0.5), side, side};
        parts.nav_page_icon = PartGeometry::from_bounds(Rect::intersection(icon, bounds));
        x += side + icon_label_gap * scale;
    }
    const double end = bounds.x + bounds.width - button_padding * scale;
    const Rect label{x, bounds.y, std::max(0.0, end - x), bounds.height};
    parts.nav_page_label = PartGeometry::from_bounds(label);
    return parts;
}

NavigationPane::PageIconParts NavigationPane::icon_parts(const TabSlot& slot) const {
    PageIconParts parts{};
    const Rect bounds = slot.bounds;
    if (bounds.empty()) return parts;
    parts.nav_overflow_face = PartGeometry::from_bounds(bounds);
    if (page_image(slot.position).value != 0U) {
        parts.nav_overflow_icon = PartGeometry::from_bounds(centred_square(bounds, icon_side()));
    } else {
        parts.nav_overflow_label = PartGeometry::from_bounds(bounds);
    }
    return parts;
}

NavigationPane::FlyoutLayout NavigationPane::flyout_layout() const {
    FlyoutLayout placed{};
    const Window* const host = attached_window();
    if (host == nullptr) return placed;
    const double scale = effective_text_scale();
    const Rect absolute = absolute_bounds();
    const Size client = (*host).client_size();
    const double width = std::min(expanded_width.read(), client.width);
    // Beside the bar, on its right unless only the left has room.
    double x = absolute.x + absolute.width;
    if (x + width > client.width && absolute.x - width >= 0.0) x = absolute.x - width;
    x = std::clamp(x, 0.0, std::max(0.0, client.width - width));
    placed.window_bounds = {x, absolute.y, width, absolute.height};
    const double header_extent = std::min(header_height * scale, absolute.height);
    placed.face = {0.0, 0.0, width, absolute.height};
    placed.header = {0.0, 0.0, width, header_extent};
    const double padding = header_padding * scale;
    placed.label = {padding, 0.0, std::max(0.0, width - padding * 2.0), header_extent};
    placed.content = {0.0, header_extent, width, std::max(0.0, absolute.height - header_extent)};
    return placed;
}

void NavigationPane::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    const Layout placed = layout_for({0.0, 0.0, final_bounds.width, final_bounds.height});
    if (host_ && (*host_).parent().get() == this) {
        const bool shown = !placed.collapsed && !placed.content.empty();
        set_child_layout_collapsed(host_, !shown);
        if (shown) set_child_layout(host_, placed.content);
    }
    if (overlay_open_ && flyout_) {
        const FlyoutLayout overlay = flyout_layout();
        if (!((*flyout_).requested_bounds() == overlay.window_bounds)) {
            (*flyout_).set_requested_bounds(overlay.window_bounds);
        }
    }
}

// -- targets and states -------------------------------------------------------------

Rect NavigationPane::target_bounds(const Layout& placed, const Target& target) const {
    switch (target.kind) {
    case TargetKind::none:
        return {};
    case TargetKind::collapse:
        return placed.collapse_button;
    case TargetKind::caption:
        return placed.collapsed_label;
    case TargetKind::grip:
        return placed.grip_bar;
    case TargetKind::configure:
        return placed.configure_button;
    case TargetKind::tab:
        for (const TabSlot& slot : placed.tabs) {
            if (pages_.record_at(PagePosition{slot.position}).id == target.page) return slot.bounds;
        }
        return {};
    }
    return {};
}

NavigationPane::Target NavigationPane::target_at(const Layout& placed, Point local) const {
    Target found{};
    for (const TabSlot& slot : placed.tabs) {
        if (!slot.bounds.contains(local)) continue;
        found.kind = TargetKind::tab;
        found.page = pages_.record_at(PagePosition{slot.position}).id;
        return found;
    }
    if (!placed.collapse_button.empty() && placed.collapse_button.contains(local)) {
        found.kind = TargetKind::collapse;
    } else if (!placed.collapsed_label.empty() && placed.collapsed_label.contains(local)) {
        found.kind = TargetKind::caption;
    } else if (!placed.grip_bar.empty() && placed.grip_bar.contains(local)) {
        found.kind = TargetKind::grip;
    } else if (!placed.configure_button.empty() && placed.configure_button.contains(local)) {
        found.kind = TargetKind::configure;
    }
    return found;
}

AnatomyStateSet NavigationPane::target_states(const Target& target) const {
    AnatomyStateSet states{};
    if (target.kind == TargetKind::none) return states;
    if (hot_ == target) states.insert(AnatomyState::hot);
    if (pressed_ == target && target.kind != TargetKind::grip) states.insert(AnatomyState::pressed);
    if (target.kind == TargetKind::grip && grip_captured_) states.insert(AnatomyState::captured);
    if (target.kind == TargetKind::configure && configure_menu_open()) states.insert(AnatomyState::open);
    if (target.kind == TargetKind::caption && overlay_open_) states.insert(AnatomyState::open);
    const Window* const host = attached_window();
    if (focused_ && stop_ == target && host != nullptr && (*host).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    return states;
}

AnatomyStateSet NavigationPane::tab_states(std::size_t position) const {
    Target target{};
    target.kind = TargetKind::tab;
    target.page = pages_.record_at(PagePosition{position}).id;
    AnatomyStateSet states = target_states(target);
    if (selected_position() == position) states.insert(AnatomyState::selected);
    return states;
}

void NavigationPane::set_hot(const Target& target) {
    if (hot_ == target) return;
    hot_ = target;
    invalidate(Dirty::paint | Dirty::semantics);
}

void NavigationPane::set_stop(const Target& target) {
    if (stop_ == target) return;
    stop_ = target;
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- paint ---------------------------------------------------------------------------

void NavigationPane::paint_tab(Painter& painter, const TabSlot& slot) const {
    const AnatomyStateSet states = tab_states(slot.position);
    const std::string text = page_text(slot.position);
    PartPaint part{};
    part.family = ControlVisualRole::menu_item;
    part.states = states;
    part.font = label_font();
    if (slot.kind == NavigationPaneItemKind::large_button) {
        const PageButtonParts parts = button_parts(slot);
        part.role = PartRole::face;
        part.bounds = parts.nav_page_face.bounds;
        paint_part(painter, *this, part);
        if (parts.nav_page_icon.present) {
            part.role = PartRole::icon;
            part.bounds = parts.nav_page_icon.bounds;
            part.image = page_image(slot.position);
            paint_part(painter, *this, part);
            part.image = {};
        }
        part.role = PartRole::label;
        part.bounds = parts.nav_page_label.bounds;
        part.text = text;
        paint_part(painter, *this, part);
        return;
    }
    const PageIconParts parts = icon_parts(slot);
    part.role = PartRole::face;
    part.bounds = parts.nav_overflow_face.bounds;
    paint_part(painter, *this, part);
    if (parts.nav_overflow_icon.present) {
        part.role = PartRole::icon;
        part.bounds = parts.nav_overflow_icon.bounds;
        part.image = page_image(slot.position);
        paint_part(painter, *this, part);
        return;
    }
    const std::string initial = first_character(text);
    part.role = PartRole::label;
    part.bounds = parts.nav_overflow_label.bounds;
    part.alignment = PartTextAlignment::center;
    part.text = initial;
    paint_part(painter, *this, part);
}

void NavigationPane::on_paint(Painter& painter, Rect) {
    const Layout placed = layout();
    const std::string title = selected_text();
    PartPaint part{};
    part.family = ControlVisualRole::panel;
    if (placed.collapsed) {
        part.role = PartRole::face;
        part.bounds = placed.collapsed_bar;
        paint_part(painter, *this, part);
    } else {
        part.role = PartRole::face;
        part.bounds = placed.header;
        paint_part(painter, *this, part);
        const std::optional<std::size_t> selected = selected_position();
        if (!placed.header_icon.empty() && selected) {
            part.role = PartRole::icon;
            part.bounds = placed.header_icon;
            part.image = page_image(*selected);
            paint_part(painter, *this, part);
            part.image = {};
        }
        part.role = PartRole::caption;
        part.bounds = placed.header_label;
        part.font = header_font();
        part.text = title;
        paint_part(painter, *this, part);
        part.text = {};
        part.role = PartRole::well;
        part.bounds = placed.content;
        paint_part(painter, *this, part);
        Target grip_target{};
        grip_target.kind = TargetKind::grip;
        part.role = PartRole::face;
        part.bounds = placed.grip_bar;
        part.states = target_states(grip_target);
        paint_part(painter, *this, part);
        part.role = PartRole::grip;
        part.bounds = placed.grip;
        paint_part(painter, *this, part);
        part.states = {};
        part.role = PartRole::face;
        part.bounds = placed.button_area;
        paint_part(painter, *this, part);
        part.bounds = placed.overflow_area;
        paint_part(painter, *this, part);
    }
    for (const TabSlot& slot : placed.tabs) paint_tab(painter, slot);

    // The header's collapse button (the bar's expand button when collapsed).
    Target collapse_target{};
    collapse_target.kind = TargetKind::collapse;
    PartPaint button{};
    button.family = ControlVisualRole::menu_item;
    button.role = PartRole::actuator;
    button.bounds = placed.collapse_button;
    button.states = target_states(collapse_target);
    paint_part(painter, *this, button);
    button.role = PartRole::glyph;
    button.bounds = placed.collapse_glyph;
    button.mark = placed.collapsed ? PartMark::arrow_right : PartMark::arrow_left;
    paint_part(painter, *this, button);
    if (placed.collapsed) {
        // The title, turned to read bottom to top (owner decision DK-42),
        // ending at the top of its space.
        Target caption_target{};
        caption_target.kind = TargetKind::caption;
        PartPaint caption{};
        caption.family = ControlVisualRole::menu_item;
        caption.role = PartRole::label;
        caption.bounds = placed.collapsed_label;
        caption.states = target_states(caption_target);
        caption.font = header_font();
        caption.text = title;
        caption.orientation = TextOrientation::bottom_to_top;
        caption.alignment = PartTextAlignment::end;
        paint_part(painter, *this, caption);
    } else {
        Target configure_target{};
        configure_target.kind = TargetKind::configure;
        button.role = PartRole::actuator;
        button.bounds = placed.configure_button;
        button.states = target_states(configure_target);
        button.mark = PartMark::none;
        paint_part(painter, *this, button);
        button.role = PartRole::glyph;
        button.bounds = placed.configure_glyph;
        button.mark = PartMark::customize;
        paint_part(painter, *this, button);
    }
    const Target stop = stop_.kind == TargetKind::none ? default_stop(placed) : stop_;
    PartPaint focus{};
    focus.family = ControlVisualRole::panel;
    focus.role = PartRole::focus;
    focus.bounds = target_bounds(placed, stop);
    const Window* const host = attached_window();
    if (focused_ && host != nullptr && (*host).focus_cue_visible()) focus.states.insert(AnatomyState::focus_visible);
    paint_part(painter, *this, focus);
}

// Paints the overlay's surface into its own root; its content panel paints
// itself above.
void NavigationPane::paint_flyout(Painter& painter, const Control& surface) const {
    const FlyoutLayout placed = flyout_layout();
    PartPaint part{};
    part.family = ControlVisualRole::panel;
    part.role = PartRole::shadow;
    part.bounds = placed.face;
    part.corner_radius = 0.0;
    paint_part(painter, surface, part);
    part.corner_radius.reset();
    part.role = PartRole::face;
    paint_part(painter, surface, part);
    part.bounds = placed.header;
    paint_part(painter, surface, part);
    const std::string title = selected_text();
    part.role = PartRole::caption;
    part.bounds = placed.label;
    part.font = header_font();
    part.text = title;
    paint_part(painter, surface, part);
    part.text = {};
    part.role = PartRole::well;
    part.bounds = placed.content;
    paint_part(painter, surface, part);
}

// -- pointer -------------------------------------------------------------------------

void NavigationPane::begin_grip_drag(Point local) {
    const Layout placed = layout();
    grip_captured_ = true;
    grip_origin_y_ = local.y;
    grip_origin_count_ = placed.large_count;
    invalidate(Dirty::paint | Dirty::semantics);
}

// The grip moves in whole-button steps: each button height the pointer
// travels up from the press shows one more large button, down one fewer.
void NavigationPane::continue_grip_drag(Point local) {
    const Layout placed = layout();
    const double step = button_height * effective_text_scale();
    const double travelled = grip_origin_y_ - local.y;
    const double steps = std::floor(travelled / step + 0.5);
    const double origin = static_cast<double>(grip_origin_count_);
    const double maximum = static_cast<double>(std::min(placed.shown_count, placed.button_capacity));
    const double wanted = std::clamp(origin + steps, 0.0, maximum);
    const std::size_t count = static_cast<std::size_t>(wanted);
    if (count == placed.large_count) return;
    button_count_ = count;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
    notify_state();
}

void NavigationPane::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::leave) {
        set_hot({});
        return;
    }
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const Layout placed = layout();
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        if (grip_captured_) {
            continue_grip_drag(local);
            event.handled = true;
            return;
        }
        set_hot(target_at(placed, local));
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        const Target target = target_at(placed, local);
        if (target.kind == TargetKind::none) return;
        if (target.kind == TargetKind::configure) {
            if (configure_menu_open()) {
                close_configure_menu();
            } else {
                static_cast<void>(show_configure_menu(MenuOpenMode::pointer));
            }
            event.handled = true;
            return;
        }
        pressed_ = target;
        if (target.kind == TargetKind::grip) begin_grip_drag(local);
        invalidate(Dirty::paint | Dirty::semantics);
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::up) {
        const Target pressed = pressed_;
        pressed_ = {};
        if (grip_captured_) {
            grip_captured_ = false;
            invalidate(Dirty::paint | Dirty::semantics);
            event.handled = true;
            return;
        }
        invalidate(Dirty::paint | Dirty::semantics);
        // A click is a press and a release on the same target.
        const Target released = target_at(placed, local);
        if (pressed.kind != TargetKind::none && pressed == released) {
            if (pressed.kind == TargetKind::tab) {
                const std::optional<PagePosition> position = pages_.find(pressed.page);
                if (position) select_page((*position).value, PageSelectionCause::pointer);
            } else {
                activate(pressed);
            }
        }
        event.handled = pressed.kind != TargetKind::none;
    }
}

// A press outside the pane while the overlay is open closes it; the press goes
// on to whatever is under it (owner decision DK-27, E2-1), so the event is
// observed and never handled here.
void NavigationPane::on_window_pointer(const PointerEvent& event) {
    if (!overlay_open_ || event.action != PointerAction::down) return;
    const Rect absolute = absolute_bounds();
    if (absolute.contains(event.position)) return;
    close_overlay();
}

void NavigationPane::select_page(std::size_t position, PageSelectionCause cause) {
    const PageId id = pages_.record_at(PagePosition{position}).id;
    static_cast<void>(pages_.select(id, cause));
}

void NavigationPane::activate(const Target& target) {
    switch (target.kind) {
    case TargetKind::collapse:
        collapsed = !collapsed_;
        return;
    case TargetKind::caption:
        if (overlay_open_) {
            close_overlay();
        } else {
            static_cast<void>(open_overlay());
        }
        return;
    case TargetKind::configure:
        static_cast<void>(show_configure_menu(MenuOpenMode::keyboard));
        return;
    case TargetKind::tab: {
        const std::optional<PagePosition> position = pages_.find(target.page);
        if (position) select_page((*position).value, PageSelectionCause::keyboard);
        return;
    }
    case TargetKind::none:
    case TargetKind::grip:
        // Nothing to activate: the grip is dragged or stepped with the arrow
        // keys.
        return;
    }
    // A target outside the enumeration activates nothing.
    return;
}

// -- keyboard ------------------------------------------------------------------------

// The keyboard stops in Tab order. Expanded: the collapse button, the grip,
// the tab list (one stop; the arrows move inside it) and the configure
// button. Collapsed: the expand button, the title and the tab list.
std::vector<NavigationPane::Target> NavigationPane::stops(const Layout& placed) const {
    std::vector<Target> result{};
    result.reserve(4U);
    Target target{};
    if (!placed.collapse_button.empty()) {
        target.kind = TargetKind::collapse;
        result.push_back(target);
    }
    if (placed.collapsed && !placed.collapsed_label.empty()) {
        target.kind = TargetKind::caption;
        result.push_back(target);
    }
    if (!placed.collapsed && !placed.grip_bar.empty()) {
        target.kind = TargetKind::grip;
        result.push_back(target);
    }
    if (!placed.tabs.empty()) {
        Target tab = stop_.kind == TargetKind::tab && !target_bounds(placed, stop_).empty() ? stop_ : Target{};
        if (tab.kind == TargetKind::none) {
            const Target fallback = default_stop(placed);
            tab = fallback.kind == TargetKind::tab ? fallback : Target{};
        }
        result.push_back(tab);
    }
    if (!placed.collapsed && !placed.configure_button.empty()) {
        target.kind = TargetKind::configure;
        result.push_back(target);
    }
    return result;
}

// The tab of the selected page when it is realized, else the first tab, else
// the collapse button.
NavigationPane::Target NavigationPane::default_stop(const Layout& placed) const {
    Target target{};
    const std::optional<std::size_t> selected = selected_position();
    for (const TabSlot& slot : placed.tabs) {
        if (selected && slot.position == *selected) {
            target.kind = TargetKind::tab;
            target.page = pages_.record_at(PagePosition{slot.position}).id;
            return target;
        }
    }
    if (!placed.tabs.empty()) {
        target.kind = TargetKind::tab;
        target.page = pages_.record_at(PagePosition{placed.tabs.front().position}).id;
        return target;
    }
    if (!placed.collapse_button.empty()) target.kind = TargetKind::collapse;
    return target;
}

void NavigationPane::move_along_tabs(const Layout& placed, int direction, bool to_end) {
    const std::size_t count = placed.tabs.size();
    if (count == 0U) return;
    std::size_t current = 0U;
    for (std::size_t index = 0; index < count; ++index) {
        if (pages_.record_at(PagePosition{placed.tabs[index].position}).id == stop_.page) current = index;
    }
    std::size_t next = current;
    if (to_end) {
        next = direction < 0 ? 0U : count - 1U;
    } else if (direction < 0) {
        next = current == 0U ? count - 1U : current - 1U;
    } else {
        next = current + 1U == count ? 0U : current + 1U;
    }
    Target target{};
    target.kind = TargetKind::tab;
    target.page = pages_.record_at(PagePosition{placed.tabs[next].position}).id;
    set_stop(target);
}

void NavigationPane::on_key(KeyEvent& event) {
    if (!focused_ || event.action != KeyAction::down || !effectively_enabled()) return;
    const std::uint32_t key = event.physical_key;
    if (key == PhysicalKey::escape) {
        if (!overlay_open_) return;
        close_overlay();
        event.handled = true;
        return;
    }
    const Layout placed = layout();
    const std::vector<Target> all = stops(placed);
    if (all.empty()) return;
    // stops() names the tab list by the focused tab, so the current stop is
    // found by equality.
    std::optional<std::size_t> position{};
    for (std::size_t index = 0; index < all.size(); ++index) {
        if (all[index] == stop_) position = index;
    }
    if (!position) {
        const Target fallback = default_stop(placed);
        for (std::size_t index = 0; index < all.size(); ++index) {
            if (all[index] == fallback) position = index;
        }
        if (!position) position = 0U;
        set_stop(all[*position]);
    }
    const Target current = all[*position];
    const bool shift = has_modifier(event.modifiers, Modifier::shift);
    if (key == PhysicalKey::tab) {
        // Tab moves through the stops and leaves the pane after the last
        // (or, with Shift, before the first).
        if (!shift && *position + 1U < all.size()) {
            set_stop(all[*position + 1U]);
        } else if (shift && *position > 0U) {
            set_stop(all[*position - 1U]);
        } else {
            return;
        }
    } else if (current.kind == TargetKind::tab &&
               (key == PhysicalKey::up || key == PhysicalKey::left || key == PhysicalKey::down ||
                key == PhysicalKey::right)) {
        const int direction = key == PhysicalKey::up || key == PhysicalKey::left ? -1 : 1;
        move_along_tabs(placed, direction, false);
    } else if (current.kind == TargetKind::tab && (key == PhysicalKey::home || key == PhysicalKey::end)) {
        move_along_tabs(placed, key == PhysicalKey::home ? -1 : 1, true);
    } else if (current.kind == TargetKind::grip && (key == PhysicalKey::up || key == PhysicalKey::down)) {
        // The grip moves with the arrow: up shows one more large button.
        static_cast<void>(step_buttons(key == PhysicalKey::up ? 1 : -1));
    } else if (key == PhysicalKey::enter || key == PhysicalKey::space) {
        activate(current);
    } else if (current.kind == TargetKind::configure && key == PhysicalKey::down) {
        static_cast<void>(show_configure_menu(MenuOpenMode::keyboard));
    } else {
        return;
    }
    event.handled = true;
}

void NavigationPane::on_focus_changed(bool focused) {
    focused_ = focused;
    if (focused_) {
        const Layout placed = layout();
        if (target_bounds(placed, stop_).empty()) stop_ = default_stop(placed);
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- the overlay -------------------------------------------------------------------------

void NavigationPane::move_host_to_overlay() {
    if (!host_ || !flyout_ || (*host_).parent().get() != this) return;
    const Control::Ptr moved = remove_child((*host_).runtime_id());
    if (moved) (*flyout_).adopt(moved);
}

void NavigationPane::move_host_to_pane() {
    if (!host_ || !flyout_ || (*host_).parent() != flyout_) return;
    const Control::Ptr moved = (*flyout_).remove_child((*host_).runtime_id());
    if (moved) add_child(moved);
}

bool NavigationPane::open_overlay() {
    require_mutable();
    if (overlay_open_) return true;
    Window* const host = attached_window();
    if (!collapsed_ || host == nullptr || !effectively_visible() || !effectively_enabled()) return false;
    close_configure_menu();
    flyout_ = make_control<Flyout>(StableId(std::string(stable_id().value()) + ".overlay"), *this);
    const FlyoutLayout placed = flyout_layout();
    (*flyout_).set_requested_bounds(placed.window_bounds);
    overlay_token_ = (*host).open_overlay(shared_from_this(), flyout_);
    if (Event<>* const closed = overlay_token_.closed_event()) {
        overlay_revoked_ = (*closed).subscribe(*this, Delegate<>::bind<NavigationPane, &NavigationPane::on_overlay_revoked>(*this));
    }
    move_host_to_overlay();
    if (const Control::Ptr root = (*host).root()) {
        window_pointer_ = (*root).pointer_preview_observed().subscribe(
            *this, Delegate<const PointerEvent&>::bind<NavigationPane, &NavigationPane::on_window_pointer>(*this));
    }
    // Focus enters the overlay's first Tab stop and returns when it closes.
    // The scope does not contain focus: the bar stays usable while the overlay
    // is open (its title closes it, its icons switch the page shown).
    FocusScopeOptions options{};
    options.contain_focus = false;
    overlay_scope_ = (*host).begin_focus_scope(flyout_, {}, options);
    overlay_open_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    overlay_open_changed_.emit(true);
    return true;
}

void NavigationPane::close_overlay() noexcept {
    if (!overlay_open_ || overlay_closing_) return;
    release_overlay(FocusScopeCloseReason::explicit_close);
}

// The window dropped the overlay (its owner became unavailable): the content
// panel comes back and the pane forgets the overlay.
void NavigationPane::on_overlay_revoked() {
    if (!overlay_open_ || overlay_closing_) return;
    release_overlay(FocusScopeCloseReason::owner_unavailable);
}

// Closing order, for either cause: focus leaves the overlay (the scope
// restores it), the content panel returns to the pane, the window drops the
// overlay (a revoked overlay's window has already dropped it), then
// overlay_open_changed(false) is raised.
void NavigationPane::release_overlay(FocusScopeCloseReason reason) noexcept {
    overlay_closing_ = true;
    Window* const host = attached_window();
    if (host != nullptr && overlay_scope_) {
        try {
            static_cast<void>((*host).end_focus_scope(overlay_scope_, reason));
        } catch (...) {
        }
    }
    overlay_scope_ = {};
    overlay_revoked_.disconnect();
    if (flyout_) {
        try {
            move_host_to_pane();
        } catch (...) {
        }
    }
    if (reason == FocusScopeCloseReason::explicit_close) overlay_token_.disconnect();
    finish_overlay_close();
}

void NavigationPane::finish_overlay_close() noexcept {
    window_pointer_.disconnect();
    if (flyout_) (*flyout_).release_pane();
    flyout_.reset();
    overlay_open_ = false;
    overlay_closing_ = false;
    if (!is_alive()) return;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::semantics);
    overlay_open_changed_.emit(false);
}

// -- the configure menu ------------------------------------------------------------

bool NavigationPane::configure_menu_open() const noexcept {
    const bool open = menu_ && (*menu_).is_open();
    return open;
}

void NavigationPane::close_configure_menu() noexcept {
    if (menu_) (*menu_).close();
}

bool NavigationPane::show_configure_menu(MenuOpenMode mode) {
    require_mutable();
    if (mode != MenuOpenMode::keyboard && mode != MenuOpenMode::pointer) {
        throw std::invalid_argument("NavigationPane menu opening mode is invalid");
    }
    if (collapsed_ || attached_window() == nullptr) return false;
    const Layout placed = layout();
    if (placed.configure_button.empty()) return false;
    const std::string prefix(stable_id().value());
    const std::size_t maximum = std::min(placed.shown_count, placed.button_capacity);
    (*show_more_command_).set_enabled(placed.large_count < maximum);
    (*show_fewer_command_).set_enabled(placed.large_count > 0U);
    std::vector<MenuItemSpec> items{};
    items.emplace_back(prefix + ".show-more", MenuItemKind::command, show_more_command_, std::string(show_more_text));
    items.emplace_back(prefix + ".show-fewer", MenuItemKind::command, show_fewer_command_,
                       std::string(show_fewer_text));
    page_commands_.clear();
    // The pages with neither a large button nor an icon, then the pages the
    // user hid; choosing one selects it (and shows a hidden one again).
    if (!placed.unfit.empty()) items.emplace_back(prefix + ".unfit-separator", MenuItemKind::separator);
    for (const std::size_t position : placed.unfit) {
        const std::string id = prefix + ".page." + pages_.record_at(PagePosition{position}).id.value;
        const std::string text = page_text(position);
        page_commands_.push_back(std::make_shared<Command>(id, text));
        items.emplace_back(id, MenuItemKind::command, page_commands_.back(), text);
    }
    const std::vector<PageId> hidden = hidden_pages();
    if (!hidden.empty()) {
        items.emplace_back(prefix + ".hidden-separator", MenuItemKind::separator);
        // No menu row kind is a section heading: a disabled row stands in.
        items.emplace_back(prefix + ".hidden-heading", MenuItemKind::command, hidden_heading_command_,
                           std::string(hidden_heading_text));
    }
    for (const PageId& page : hidden) {
        const std::optional<PagePosition> position = pages_.find(page);
        if (!position) continue;
        const std::string id = prefix + ".page." + page.value;
        const std::string text = page_text((*position).value);
        page_commands_.push_back(std::make_shared<Command>(id, text));
        items.emplace_back(id, MenuItemKind::command, page_commands_.back(), text);
    }
    items.emplace_back(prefix + ".options-separator", MenuItemKind::separator);
    items.emplace_back(prefix + ".options", MenuItemKind::command, options_command_, std::string(options_text));
    (*menu_).set_items(std::move(items));
    const Rect absolute = absolute_bounds();
    const Point anchor{absolute.x + placed.configure_button.x + placed.configure_button.width,
                       absolute.y + placed.configure_button.y};
    (*menu_).show(shared_from_this(), anchor, mode);
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

void NavigationPane::on_menu_open_changed(bool) {
    invalidate(Dirty::paint | Dirty::semantics);
}

void NavigationPane::on_menu_item_invoked(const MenuItemInvocation& invocation) {
    const std::string prefix(stable_id().value());
    const std::string& item = invocation.item_id;
    if (item == prefix + ".show-more") {
        static_cast<void>(step_buttons(1));
        return;
    }
    if (item == prefix + ".show-fewer") {
        static_cast<void>(step_buttons(-1));
        return;
    }
    if (item == prefix + ".options") {
        const NavigationPaneOptionsRequest request{prefix};
        options_requested_.emit(request);
        return;
    }
    const std::string page_prefix = prefix + ".page.";
    if (item.size() <= page_prefix.size() || item.compare(0U, page_prefix.size(), page_prefix) != 0) return;
    const PageId page{item.substr(page_prefix.size())};
    const std::optional<PagePosition> position = pages_.find(page);
    if (!position) return;
    if (page_hidden(page)) static_cast<void>(set_page_hidden(page, false));
    select_page((*position).value, PageSelectionCause::programmatic);
}

// -- semantics ----------------------------------------------------------------------

SemanticDescriptor NavigationPane::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::group;
    descriptor.name = accessible_name.read().empty() ? "Navigation pane" : accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> NavigationPane::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    const Layout placed = layout();
    const Rect absolute = absolute_bounds();
    const std::string prefix(stable_id().value());
    SemanticState base = SemanticState::visible;
    if (effectively_enabled()) base |= SemanticState::enabled;

    if (!placed.collapse_button.empty()) {
        Target target{};
        target.kind = TargetKind::collapse;
        SemanticNode node{};
        node.stable_id = prefix + ".collapse";
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::button;
        node.name = placed.collapsed ? "Expand navigation pane" : "Collapse navigation pane";
        node.bounds = window_rect(absolute, placed.collapse_button);
        node.states = base | SemanticState::focusable;
        if (!placed.collapsed) node.states |= SemanticState::expanded;
        if (focused_ && stop_ == target) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::press, placed.collapsed ? SemanticAction::expand : SemanticAction::collapse};
        nodes.push_back(std::move(node));
    }
    if (placed.collapsed && !placed.collapsed_label.empty()) {
        Target target{};
        target.kind = TargetKind::caption;
        SemanticNode node{};
        node.stable_id = prefix + ".caption";
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::button;
        node.name = selected_text();
        node.has_popup = true;
        node.bounds = window_rect(absolute, placed.collapsed_label);
        node.states = base | SemanticState::focusable;
        if (overlay_open_) node.states |= SemanticState::expanded;
        if (focused_ && stop_ == target) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::press, overlay_open_ ? SemanticAction::collapse : SemanticAction::expand};
        nodes.push_back(std::move(node));
    }
    if (!placed.collapsed && !placed.grip_bar.empty()) {
        Target target{};
        target.kind = TargetKind::grip;
        SemanticNode node{};
        node.stable_id = prefix + ".grip";
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::split_pane;
        node.name = "Large buttons";
        node.value = std::to_string(placed.large_count);
        node.numeric_value = static_cast<double>(placed.large_count);
        node.minimum_value = 0.0;
        node.maximum_value = static_cast<double>(std::min(placed.shown_count, placed.button_capacity));
        node.bounds = window_rect(absolute, placed.grip_bar);
        node.states = base | SemanticState::focusable;
        if (focused_ && stop_ == target) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::focus, SemanticAction::increment, SemanticAction::decrement,
                        SemanticAction::set_value};
        nodes.push_back(std::move(node));
    }
    if (!placed.tabs.empty()) {
        SemanticNode list{};
        list.stable_id = prefix + ".tabs";
        list.runtime_id = virtual_semantic_runtime_id(list.stable_id);
        list.role = SemanticRole::tab_group;
        list.name = "Pages";
        list.orientation = SemanticOrientation::vertical;
        Rect area = placed.tabs.front().bounds;
        for (const TabSlot& slot : placed.tabs) area = Rect::united(area, slot.bounds);
        list.bounds = window_rect(absolute, area);
        list.states = base;
        const std::optional<std::size_t> selected = selected_position();
        const std::string panel_id = host_ ? std::string((*host_).stable_id().value()) : std::string();
        for (std::size_t index = 0; index < placed.tabs.size(); ++index) {
            const TabSlot& slot = placed.tabs[index];
            Target target{};
            target.kind = TargetKind::tab;
            target.page = pages_.record_at(PagePosition{slot.position}).id;
            SemanticNode tab{};
            tab.stable_id = prefix + ".tab." + target.page.value;
            tab.runtime_id = virtual_semantic_runtime_id(tab.stable_id);
            tab.role = SemanticRole::tab;
            tab.name = page_text(slot.position);
            tab.bounds = window_rect(absolute, slot.bounds);
            tab.position_in_set = index + 1U;
            tab.set_size = placed.tabs.size();
            tab.states = base | SemanticState::focusable;
            if (selected == slot.position) {
                tab.states |= SemanticState::selected;
                // The selected tab shows the content panel (owner decision
                // DK-40). Tabs publish select, not press (DK-35).
                tab.controlled_panel = panel_id;
            }
            if (focused_ && stop_ == target) tab.states |= SemanticState::focused;
            tab.actions = {SemanticAction::focus, SemanticAction::select};
            list.children.push_back(std::move(tab));
        }
        nodes.push_back(std::move(list));
    }
    if (!placed.collapsed && !placed.configure_button.empty()) {
        Target target{};
        target.kind = TargetKind::configure;
        SemanticNode node{};
        node.stable_id = prefix + ".configure";
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::menu_button;
        node.name = "Configure buttons";
        node.has_popup = true;
        node.bounds = window_rect(absolute, placed.configure_button);
        node.states = base | SemanticState::focusable;
        if (configure_menu_open()) node.states |= SemanticState::expanded;
        if (focused_ && stop_ == target) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::press, SemanticAction::show_menu};
        nodes.push_back(std::move(node));
    }
    return nodes;
}

bool NavigationPane::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view value) {
    const std::string prefix(stable_id().value());
    const Layout placed = layout();
    Target target{};
    if (id == prefix + ".collapse") {
        target.kind = TargetKind::collapse;
        if (action == SemanticAction::press) {
            collapsed = !collapsed_;
            return true;
        }
        if (action == SemanticAction::expand || action == SemanticAction::collapse) {
            collapsed = action == SemanticAction::collapse;
            return true;
        }
    } else if (id == prefix + ".caption") {
        if (!placed.collapsed) return false;
        target.kind = TargetKind::caption;
        if (action == SemanticAction::press) {
            activate(target);
            return true;
        }
        if (action == SemanticAction::expand) {
            const bool opened = open_overlay();
            return opened;
        }
        if (action == SemanticAction::collapse) {
            if (!overlay_open_) return false;
            close_overlay();
            return true;
        }
    } else if (id == prefix + ".grip") {
        if (placed.collapsed) return false;
        target.kind = TargetKind::grip;
        if (action == SemanticAction::increment || action == SemanticAction::decrement) {
            const bool stepped = step_buttons(action == SemanticAction::increment ? 1 : -1);
            return stepped;
        }
        if (action == SemanticAction::set_value) {
            std::size_t count = 0U;
            const char* const first = value.data();
            const char* const last = value.data() + value.size();
            const std::from_chars_result parsed = std::from_chars(first, last, count);
            const std::size_t maximum = std::min(placed.shown_count, placed.button_capacity);
            if (parsed.ec != std::errc{} || parsed.ptr != last || count > maximum) return false;
            if (count != placed.large_count) {
                button_count_ = count;
                invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
                notify_state();
            }
            return true;
        }
    } else if (id == prefix + ".configure") {
        if (placed.collapsed) return false;
        target.kind = TargetKind::configure;
        if (action == SemanticAction::press || action == SemanticAction::show_menu) {
            const bool opened = show_configure_menu(MenuOpenMode::keyboard);
            return opened;
        }
    } else {
        const std::string tab_prefix = prefix + ".tab.";
        if (id.size() <= tab_prefix.size() || id.substr(0U, tab_prefix.size()) != tab_prefix) return false;
        target.kind = TargetKind::tab;
        target.page = PageId{std::string(id.substr(tab_prefix.size()))};
        if (target_bounds(placed, target).empty()) return false;
        if (action == SemanticAction::select) {
            const std::optional<PagePosition> position = pages_.find(target.page);
            if (!position) return false;
            select_page((*position).value, PageSelectionCause::semantic);
            return true;
        }
    }
    if (action == SemanticAction::focus && target.kind != TargetKind::none) {
        if (attached_window() != nullptr) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
        set_stop(target);
        return true;
    }
    return false;
}

// -- anatomy ------------------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 3> button_states{{AnatomyState::hot, AnatomyState::pressed,
                                                     AnatomyState::focus_visible}};
constexpr std::array<AnatomyState, 4> configure_states{{AnatomyState::hot, AnatomyState::pressed,
                                                        AnatomyState::open, AnatomyState::focus_visible}};
constexpr std::array<AnatomyState, 3> grip_states{{AnatomyState::hot, AnatomyState::captured,
                                                   AnatomyState::focus_visible}};
constexpr std::array<AnatomyState, 4> caption_states{{AnatomyState::hot, AnatomyState::pressed,
                                                      AnatomyState::open, AnatomyState::focus_visible}};

using PaneParts = NavigationPane::Parts;

constexpr std::array<PartField<PaneParts>, 21> navigation_pane_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}}, .member = &PaneParts::region},
    {.declaration = {.name = "header", .role = PartRole::face, .composed_from = {}}, .member = &PaneParts::header},
    {.declaration = {.name = "header_icon", .role = PartRole::icon, .composed_from = {}},
     .member = &PaneParts::header_icon},
    {.declaration = {.name = "header_label", .role = PartRole::caption, .composed_from = {}},
     .member = &PaneParts::header_label},
    {.declaration = {.name = "collapse_button", .role = PartRole::actuator, .composed_from = "button",
                     .states = button_states},
     .member = &PaneParts::collapse_button},
    {.declaration = {.name = "collapse_glyph", .role = PartRole::glyph, .composed_from = {}, .states = button_states},
     .member = &PaneParts::collapse_glyph},
    {.declaration = {.name = "content", .role = PartRole::well, .composed_from = {}}, .member = &PaneParts::content},
    {.declaration = {.name = "grip_bar", .role = PartRole::face, .composed_from = {}, .states = grip_states},
     .member = &PaneParts::grip_bar},
    {.declaration = {.name = "grip", .role = PartRole::grip, .composed_from = {}, .states = grip_states},
     .member = &PaneParts::grip},
    {.declaration = {.name = "button_area", .role = PartRole::face, .composed_from = {}},
     .member = &PaneParts::button_area},
    {.declaration = {.name = "overflow_area", .role = PartRole::face, .composed_from = {}},
     .member = &PaneParts::overflow_area},
    {.declaration = {.name = "configure_button", .role = PartRole::actuator, .composed_from = "button",
                     .states = configure_states},
     .member = &PaneParts::configure_button},
    {.declaration = {.name = "configure_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = configure_states},
     .member = &PaneParts::configure_glyph},
    {.declaration = {.name = "collapsed_bar", .role = PartRole::face, .composed_from = {}},
     .member = &PaneParts::collapsed_bar},
    {.declaration = {.name = "collapsed_label", .role = PartRole::label, .composed_from = {},
                     .states = caption_states},
     .member = &PaneParts::collapsed_label},
    {.declaration = {.name = "overlay_shadow", .role = PartRole::shadow, .composed_from = {}},
     .member = &PaneParts::overlay_shadow},
    {.declaration = {.name = "overlay_face", .role = PartRole::face, .composed_from = {}},
     .member = &PaneParts::overlay_face},
    {.declaration = {.name = "overlay_header", .role = PartRole::face, .composed_from = {}},
     .member = &PaneParts::overlay_header},
    {.declaration = {.name = "overlay_label", .role = PartRole::caption, .composed_from = {}},
     .member = &PaneParts::overlay_label},
    {.declaration = {.name = "overlay_content", .role = PartRole::well, .composed_from = {}},
     .member = &PaneParts::overlay_content},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}}, .member = &PaneParts::focus},
}};

using ButtonParts = NavigationPane::PageButtonParts;
using IconParts = NavigationPane::PageIconParts;

constexpr std::array<PartField<ButtonParts>, 3> nav_page_part_fields{{
    {.declaration = {.name = "nav_page_face", .role = PartRole::face, .composed_from = {}},
     .member = &ButtonParts::nav_page_face},
    {.declaration = {.name = "nav_page_icon", .role = PartRole::icon, .composed_from = {}},
     .member = &ButtonParts::nav_page_icon},
    {.declaration = {.name = "nav_page_label", .role = PartRole::label, .composed_from = {}},
     .member = &ButtonParts::nav_page_label},
}};

constexpr std::array<PartField<IconParts>, 3> nav_overflow_part_fields{{
    {.declaration = {.name = "nav_overflow_face", .role = PartRole::face, .composed_from = {}},
     .member = &IconParts::nav_overflow_face},
    {.declaration = {.name = "nav_overflow_icon", .role = PartRole::icon, .composed_from = {}},
     .member = &IconParts::nav_overflow_icon},
    {.declaration = {.name = "nav_overflow_label", .role = PartRole::label, .composed_from = {}},
     .member = &IconParts::nav_overflow_label},
}};

constexpr std::array<StateField<NavigationPane::ItemStates>, 4> navigation_pane_item_state_fields{{
    {.state = AnatomyState::hot, .member = &NavigationPane::ItemStates::hot},
    {.state = AnatomyState::pressed, .member = &NavigationPane::ItemStates::pressed},
    {.state = AnatomyState::selected, .member = &NavigationPane::ItemStates::selected},
    {.state = AnatomyState::focus_visible, .member = &NavigationPane::ItemStates::focus_visible},
}};

constexpr std::array<PartDeclaration, 21> navigation_pane_parts = part_declarations(navigation_pane_part_fields);
constexpr std::array<PartDeclaration, 3> nav_page_parts = part_declarations(nav_page_part_fields);
constexpr std::array<PartDeclaration, 3> nav_overflow_parts = part_declarations(nav_overflow_part_fields);
constexpr std::array<AnatomyState, 4> navigation_pane_item_states =
    state_declarations(navigation_pane_item_state_fields);
constexpr std::array<AnatomyState, 5> navigation_pane_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

// System 0 is the large buttons, system 1 the icons.
constexpr std::array<ItemSystemDeclaration, 2> navigation_pane_item_systems{{
    {.name = "nav_page", .parts = nav_page_parts, .states = navigation_pane_item_states, .custom_states = {}},
    {.name = "nav_overflow", .parts = nav_overflow_parts, .states = navigation_pane_item_states,
     .custom_states = {}},
}};

constexpr ControlAnatomy navigation_pane_anatomy{
    .control_id = "navigation_pane",
    .parts = navigation_pane_parts,
    .states = navigation_pane_states,
    .custom_states = {},
    .item_systems = navigation_pane_item_systems,
};

} // namespace

const ControlAnatomy& NavigationPane::anatomy() const noexcept {
    return navigation_pane_anatomy;
}

void NavigationPane::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Layout placed = layout();
    const Rect surface = client_rectangle();
    Target target{};
    Parts record{};
    record.region = PartGeometry::from_bounds(surface);
    record.header = PartGeometry::from_bounds(placed.header);
    record.header_icon = PartGeometry::from_bounds(placed.header_icon);
    record.header_label = PartGeometry::from_bounds(placed.header_label);
    target.kind = TargetKind::collapse;
    record.collapse_button = part_with_states(placed.collapse_button, target_states(target), button_states);
    record.collapse_glyph = part_with_states(placed.collapse_glyph, target_states(target), button_states);
    record.content = PartGeometry::from_bounds(placed.content);
    target.kind = TargetKind::grip;
    record.grip_bar = part_with_states(placed.grip_bar, target_states(target), grip_states);
    record.grip = part_with_states(placed.grip, target_states(target), grip_states);
    record.button_area = PartGeometry::from_bounds(placed.button_area);
    record.overflow_area = PartGeometry::from_bounds(placed.overflow_area);
    target.kind = TargetKind::configure;
    record.configure_button = part_with_states(placed.configure_button, target_states(target), configure_states);
    record.configure_glyph = part_with_states(placed.configure_glyph, target_states(target), configure_states);
    record.collapsed_bar = PartGeometry::from_bounds(placed.collapsed_bar);
    target.kind = TargetKind::caption;
    record.collapsed_label = part_with_states(placed.collapsed_label, target_states(target), caption_states);
    if (overlay_open_) {
        const FlyoutLayout overlay = flyout_layout();
        const Rect absolute = absolute_bounds();
        const double dx = overlay.window_bounds.x - absolute.x;
        const double dy = overlay.window_bounds.y - absolute.y;
        record.overlay_shadow = PartGeometry::from_bounds(offset_rect(overlay.face, dx, dy));
        record.overlay_face = PartGeometry::from_bounds(offset_rect(overlay.face, dx, dy));
        record.overlay_header = PartGeometry::from_bounds(offset_rect(overlay.header, dx, dy));
        record.overlay_label = PartGeometry::from_bounds(offset_rect(overlay.label, dx, dy));
        record.overlay_content = PartGeometry::from_bounds(offset_rect(overlay.content, dx, dy));
    }
    const Target stop = stop_.kind == TargetKind::none ? default_stop(placed) : stop_;
    record.focus = PartGeometry::from_bounds(target_bounds(placed, stop));
    write_parts(anatomy(), navigation_pane_part_fields, record, geometry);
}

NavigationPane::Parts NavigationPane::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record{};
    read_parts(anatomy(), navigation_pane_part_fields, published.parts(), record);
    return record;
}

std::size_t NavigationPane::realized_item_count() const {
    const Layout placed = layout();
    return placed.tabs.size();
}

std::string NavigationPane::realized_item_stable_id(std::size_t item) const {
    const Layout placed = layout();
    const TabSlot& slot = placed.tabs.at(item);
    std::string id = std::string(stable_id().value()) + ".tab." + pages_.record_at(PagePosition{slot.position}).id.value;
    return id;
}

ItemSystemIndex NavigationPane::publish_realized_item_system(std::size_t item) const {
    const Layout placed = layout();
    const TabSlot& slot = placed.tabs.at(item);
    const ItemSystemIndex system{static_cast<std::uint8_t>(slot.kind == NavigationPaneItemKind::large_button ? 0U : 1U)};
    return system;
}

void NavigationPane::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const Layout placed = layout();
    const TabSlot& slot = placed.tabs.at(item);
    if (slot.kind == NavigationPaneItemKind::large_button) {
        const PageButtonParts record = button_parts(slot);
        write_item_parts(anatomy().item_systems[0], nav_page_part_fields, record, geometry);
        return;
    }
    const PageIconParts record = icon_parts(slot);
    write_item_parts(anatomy().item_systems[1], nav_overflow_part_fields, record, geometry);
}

AnatomyStateSet NavigationPane::realized_item_anatomy_states(std::size_t item) const {
    const Layout placed = layout();
    const TabSlot& slot = placed.tabs.at(item);
    return tab_states(slot.position);
}

std::vector<NavigationPane::Item> NavigationPane::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records{};
    records.reserve(count);
    const Layout placed = layout();
    for (std::size_t item = 0; item < count; ++item) {
        const TabSlot& slot = placed.tabs[item];
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        Item record{};
        record.kind = slot.kind;
        record.position = PagePosition{slot.position};
        record.page = pages_.record_at(PagePosition{slot.position}).id;
        record.stable_id = realized_item_stable_id(item);
        if (slot.kind == NavigationPaneItemKind::large_button) {
            read_item_parts(anatomy().item_systems[0], nav_page_part_fields, published.parts(), record.button);
        } else {
            read_item_parts(anatomy().item_systems[1], nav_overflow_part_fields, published.parts(), record.icon);
        }
        read_states(navigation_pane_item_state_fields, realized_item_anatomy_states(item), record.states);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events NavigationPane adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 3> navigation_pane_interactions{{
    {.name = "options_requested", .payload = "NavigationPaneOptionsRequest", .mutable_payload = false},
    {.name = "state_changed", .payload = "NavigationPaneState", .mutable_payload = false},
    {.name = "overlay_open_changed", .payload = "bool", .mutable_payload = false},
}};

} // namespace

void NavigationPane::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), navigation_pane_interactions.begin(), navigation_pane_interactions.end());
}

// property_codemod begin NavigationPane (tools/property_families/navigation_pane.json; generated, do not edit)

void NavigationPane::prepare_write(const decltype(expanded_width)&, double& incoming) {
    require_mutable();
    if (!std::isfinite(incoming) || incoming < minimum_expanded_width || incoming > maximum_expanded_width) {
        throw std::out_of_range("NavigationPane expanded width must be from 80 to 1024");
    }
}

void NavigationPane::written(const decltype(expanded_width)&) {
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// property_codemod end NavigationPane

} // namespace gui_forms
