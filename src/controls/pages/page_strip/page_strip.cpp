#include "gui_forms/controls/pages/page_strip/page_strip.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace gui_forms {
namespace {

// Strip geometry in logical units at text scale 1.
constexpr double strip_height = 26.0;
// The gap before the first tab, and after the last button.
constexpr double edge_inset = 2.0;
// The gap between a tab and the strip's edge away from the content; the
// tabs touch the edge the content is on.
constexpr double away_gap = 2.0;
constexpr double tab_padding = 8.0;
constexpr double image_size = 16.0;
constexpr double image_text_gap = 4.0;
constexpr double button_width = 16.0;
constexpr double glyph_size = 7.0;
constexpr double seam_thickness = 1.0;
constexpr double focus_inset = 2.0;
// The slot after a tab's label: a square for the close button or the pin,
// with the modified dot centred in it, and the gap before it.
constexpr double slot_gap = 4.0;
constexpr double slot_size = 14.0;
constexpr double dot_size = 6.0;
// The list button under shrink_then_list: padding, the count of hidden pages,
// a gap, the down arrow, padding.
constexpr double list_padding = 4.0;
constexpr double count_glyph_gap = 2.0;
// The insertion mark's thickness along the row.
constexpr double insertion_thickness = 2.0;
// Widths within this of each other are equal (the shrink pass divides).
constexpr double width_tolerance = 0.001;

// A title shown with mnemonics off keeps every `&`; the page list's menu
// parses markers, so each `&` is doubled for it.
std::string literal_menu_text(std::string_view title) {
    std::string text{};
    text.reserve(title.size());
    for (const char byte : title) {
        text.push_back(byte);
        if (byte == '&') text.push_back('&');
    }
    return text;
}

Rect to_window(Rect absolute, Rect local) noexcept {
    const Rect result{absolute.x + local.x, absolute.y + local.y, local.width, local.height};
    return result;
}

bool valid_widths(const PageItemWidths& widths) noexcept {
    const bool finite = std::isfinite(widths.minimum) && std::isfinite(widths.maximum) &&
                        std::isfinite(widths.fixed);
    if (!finite) return false;
    const bool ordered = widths.minimum > 0.0 && widths.minimum <= widths.maximum &&
                         widths.maximum <= PageItemWidths::maximum_width;
    const bool fixed = widths.fixed > 0.0 && widths.fixed <= PageItemWidths::maximum_width;
    const bool result = ordered && fixed;
    return result;
}

// The page item system's custom states, in declaration order.
constexpr CustomStateIndex pinned_state{0U};
constexpr CustomStateIndex modified_state{1U};

} // namespace

PageStrip::PageStrip(StableId stable_id, PageSet& pages) : Control(std::move(stable_id)), pages_(&pages) {
    set_focusable(true);
    cursor = CursorKind::arrow;
    set_paint_plane(PaintPlane::control);
    pages_changed_token_ = (*pages_).pages_changed().subscribe(
        *this, Delegate<const PageSetChange&>::bind<PageStrip, &PageStrip::on_pages_changed>(*this));
    selection_token_ = (*pages_).selected_page_changed().subscribe(
        *this, Delegate<const PageSelectionChange&>::bind<PageStrip, &PageStrip::on_selection_changed>(*this));
    list_ = std::make_unique<ContextMenu>(std::string((*this).stable_id().value()) + ".list");
    list_invoked_token_ = (*list_).item_invoked().subscribe(
        *this, Delegate<const MenuItemInvocation&>::bind<PageStrip, &PageStrip::on_list_item_invoked>(*this));
    list_changed_token_ = (*list_).open_changed().subscribe(
        *this, Delegate<bool>::bind<PageStrip, &PageStrip::on_list_open_changed>(*this));
}

PageStrip::~PageStrip() {
    close_page_list();
}

void PageStrip::on_dispose() noexcept {
    close_page_list();
    if (list_) (*list_).dispose();
    Control::on_dispose();
}

void PageStrip::release_page_set() noexcept {
    close_page_list();
    pages_changed_token_.disconnect();
    selection_token_.disconnect();
    pages_ = nullptr;
    keyboard_page_.reset();
    middle_pressed_.reset();
    secondary_pressed_.reset();
    drag_ = {};
    insertion_gap_.reset();
    hot_ = {};
    pressed_ = {};
    first_visible_ = 0U;
    widths_frozen_ = false;
    frozen_widths_.clear();
    ++settings_revision_;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- settings ------------------------------------------------------------------

// A setting changed: frozen widths thaw and the layout is recomputed.
void PageStrip::bump_settings() {
    widths_frozen_ = false;
    frozen_widths_.clear();
    ++settings_revision_;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void PageStrip::set_placement(PageStripPlacement placement) {
    require_mutable();
    if (placement != PageStripPlacement::top && placement != PageStripPlacement::bottom &&
        placement != PageStripPlacement::left && placement != PageStripPlacement::right) {
        throw std::invalid_argument("PageStrip placement is invalid");
    }
    if (placement_ == placement) return;
    placement_ = placement;
    bump_settings();
}

void PageStrip::set_item_sizing(PageItemSizing sizing) {
    require_mutable();
    if (sizing != PageItemSizing::content && sizing != PageItemSizing::fixed && sizing != PageItemSizing::fill) {
        throw std::invalid_argument("PageStrip item sizing is invalid");
    }
    if (item_sizing_ == sizing) return;
    item_sizing_ = sizing;
    bump_settings();
}

void PageStrip::set_item_widths(PageItemWidths widths) {
    require_mutable();
    if (!valid_widths(widths)) {
        throw std::invalid_argument(
            "PageStrip widths need 0 < minimum <= maximum <= 1024 and 0 < fixed <= 1024");
    }
    item_widths_ = widths;
    bump_settings();
}

void PageStrip::set_activation(PageStripActivation activation) {
    require_mutable();
    if (activation != PageStripActivation::automatic && activation != PageStripActivation::manual) {
        throw std::invalid_argument("PageStrip activation is invalid");
    }
    if (activation_ == activation) return;
    activation_ = activation;
    // Under automatic activation the keyboard is always on the selection.
    if (activation_ == PageStripActivation::automatic && focused_ && pages_ != nullptr) {
        keyboard_page_ = (*pages_).selected_page();
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

void PageStrip::set_overflow(PageStripOverflow overflow) {
    require_mutable();
    if (overflow != PageStripOverflow::scroll && overflow != PageStripOverflow::shrink_then_list) {
        throw std::invalid_argument("PageStrip overflow is invalid");
    }
    if (overflow_ == overflow) return;
    overflow_ = overflow;
    first_visible_ = 0U;
    close_page_list();
    bump_settings();
}

void PageStrip::set_close_buttons(PageCloseButtons buttons) {
    require_mutable();
    if (buttons != PageCloseButtons::none && buttons != PageCloseButtons::selected_and_hot &&
        buttons != PageCloseButtons::all) {
        throw std::invalid_argument("PageStrip close buttons are invalid");
    }
    if (close_buttons_ == buttons) return;
    close_buttons_ = buttons;
    bump_settings();
}

void PageStrip::set_reorder_enabled(bool enabled) {
    require_mutable();
    if (reorder_enabled_ == enabled) return;
    reorder_enabled_ = enabled;
    if (!enabled) static_cast<void>(cancel_drag());
}

void PageStrip::set_mnemonics_enabled(bool enabled) {
    require_mutable();
    if (mnemonics_enabled_ == enabled) return;
    mnemonics_enabled_ = enabled;
    bump_settings();
}

void PageStrip::set_font(FontSpec font) {
    require_mutable();
    if (!valid_font_spec(font)) throw std::invalid_argument("PageStrip font specification is invalid");
    set_font_from_code(true);
    if (font_ == font) return;
    font_ = font;
    widths_frozen_ = false;
    frozen_widths_.clear();
    ++settings_revision_;
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void PageStrip::set_draw_mode(ItemDrawMode mode) {
    require_mutable();
    if (mode != ItemDrawMode::normal && mode != ItemDrawMode::owner_draw) {
        throw std::invalid_argument("PageStrip draw mode is invalid");
    }
    if (draw_mode_ == mode) return;
    draw_mode_ = mode;
    invalidate(Dirty::paint);
}

void PageStrip::set_image_list(std::shared_ptr<ImageList> image_list) {
    require_mutable();
    if (image_list && !(*image_list).is_alive()) {
        throw std::invalid_argument("PageStrip requires a live ImageList");
    }
    if (image_list && window() != nullptr && !(*image_list).belongs_to(*window())) {
        throw std::invalid_argument("PageStrip and ImageList must belong to one Window");
    }
    if (image_list_ == image_list) return;
    image_list_token_.disconnect();
    image_list_ = std::move(image_list);
    if (image_list_) {
        image_list_token_ = (*image_list_).changed().subscribe(
            *this, Delegate<const ImageListChange&>::bind<PageStrip, &PageStrip::on_image_list_changed>(*this));
    }
    bump_settings();
}

void PageStrip::on_image_list_changed(const ImageListChange&) {
    if (is_alive()) bump_settings();
}

void PageStrip::on_attached_to_window() {
    Control::on_attached_to_window();
    if (image_list_ && !(*image_list_).belongs_to(*window())) {
        throw std::logic_error("PageStrip cannot attach to a different ImageList Window");
    }
}

// -- set notifications ---------------------------------------------------------

void PageStrip::on_pages_changed(const PageSetChange& change) {
    if (pages_ == nullptr) return;
    if (keyboard_page_ && !(*pages_).find(*keyboard_page_)) keyboard_page_ = (*pages_).selected_page();
    if (middle_pressed_ && !(*pages_).find(*middle_pressed_)) middle_pressed_.reset();
    if (secondary_pressed_ && !(*pages_).find(*secondary_pressed_)) secondary_pressed_.reset();
    const bool hot_stale = hot_.zone != Zone::none && hot_.position >= (*pages_).count();
    if (hot_stale || change.membership || change.order) hot_ = {};
    if (change.membership || change.order) {
        pressed_ = {};
        close_page_list();
        insertion_gap_.reset();
        // A claimed drag stays its claimant's; the strip's own ends.
        if (drag_.phase != DragState::Phase::claimed) drag_ = {};
    }
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void PageStrip::on_selection_changed(const PageSelectionChange& change) {
    if (pages_ == nullptr) return;
    if (focused_ && activation_ == PageStripActivation::automatic) keyboard_page_ = change.new_page;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- layout --------------------------------------------------------------------

// A strip's thickness across its row is the same on every edge: the tab
// height of a top strip is the tab width of a side strip.
Size PageStrip::measure(Size available) {
    const double scale = effective_text_scale();
    const ResolvedTextLayout metrics = resolve_text_layout_utf8("Mg", text_font(font_));
    const double preferred = std::max(strip_height * scale, metrics.logical_size.height + 10.0 * scale);
    if (!horizontal()) {
        const Size side{.width = std::min(available.width, preferred), .height = available.height};
        return side;
    }
    const Size result{.width = available.width, .height = std::min(available.height, preferred)};
    return result;
}

// A new size thaws frozen widths: they were kept for the old one.
void PageStrip::arrange(Rect final_bounds) {
    const Rect before = client_rectangle();
    arrange_self(final_bounds);
    const bool resized = before.width != final_bounds.width || before.height != final_bounds.height;
    if (resized && widths_frozen_) {
        widths_frozen_ = false;
        frozen_widths_.clear();
        ++settings_revision_;
    }
}

bool PageStrip::horizontal() const noexcept {
    const bool result = placement_ == PageStripPlacement::top || placement_ == PageStripPlacement::bottom;
    return result;
}

double PageStrip::main_extent() const noexcept {
    const double extent = horizontal() ? layout_.key.surface.width : layout_.key.surface.height;
    return extent;
}

double PageStrip::cross_extent() const noexcept {
    const double extent = horizontal() ? layout_.key.surface.height : layout_.key.surface.width;
    return extent;
}

double PageStrip::main_start(Rect bounds) const noexcept {
    const double start = horizontal() ? bounds.x : bounds.y;
    return start;
}

// The reading direction of a tab's label (owner decision DK-42).
TextOrientation PageStrip::label_orientation() const noexcept {
    if (placement_ == PageStripPlacement::left) return TextOrientation::bottom_to_top;
    if (placement_ == PageStripPlacement::right) return TextOrientation::top_to_bottom;
    return TextOrientation::horizontal;
}

PageStrip::LayoutKey PageStrip::current_key() const {
    const Rect surface = client_rectangle();
    LayoutKey key{};
    key.surface = Size{surface.width, surface.height};
    key.scale = effective_text_scale();
    key.settings_revision = settings_revision_;
    key.scroll_revision = scroll_revision_;
    key.bound = pages_ != nullptr;
    if (pages_ != nullptr) {
        key.set_revision = (*pages_).revision();
        key.page_count = (*pages_).count();
    }
    key.expanded = key.page_count;
    const bool on_tab = is_tab_zone(hot_.zone);
    if (key.bound && on_tab && hot_.position < key.page_count &&
        (*pages_).record_at(PagePosition{hot_.position}).pinned) {
        key.expanded = hot_.position;
    }
    return key;
}

const PageStrip::Layout& PageStrip::layout() const {
    const LayoutKey key = current_key();
    if (!layout_.valid || !(layout_.key == key)) compute_layout(key);
    return layout_;
}

// Fills each slot's label, flags and natural width (before fill sharing,
// frozen widths and shrinking). A pinned tab with an icon is collapsed to
// it unless it is the hot one; a pinned page without an image has nothing
// smaller to show and keeps its label.
void PageStrip::measure_tabs() const {
    const double scale = effective_text_scale();
    const FontSpec font = text_font(font_);
    const bool images = image_list_ && (*image_list_).is_alive();
    const double minimum = item_widths_.minimum * scale;
    const double maximum = item_widths_.maximum * scale;
    const double fixed = item_widths_.fixed * scale;
    const double padding = tab_padding * scale;
    const double slot = (slot_gap + slot_size) * scale;
    const bool close_reserved = close_buttons_ != PageCloseButtons::none;
    const std::size_t count = layout_.tabs.size();
    for (std::size_t position = 0; position < count; ++position) {
        const PageRecord& record = (*pages_).record_at(PagePosition{position});
        TabSlot& tab = layout_.tabs[position];
        if (mnemonics_enabled_) {
            tab.label = parse_mnemonic_text(record.title).display_text;
        } else {
            tab.label = record.title;
        }
        tab.has_icon = images && !record.image_key.empty() && (*image_list_).contains_key(record.image_key);
        tab.pinned = record.pinned;
        tab.reserves_slot = record.pinned || record.modified || (close_reserved && record.closable);
        tab.collapsed = record.pinned && tab.has_icon && position != layout_.key.expanded;
        tab.settled = false;
        tab.bounds = {};
        double natural = fixed;
        if (tab.collapsed) {
            natural = padding * 2.0 + image_size * scale;
        } else if (item_sizing_ != PageItemSizing::fixed) {
            double content = padding * 2.0;
            if (tab.has_icon) content += image_size * scale;
            if (tab.has_icon && !tab.label.empty()) content += image_text_gap * scale;
            if (!tab.label.empty()) content += resolve_text_layout_utf8(tab.label, font).logical_size.width;
            if (tab.reserves_slot) content += slot;
            natural = std::clamp(content, minimum, maximum);
        }
        tab.natural = natural;
        tab.width = natural;
    }
}

// While widths are frozen, every ordinary tab whose page had a tab when they
// froze keeps that width; others keep their measured width.
void PageStrip::apply_frozen_widths() const {
    if (!widths_frozen_) return;
    const std::size_t count = layout_.tabs.size();
    for (std::size_t position = 0; position < count; ++position) {
        TabSlot& tab = layout_.tabs[position];
        if (tab.pinned) continue;
        const PageId& page = (*pages_).record_at(PagePosition{position}).id;
        for (const FrozenWidth& frozen : frozen_widths_) {
            if (frozen.page != page) continue;
            tab.width = frozen.width;
            tab.natural = frozen.width;
            break;
        }
    }
}

// The smallest first tab from which the row still shows the tab at
// `target` within `area`; `target` itself when that tab alone is wider.
std::size_t PageStrip::first_showing(std::size_t target, double area) const {
    double used = layout_.tabs[target].width;
    std::size_t first = target;
    while (first > 0U && used + layout_.tabs[first - 1U].width <= area) {
        used += layout_.tabs[first - 1U].width;
        --first;
    }
    return first;
}

// The band a tab or button occupies: `length` along the row from `start`
// (rightwards, or on a side strip downwards), across it from the edge away
// from the content (less the away gap) to the content's edge.
Rect PageStrip::band_rect(double start, double length) const {
    const double gap = away_gap * layout_.key.scale;
    const double thickness = std::max(0.0, cross_extent() - gap);
    if (placement_ == PageStripPlacement::left) {
        const Rect band{gap, start, thickness, length};
        return band;
    }
    if (placement_ == PageStripPlacement::right) {
        const Rect band{0.0, start, thickness, length};
        return band;
    }
    const double top = placement_ == PageStripPlacement::top ? gap : 0.0;
    const Rect band{start, top, length, thickness};
    return band;
}

// `offset` and `length` run along the tab's reading direction from the end
// reading starts at: the left of a horizontal tab, the bottom of a tab on the
// left edge, the top of one on the right edge. `cross_offset` and
// `cross_length` run across it.
Rect PageStrip::along_tab(Rect tab, double offset, double length, double cross_offset,
                          double cross_length) const noexcept {
    if (placement_ == PageStripPlacement::left) {
        const Rect turned{tab.x + cross_offset, tab.y + tab.height - offset - length, cross_length, length};
        return turned;
    }
    if (placement_ == PageStripPlacement::right) {
        const Rect turned{tab.x + tab.width - cross_offset - cross_length, tab.y + offset, cross_length, length};
        return turned;
    }
    const Rect part{tab.x + offset, tab.y + cross_offset, length, cross_length};
    return part;
}

// Lays out whole tabs from `first_visible` while they fit in `area` after
// `start`; the first tab always shows, cut to the area when it is wider.
void PageStrip::place_tabs(double start, double area) const {
    const double end = start + area;
    const std::size_t count = layout_.tabs.size();
    double x = start;
    for (std::size_t position = layout_.first_visible; position < count; ++position) {
        const double width = layout_.tabs[position].width;
        if (position > layout_.first_visible && x + width > end) break;
        const double shown = std::min(width, end - x);
        if (shown <= 0.0) break;
        layout_.tabs[position].bounds = band_rect(x, shown);
        layout_.visible.push_back(position);
        x += width;
    }
}

// Places the tabs already listed in `visible`, in that order, from `start`
// to the list button (or the strip's end); a tab reaching past it is cut to
// it and those after it are dropped.
void PageStrip::place_visible(double start) const {
    const double end = layout_.overflow ? main_start(layout_.list_button)
                                        : main_extent() - edge_inset * layout_.key.scale;
    double x = start;
    std::size_t placed = 0U;
    for (; placed < layout_.visible.size(); ++placed) {
        TabSlot& tab = layout_.tabs[layout_.visible[placed]];
        const double shown = std::min(tab.width, end - x);
        if (shown <= 0.0) break;
        tab.bounds = band_rect(x, shown);
        x += tab.width;
    }
    layout_.visible.resize(placed);
}

// The width the widest tabs are cut to so that the ordinary tabs in
// [begin, end), and `extra` when given, fill `area`: the widest shrink first
// and narrower tabs keep their width (water filling: each round, the tabs no
// wider than an equal share of what is left keep their width and leave the
// share). The largest double when they fit without cutting.
double PageStrip::shrink_cap(std::size_t begin, std::size_t end, double area,
                             std::optional<std::size_t> extra) const {
    std::size_t remaining = end - begin;
    if (extra) ++remaining;
    double natural = 0.0;
    for (std::size_t position = begin; position < end; ++position) {
        layout_.tabs[position].settled = false;
        natural += layout_.tabs[position].width;
    }
    if (extra) {
        layout_.tabs[*extra].settled = false;
        natural += layout_.tabs[*extra].width;
    }
    if (remaining == 0U || natural <= area) return std::numeric_limits<double>::max();
    double left = area;
    double cap = left / static_cast<double>(remaining);
    bool changed = true;
    while (changed && remaining > 0U) {
        changed = false;
        cap = left / static_cast<double>(remaining);
        for (std::size_t position = begin; position < end; ++position) {
            TabSlot& tab = layout_.tabs[position];
            if (tab.settled || tab.width > cap) continue;
            tab.settled = true;
            left -= tab.width;
            --remaining;
            changed = true;
        }
        if (extra && !layout_.tabs[*extra].settled && layout_.tabs[*extra].width <= cap) {
            layout_.tabs[*extra].settled = true;
            left -= layout_.tabs[*extra].width;
            --remaining;
            changed = true;
        }
    }
    if (remaining == 0U) return std::numeric_limits<double>::max();
    const double result = std::max(0.0, cap);
    return result;
}

// The strip policy: whole tabs from the scroll position, and scroll back,
// scroll forward and list buttons at the end when they do not all fit.
void PageStrip::layout_scroll(bool scroll_only) const {
    const double scale = layout_.key.scale;
    const double inset = edge_inset * scale;
    const double available = std::max(0.0, main_extent() - inset * 2.0);
    const std::size_t count = layout_.tabs.size();
    double total = 0.0;
    for (const TabSlot& tab : layout_.tabs) total += tab.width;
    if (total <= available) {
        first_visible_ = 0U;
        place_tabs(inset, available);
        return;
    }
    layout_.overflow = true;
    const double buttons = button_width * scale;
    const double area = std::max(0.0, available - buttons * 3.0);
    const double end = main_extent() - inset;
    layout_.scroll_back = band_rect(end - buttons * 3.0, buttons);
    layout_.scroll_forward = band_rect(end - buttons * 2.0, buttons);
    layout_.list_button = band_rect(end - buttons, buttons);
    layout_.list_glyph = glyph_bounds(layout_.list_button);
    layout_.last_first = first_showing(count - 1U, area);
    std::size_t first = std::min(first_visible_, layout_.last_first);
    std::optional<std::size_t> target = layout_.selected;
    if (focused_ && activation_ == PageStripActivation::manual) target = keyboard_position();
    if (!scroll_only && target) {
        if (*target < first) {
            first = *target;
        } else {
            double used = 0.0;
            for (std::size_t position = first; position <= *target; ++position) {
                used += layout_.tabs[position].width;
            }
            if (*target != first && used > area) first = first_showing(*target, area);
        }
    }
    layout_.first_visible = first;
    first_visible_ = first;
    place_tabs(inset, area);
}

// The document policy: pinned tabs at their width, then the ordinary tabs
// shrunk towards the minimum, the widest first; when even the minimum does
// not fit, the front run of ordinary tabs that fits (at the minimum, then
// widened to fill the row), the selected page after it when it would be
// hidden, and the list button with the count of the rest.
void PageStrip::layout_shrink() const {
    const double scale = layout_.key.scale;
    const double inset = edge_inset * scale;
    const double available = std::max(0.0, main_extent() - inset * 2.0);
    const std::size_t count = layout_.tabs.size();
    const std::size_t pinned = std::min((*pages_).pinned_count(), count);
    double pinned_total = 0.0;
    for (std::size_t position = 0; position < pinned; ++position) pinned_total += layout_.tabs[position].width;
    const double ordinary_area = std::max(0.0, available - pinned_total);
    double natural_total = 0.0;
    for (std::size_t position = pinned; position < count; ++position) {
        natural_total += layout_.tabs[position].width;
    }
    const double minimum = item_widths_.minimum * scale;
    layout_.front_end = count;
    bool fits = natural_total <= ordinary_area;
    if (!fits && !widths_frozen_) {
        const double cap = shrink_cap(pinned, count, ordinary_area, std::nullopt);
        fits = cap >= minimum - width_tolerance;
        if (fits) {
            for (std::size_t position = pinned; position < count; ++position) {
                layout_.tabs[position].width = std::min(layout_.tabs[position].width, cap);
            }
        }
    }
    if (fits) {
        for (std::size_t position = 0; position < count; ++position) layout_.visible.push_back(position);
        place_visible(inset);
        return;
    }
    // Overflow: the list button at the strip's end, sized for the largest
    // count it can show so it does not move as the count changes.
    layout_.overflow = true;
    const FontSpec font = text_font(font_);
    const std::string widest = std::to_string(count - pinned);
    const double count_width = resolve_text_layout_utf8(widest, font).logical_size.width;
    const double list_width = (list_padding * 2.0 + count_glyph_gap + glyph_size) * scale + count_width;
    const double end = main_extent() - inset;
    const double button_start = end - list_width;
    layout_.list_button = band_rect(button_start, list_width);
    const double count_start = button_start + list_padding * scale;
    layout_.list_count = band_rect(count_start, count_width);
    const Rect arrow = band_rect(count_start + count_width + count_glyph_gap * scale, glyph_size * scale);
    layout_.list_glyph = glyph_bounds(arrow);
    const double area = std::max(0.0, ordinary_area - list_width);
    // The front run at the minimum (frozen tabs keep their width).
    double used = 0.0;
    std::size_t front = pinned;
    while (front < count) {
        const double least = widths_frozen_ ? layout_.tabs[front].width : std::min(layout_.tabs[front].width, minimum);
        if (used + least > area) break;
        used += least;
        ++front;
    }
    std::optional<std::size_t> target = layout_.selected;
    if (focused_ && activation_ == PageStripActivation::manual) target = keyboard_position();
    std::optional<std::size_t> extra{};
    if (target && *target >= front) {
        const double least = widths_frozen_ ? layout_.tabs[*target].width
                                            : std::min(layout_.tabs[*target].width, minimum);
        used += least;
        while (front > pinned && used > area) {
            --front;
            const double dropped = widths_frozen_ ? layout_.tabs[front].width
                                                  : std::min(layout_.tabs[front].width, minimum);
            used -= dropped;
        }
        extra = *target;
    }
    layout_.front_end = front;
    if (!widths_frozen_) {
        const double cap = shrink_cap(pinned, front, area, extra);
        for (std::size_t position = pinned; position < front; ++position) {
            layout_.tabs[position].width = std::min(layout_.tabs[position].width, cap);
        }
        if (extra) layout_.tabs[*extra].width = std::min(layout_.tabs[*extra].width, cap);
    }
    for (std::size_t position = 0; position < front; ++position) layout_.visible.push_back(position);
    if (extra) layout_.visible.push_back(*extra);
    place_visible(inset);
    layout_.list_count_text = std::to_string(count - layout_.visible.size());
}

void PageStrip::compute_layout(const LayoutKey& key) const {
    // Anything but a scroll scrolls the selected (or keyboard) page back
    // into view.
    const LayoutKey previous = layout_.key;
    const bool scroll_only = layout_.valid && previous.surface == key.surface && previous.scale == key.scale &&
                             previous.set_revision == key.set_revision &&
                             previous.settings_revision == key.settings_revision &&
                             previous.page_count == key.page_count && previous.expanded == key.expanded &&
                             previous.bound == key.bound;
    layout_.key = key;
    layout_.valid = true;
    layout_.rail = {0.0, 0.0, key.surface.width, key.surface.height};
    layout_.scroll_back = {};
    layout_.scroll_forward = {};
    layout_.list_button = {};
    layout_.list_count = {};
    layout_.list_glyph = {};
    layout_.list_count_text.clear();
    layout_.visible.clear();
    layout_.first_visible = 0U;
    layout_.last_first = 0U;
    layout_.front_end = 0U;
    layout_.hidden = 0U;
    layout_.overflow = false;
    layout_.selected = selected_position();
    const std::size_t count = key.bound ? key.page_count : 0U;
    layout_.tabs.resize(count);
    if (count == 0U) {
        first_visible_ = 0U;
        return;
    }
    measure_tabs();
    if (item_sizing_ == PageItemSizing::fill) {
        const double scale = key.scale;
        const double available = std::max(0.0, main_extent() - edge_inset * scale * 2.0);
        double collapsed = 0.0;
        std::size_t open = 0U;
        for (const TabSlot& tab : layout_.tabs) {
            if (tab.collapsed) {
                collapsed += tab.width;
            } else {
                ++open;
            }
        }
        if (open > 0U) {
            const double share = std::max(0.0, available - collapsed) / static_cast<double>(open);
            const double width = std::clamp(share, item_widths_.minimum * scale, item_widths_.maximum * scale);
            for (TabSlot& tab : layout_.tabs) {
                if (tab.collapsed) continue;
                tab.width = width;
                tab.natural = width;
            }
        }
    }
    apply_frozen_widths();
    if (overflow_ == PageStripOverflow::shrink_then_list) {
        first_visible_ = 0U;
        layout_shrink();
    } else {
        layout_scroll(scroll_only);
    }
    layout_.hidden = count - layout_.visible.size();
}

// -- part geometry -------------------------------------------------------------

// What a tab's slot after its label shows, with the tab as hot or not: a
// pinned tab's pin (or its modified dot while not hot); otherwise the close
// button where the rule shows it and the tab is hot, else the modified dot,
// else the close button where the rule shows it on a tab that is not hot.
PageStrip::SlotContent PageStrip::slot_content(std::size_t position, bool hot) const {
    const Layout& placed = layout();
    const TabSlot& tab = placed.tabs[position];
    if (!tab.reserves_slot || tab.collapsed) return SlotContent::none;
    const PageRecord& record = (*pages_).record_at(PagePosition{position});
    if (record.pinned) {
        if (record.modified && !hot) return SlotContent::modified;
        return SlotContent::pin;
    }
    const bool selected = placed.selected && *placed.selected == position;
    bool close_shown = false;
    if (record.closable && close_buttons_ == PageCloseButtons::all) close_shown = true;
    if (record.closable && close_buttons_ == PageCloseButtons::selected_and_hot) close_shown = hot || selected;
    if (close_shown && hot) return SlotContent::close;
    if (record.modified) return SlotContent::modified;
    if (close_shown) return SlotContent::close;
    return SlotContent::none;
}

// One tab's parts, from its laid-out bounds; all absent when the tab is not
// laid out. Interior, along the reading direction from where reading starts
// (see along_tab): padding, icon, gap, label, the slot gap, the slot (close,
// pin or modified dot), padding; each centred across the tab. The slot's
// space is reserved whether or not anything shows in it. A collapsed pinned
// tab shows its icon centred and nothing else. The seam lies along the edge
// the content is on.
PageStrip::ItemParts PageStrip::tab_parts_as(std::size_t position, bool hot) const {
    const Layout& placed = layout();
    ItemParts parts{};
    if (position >= placed.tabs.size()) return parts;
    const TabSlot& tab = placed.tabs[position];
    const Rect bounds = slid_bounds(position);
    if (bounds.empty()) return parts;
    const double scale = placed.key.scale;
    parts.tab = PartGeometry::from_bounds(bounds);
    const bool flat = horizontal();
    const double along = flat ? bounds.width : bounds.height;
    const double across = flat ? bounds.height : bounds.width;
    const double padding = tab_padding * scale;
    double interior_end = along - padding;
    double start = padding;
    if (tab.reserves_slot && !tab.collapsed) {
        const double side = slot_size * scale;
        const double slot_offset = interior_end - side;
        const double slot_cross = std::floor((across - side) * 0.5);
        const Rect slot = along_tab(bounds, slot_offset, side, slot_cross, side);
        const Rect shown = Rect::intersection(slot, bounds);
        interior_end = slot_offset - slot_gap * scale;
        const SlotContent content = slot_content(position, hot);
        // An empty slot (none) publishes no part.
        if (content == SlotContent::close) {
            const AnatomyStateSet states = slot_states(Zone::tab_close, position);
            parts.tab_close = PartGeometry::from_bounds(shown);
            parts.tab_close.states = states;
            parts.tab_close_glyph = PartGeometry::from_bounds(glyph_bounds(shown));
            parts.tab_close_glyph.states = states;
        } else if (content == SlotContent::pin) {
            AnatomyStateSet states = slot_states(Zone::tab_pin, position);
            // The pin is drawn pinned: the part is checked.
            states.insert(AnatomyState::checked);
            parts.tab_pin = PartGeometry::from_bounds(shown);
            parts.tab_pin.states = states;
            parts.tab_pin_glyph = PartGeometry::from_bounds(glyph_bounds(shown));
            parts.tab_pin_glyph.states = states;
        } else if (content == SlotContent::modified) {
            const double dot = dot_size * scale;
            const double inset = std::floor((side - dot) * 0.5);
            const Rect mark = along_tab(bounds, slot_offset + inset, dot, slot_cross + inset, dot);
            parts.tab_modified = PartGeometry::from_bounds(Rect::intersection(mark, bounds));
        }
    }
    if (tab.has_icon) {
        const double side = image_size * scale;
        double icon_offset = start;
        if (tab.collapsed) icon_offset = std::floor((along - side) * 0.5);
        const Rect icon = along_tab(bounds, icon_offset, side, std::floor((across - side) * 0.5), side);
        parts.tab_icon = PartGeometry::from_bounds(Rect::intersection(icon, bounds));
        start = icon_offset + side + image_text_gap * scale;
    }
    if (!tab.collapsed && !tab.label.empty() && interior_end > start) {
        const Rect label = along_tab(bounds, start, interior_end - start, 0.0, across);
        parts.tab_label = PartGeometry::from_bounds(label);
    }
    if (placed.selected && *placed.selected == position) {
        const double thickness = seam_thickness * scale;
        Rect seam{bounds.x, bounds.y + bounds.height - thickness, bounds.width, thickness};
        if (placement_ == PageStripPlacement::bottom) seam = {bounds.x, bounds.y, bounds.width, thickness};
        if (placement_ == PageStripPlacement::left) {
            seam = {bounds.x + bounds.width - thickness, bounds.y, thickness, bounds.height};
        }
        if (placement_ == PageStripPlacement::right) seam = {bounds.x, bounds.y, thickness, bounds.height};
        parts.tab_seam = PartGeometry::from_bounds(seam);
    }
    const double inset = focus_inset * scale;
    const Rect focus{bounds.x + inset, bounds.y + inset, std::max(0.0, bounds.width - inset * 2.0),
                     std::max(0.0, bounds.height - inset * 2.0)};
    parts.tab_focus = PartGeometry::from_bounds(focus);
    return parts;
}

PageStrip::ItemParts PageStrip::tab_parts(std::size_t position) const {
    const ItemParts parts = tab_parts_as(position, tab_hot(position));
    return parts;
}

PageStrip::ItemParts PageStrip::tab_parts_of(const PageId& page) const {
    if (pages_ == nullptr) return {};
    const std::optional<PagePosition> found = (*pages_).find(page);
    if (!found) return {};
    const ItemParts parts = tab_parts((*found).value);
    return parts;
}

ControlVisualRole PageStrip::tab_family() const noexcept {
    return ControlVisualRole::menu_item;
}

// A button's glyph: a square centred in it, on whole units.
Rect PageStrip::glyph_bounds(Rect button) const {
    const double side = glyph_size * effective_text_scale();
    if (button.width < side || button.height < side) return {};
    const Rect glyph{std::floor(button.x + (button.width - side) * 0.5),
                     std::floor(button.y + (button.height - side) * 0.5), side, side};
    return glyph;
}

PageStrip::Parts PageStrip::strip_parts() const {
    const Layout& placed = layout();
    const Rect surface = client_rectangle();
    Parts record{};
    record.region = PartGeometry::from_bounds(surface);
    record.rail = PartGeometry::from_bounds(placed.rail);
    record.scroll_back = PartGeometry::from_bounds(placed.scroll_back);
    record.scroll_back_glyph = PartGeometry::from_bounds(glyph_bounds(placed.scroll_back));
    record.scroll_forward = PartGeometry::from_bounds(placed.scroll_forward);
    record.scroll_forward_glyph = PartGeometry::from_bounds(glyph_bounds(placed.scroll_forward));
    record.list_button = PartGeometry::from_bounds(placed.list_button);
    record.list_count = PartGeometry::from_bounds(placed.list_count);
    record.list_glyph = PartGeometry::from_bounds(placed.list_glyph);
    record.insertion_mark = PartGeometry::from_bounds(insertion_mark_bounds());
    if (record.insertion_mark.present) record.insertion_mark.states.insert(AnatomyState::drop_accepted);
    record.focus = PartGeometry::from_bounds(surface);
    if (placed.overflow) {
        const AnatomyStateSet back = button_states(Zone::scroll_back);
        const AnatomyStateSet forward = button_states(Zone::scroll_forward);
        const AnatomyStateSet list = button_states(Zone::list_button);
        record.scroll_back.states = back;
        record.scroll_back_glyph.states = back;
        record.scroll_forward.states = forward;
        record.scroll_forward_glyph.states = forward;
        record.list_button.states = list;
        record.list_count.states = list;
        record.list_glyph.states = list;
    }
    return record;
}

// -- states --------------------------------------------------------------------

bool PageStrip::focus_cue() const noexcept {
    const Window* const host = attached_window();
    const bool result = focused_ && host != nullptr && (*host).focus_cue_visible();
    return result;
}

std::optional<std::size_t> PageStrip::selected_position() const {
    if (pages_ == nullptr || !(*pages_).selected_page()) return std::nullopt;
    const std::optional<PagePosition> found = (*pages_).find(*(*pages_).selected_page());
    if (!found) return std::nullopt;
    const std::size_t position = (*found).value;
    return position;
}

std::optional<std::size_t> PageStrip::keyboard_position() const {
    if (pages_ == nullptr || !focused_) return std::nullopt;
    if (!keyboard_page_) return selected_position();
    const std::optional<PagePosition> found = (*pages_).find(*keyboard_page_);
    if (!found) return selected_position();
    const std::size_t position = (*found).value;
    return position;
}

// A tab's own zones: its body, its close button and its pin.
bool PageStrip::is_tab_zone(Zone zone) noexcept {
    const bool tab = zone == Zone::tab || zone == Zone::tab_close || zone == Zone::tab_pin;
    return tab;
}

// A tab is hot while the pointer is over it, its close button or its pin.
bool PageStrip::tab_hot(std::size_t position) const noexcept {
    const bool on_tab = is_tab_zone(hot_.zone);
    const bool result = on_tab && hot_.position == position;
    return result;
}

AnatomyStateSet PageStrip::tab_states(std::size_t position) const {
    AnatomyStateSet states{};
    if (tab_hot(position)) states.insert(AnatomyState::hot);
    if (pressed_.zone == Zone::tab && pressed_.position == position) states.insert(AnatomyState::pressed);
    if (drag_.phase == DragState::Phase::sliding && drag_.origin == position) states.insert(AnatomyState::captured);
    const std::optional<std::size_t> selected = layout().selected;
    if (selected && *selected == position) states.insert(AnatomyState::selected);
    if (!effectively_enabled()) states.insert(AnatomyState::disabled);
    if (focus_cue()) {
        const std::optional<std::size_t> keyboard = keyboard_position();
        if (keyboard && *keyboard == position) states.insert(AnatomyState::focus_visible);
    }
    if (pages_ != nullptr && position < (*pages_).count()) {
        const PageRecord& record = (*pages_).record_at(PagePosition{position});
        if (record.pinned) states.insert(pinned_state);
        if (record.modified) states.insert(modified_state);
    }
    return states;
}

// The states of a tab's close button or pin: its own hot and pressed.
AnatomyStateSet PageStrip::slot_states(Zone zone, std::size_t position) const {
    AnatomyStateSet states{};
    const Hit own{zone, position};
    if (hot_ == own) states.insert(AnatomyState::hot);
    if (pressed_ == own) states.insert(AnatomyState::pressed);
    if (!effectively_enabled()) states.insert(AnatomyState::disabled);
    return states;
}

bool PageStrip::can_scroll_backward() const {
    const Layout& placed = layout();
    const bool result = overflow_ == PageStripOverflow::scroll && placed.overflow && placed.first_visible > 0U;
    return result;
}

bool PageStrip::can_scroll_forward() const {
    const Layout& placed = layout();
    const bool result =
        overflow_ == PageStripOverflow::scroll && placed.overflow && placed.first_visible < placed.last_first;
    return result;
}

AnatomyStateSet PageStrip::button_states(Zone zone) const {
    AnatomyStateSet states{};
    if (hot_.zone == zone) states.insert(AnatomyState::hot);
    if (pressed_.zone == zone) states.insert(AnatomyState::pressed);
    bool available = effectively_enabled();
    if (zone == Zone::scroll_back) available = available && can_scroll_backward();
    if (zone == Zone::scroll_forward) available = available && can_scroll_forward();
    if (zone == Zone::list_button && list_open_) states.insert(AnatomyState::open);
    if (!available && zone != Zone::list_button) states.insert(AnatomyState::disabled);
    return states;
}

// -- paint ---------------------------------------------------------------------

// Paints from the cached layout and the strip's own state: no allocation per
// tab (a tab's label is the layout's string, borrowed for the call).
void PageStrip::on_paint(Painter& painter, Rect) {
    const Layout& placed = layout();
    PartPaint part{};
    part.font = text_font(font_);
    part.family = ControlVisualRole::panel;
    part.role = PartRole::face;
    part.bounds = placed.rail;
    // Each part is keyed by its anatomy name (and a tab's by its page ID),
    // so that a theme's `page-strip::part(...)` rules select it.
    part.key = PartKey{"rail", {}, {}};
    paint_part(painter, *this, part);
    part.key.reset();
    // Tabs: flat at rest, a plate when hot or selected (the menu-item
    // family, tab_family) until T4 draws Plan Paint's tab stack and the
    // notched document tab (owner decisions DK-40, DK-42). The close button
    // and pin use the same family for their own plate, shown only while
    // they are hot.
    part.family = tab_family();
    part.alignment = PartTextAlignment::center;
    // Labels and the hidden count turn on a side strip (owner decision
    // DK-42); every other part reads the orientation not at all.
    part.orientation = label_orientation();
    const double scale = window() != nullptr ? (*window()).scale() : 1.0;
    // The dragged tab of a live reorder is painted last, over the tabs it
    // passes.
    const bool sliding = drag_.phase == DragState::Phase::sliding;
    // An owner-draw handler that changed the strip ends this paint; the
    // change has already asked for the next one.
    for (const std::size_t position : placed.visible) {
        if (sliding && position == drag_.origin) continue;
        if (!paint_tab(painter, part, position, scale)) return;
    }
    if (sliding && !paint_tab(painter, part, drag_.origin, scale)) return;
    const Rect mark = insertion_mark_bounds();
    if (!mark.empty()) {
        // The drop role has no recipe of its own until T2: a button plate.
        PartPaint bar = part;
        bar.family = ControlVisualRole::button;
        bar.role = PartRole::drop;
        bar.bounds = mark;
        bar.states = {};
        bar.states.insert(AnatomyState::drop_accepted);
        bar.key = PartKey{"insertion_mark", {}, {}};
        paint_part(painter, *this, bar);
    }
    if (!placed.overflow) return;
    const Parts own = strip_parts();
    const std::array<const PartGeometry*, 3> buttons{&own.scroll_back, &own.scroll_forward, &own.list_button};
    const std::array<const PartGeometry*, 3> glyphs{&own.scroll_back_glyph, &own.scroll_forward_glyph,
                                                    &own.list_glyph};
    // The list opens a list of pages: an open down chevron, as a combo
    // box's, rather than the tool strip's overflow mark.
    std::array<PartMark, 3> marks{PartMark::arrow_left, PartMark::arrow_right, PartMark::chevron_down};
    constexpr std::array<std::string_view, 3> button_names{"scroll_back", "scroll_forward", "list_button"};
    constexpr std::array<std::string_view, 3> glyph_names{"scroll_back_glyph", "scroll_forward_glyph", "list_glyph"};
    if (!horizontal()) {
        marks[0] = PartMark::arrow_up;
        marks[1] = PartMark::arrow_down;
    }
    for (std::size_t index = 0; index < buttons.size(); ++index) {
        if (!(*buttons[index]).present) continue;
        part.states = (*buttons[index]).states;
        part.role = PartRole::actuator;
        part.bounds = (*buttons[index]).bounds;
        part.key = PartKey{button_names[index], {}, {}};
        paint_part(painter, *this, part);
        part.role = PartRole::glyph;
        part.bounds = (*glyphs[index]).bounds;
        part.mark = marks[index];
        part.key = PartKey{glyph_names[index], {}, {}};
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
    if (own.list_count.present) {
        part.states = own.list_count.states;
        part.role = PartRole::label;
        part.bounds = own.list_count.bounds;
        part.text = placed.list_count_text;
        part.key = PartKey{"list_count", {}, {}};
        paint_part(painter, *this, part);
        part.text = {};
    }
    part.key.reset();
}

// One tab and its parts, in paint order: face, icon, label, the slot's
// close button, pin or modified dot, seam, focus. `part` carries the font,
// family, alignment and orientation; its other fields are left reset.
bool PageStrip::paint_tab(Painter& painter, PartPaint& part, std::size_t position, double scale) {
    const Layout& placed = layout();
    const ItemParts parts = tab_parts(position);
    const AnatomyStateSet states = tab_states(position);
    // The strip's own copy of the page ID: the parts' keys borrow it, and it
    // stays valid while a handler below changes the set.
    paint_stable_id_ = (*pages_).record_at(PagePosition{position}).id.value;
    part.states = states;
    part.role = PartRole::face;
    part.bounds = parts.tab.bounds;
    part.key = PartKey{"tab", paint_stable_id_, {}};
    paint_part(painter, *this, part);
    bool default_content = true;
    if (draw_mode_ == ItemDrawMode::owner_draw && (parts.tab_icon.present || parts.tab_label.present)) {
        Rect content = parts.tab_label.bounds;
        if (parts.tab_icon.present) {
            content = parts.tab_label.present ? Rect::united(parts.tab_icon.bounds, parts.tab_label.bounds)
                                              : parts.tab_icon.bounds;
        }
        // The handlers see the page ID through the strip's own copy, valid
        // for the whole raise even when one of them changes the set.
        ItemContentPaint request{.painter = painter,
                                 .index = position,
                                 .column = 0U,
                                 .stable_id = paint_stable_id_,
                                 .content = content,
                                 .states = states,
                                 .font = part.font,
                                 .text = item_content_text(*this, part.family, states)};
        const LayoutKey key = placed.key;
        const PageSet* const set = pages_;
        default_content = raise_item_content_paint(draw_item_content_, request);
        // `placed` is the strip's cached layout: a handler's change would
        // replace it, so nothing of it is read again once the key moved.
        if (!is_alive() || pages_ != set || !(current_key() == key)) return false;
    }
    if (default_content && parts.tab_icon.present) {
        const PageRecord& record = (*pages_).record_at(PagePosition{position});
        const ImageVisualState state = states.contains(AnatomyState::selected) ? ImageVisualState::selected
                                                                                : ImageVisualState::normal;
        const ImageListResolution resolved = (*image_list_).resolve(record.image_key, state, scale);
        part.role = PartRole::icon;
        part.bounds = parts.tab_icon.bounds;
        part.key = PartKey{"tab_icon", paint_stable_id_, {}};
        part.image = resolved.image;
        paint_part(painter, *this, part);
        part.image = {};
    }
    if (default_content && parts.tab_label.present) {
        part.role = PartRole::label;
        part.bounds = parts.tab_label.bounds;
        part.key = PartKey{"tab_label", paint_stable_id_, {}};
        part.text = placed.tabs[position].label;
        paint_part(painter, *this, part);
        part.text = {};
    }
    if (parts.tab_close.present) {
        part.states = parts.tab_close.states;
        part.role = PartRole::actuator;
        part.bounds = parts.tab_close.bounds;
        part.key = PartKey{"tab_close", paint_stable_id_, {}};
        paint_part(painter, *this, part);
        part.role = PartRole::glyph;
        part.bounds = parts.tab_close_glyph.bounds;
        part.key = PartKey{"tab_close_glyph", paint_stable_id_, {}};
        part.mark = PartMark::close;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
    if (parts.tab_pin.present) {
        part.states = parts.tab_pin.states;
        part.role = PartRole::actuator;
        part.bounds = parts.tab_pin.bounds;
        part.key = PartKey{"tab_pin", paint_stable_id_, {}};
        paint_part(painter, *this, part);
        part.role = PartRole::glyph;
        part.bounds = parts.tab_pin_glyph.bounds;
        part.key = PartKey{"tab_pin_glyph", paint_stable_id_, {}};
        part.mark = PartMark::pin;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
    part.states = states;
    if (parts.tab_modified.present) {
        // The modified dot: a filled round mark, the nearest admitted
        // mark (there is no `modified` mark; DK-35 gave the state only).
        part.role = PartRole::glyph;
        part.bounds = parts.tab_modified.bounds;
        part.key = PartKey{"tab_modified", paint_stable_id_, {}};
        part.mark = PartMark::radio_dot;
        paint_part(painter, *this, part);
        part.mark = PartMark::none;
    }
    if (parts.tab_seam.present) {
        part.role = PartRole::seam;
        part.bounds = parts.tab_seam.bounds;
        part.key = PartKey{"tab_seam", paint_stable_id_, {}};
        paint_part(painter, *this, part);
    }
    part.role = PartRole::focus;
    part.bounds = parts.tab_focus.bounds;
    part.key = PartKey{"tab_focus", paint_stable_id_, {}};
    paint_part(painter, *this, part);
    part.key.reset();
    return true;
}

// -- input ---------------------------------------------------------------------

PageStrip::Hit PageStrip::hit_at(Point window_point) const {
    Hit hit{};
    const Rect absolute = absolute_bounds();
    if (!absolute.contains(window_point)) return hit;
    const Point local{window_point.x - absolute.x, window_point.y - absolute.y};
    const Layout& placed = layout();
    if (placed.overflow) {
        if (placed.scroll_back.contains(local)) hit.zone = Zone::scroll_back;
        if (placed.scroll_forward.contains(local)) hit.zone = Zone::scroll_forward;
        if (placed.list_button.contains(local)) hit.zone = Zone::list_button;
        if (hit.zone != Zone::none) return hit;
    }
    // The dragged tab of a live reorder lies over the others.
    if (drag_.phase == DragState::Phase::sliding && slid_bounds(drag_.origin).contains(local)) {
        hit.position = drag_.origin;
        hit.zone = Zone::tab;
        return hit;
    }
    for (const std::size_t position : placed.visible) {
        if (!slid_bounds(position).contains(local)) continue;
        hit.position = position;
        hit.zone = Zone::tab;
        // The tab under the pointer is hot, so its slot is asked as hot.
        const ItemParts parts = tab_parts_as(position, true);
        if (parts.tab_close.present && parts.tab_close.bounds.contains(local)) hit.zone = Zone::tab_close;
        if (parts.tab_pin.present && parts.tab_pin.bounds.contains(local)) hit.zone = Zone::tab_pin;
        return hit;
    }
    return hit;
}

std::optional<PageId> PageStrip::page_at(Point window_point) const {
    const Hit hit = hit_at(window_point);
    const bool on_tab = is_tab_zone(hit.zone);
    if (!on_tab || pages_ == nullptr) return std::nullopt;
    const PageId page = (*pages_).record_at(PagePosition{hit.position}).id;
    return page;
}

std::optional<PageId> PageStrip::focused_page() const {
    const std::optional<std::size_t> position = keyboard_position();
    if (!position) return std::nullopt;
    const PageId page = (*pages_).record_at(PagePosition{*position}).id;
    return page;
}

void PageStrip::set_hot(Hit hit) {
    if (hot_ == hit) return;
    hot_ = hit;
    // A pinned tab expands while hot, which moves the tabs after it.
    invalidate(Dirty::paint | Dirty::hit_test);
}

bool PageStrip::choose(std::size_t position, PageSelectionCause cause) {
    if (pages_ == nullptr || position >= (*pages_).count()) return false;
    const PageId page = (*pages_).record_at(PagePosition{position}).id;
    const PageEditStatus status = (*pages_).select(page, cause);
    if (!page_edit_succeeded(status)) return false;
    // A handler may have released the set or disposed the strip.
    if (pages_ == nullptr || !is_alive()) return false;
    if (focused_) keyboard_page_ = page;
    const PageInvocation invoked{page, cause};
    page_invoked_.emit(invoked);
    return true;
}

// Under shrink_then_list a hidden ordinary page takes the last visible
// position (interview P13): selected first, so a cancelled selection moves
// nothing, then moved there, in one batch (one pages_changed, one
// selected_page_changed). The pages before that position keep their places.
bool PageStrip::choose_from_list(std::size_t position) {
    if (pages_ == nullptr || position >= (*pages_).count()) return false;
    const Layout& placed = layout();
    const std::size_t pinned = (*pages_).pinned_count();
    const bool hidden = placed.tabs[position].bounds.empty();
    const bool relocate = overflow_ == PageStripOverflow::shrink_then_list && hidden && position >= pinned;
    const PageId page = (*pages_).record_at(PagePosition{position}).id;
    if (!relocate) {
        const bool chosen = choose(position, PageSelectionCause::pointer);
        if (!chosen || !is_alive() || pages_ == nullptr) return chosen;
        const std::optional<PagePosition> still = (*pages_).find(page);
        if (still) reveal((*still).value);
        return chosen;
    }
    std::size_t destination = pinned;
    if (placed.front_end > pinned) destination = placed.front_end - 1U;
    const Control::Ptr keep = shared_from_this();
    PageSetBatch batch(*pages_);
    const PageEditStatus selected = (*pages_).select(page, PageSelectionCause::pointer);
    if (!page_edit_succeeded(selected)) {
        batch.close();
        return false;
    }
    // The destination lies in the ordinary group, as the page does, so the
    // move cannot cross the pinned boundary.
    const PageEditStatus moved = (*pages_).move(page, PagePosition{destination});
    static_cast<void>(moved);
    batch.close();
    if (!is_alive() || pages_ == nullptr) return false;
    if (focused_) keyboard_page_ = page;
    const PageInvocation invoked{page, PageSelectionCause::pointer};
    page_invoked_.emit(invoked);
    return true;
}

void PageStrip::reveal(std::size_t position) {
    const Layout& placed = layout();
    if (overflow_ != PageStripOverflow::scroll) return;
    if (!placed.overflow || position >= placed.tabs.size()) return;
    const std::size_t end = placed.first_visible + placed.visible.size();
    if (position >= placed.first_visible && position < end) return;
    const double scale = placed.key.scale;
    const double available = std::max(0.0, main_extent() - edge_inset * scale * 2.0);
    const double area = std::max(0.0, available - button_width * scale * 3.0);
    std::size_t first = position;
    if (position >= end) first = first_showing(position, area);
    first_visible_ = std::min(first, placed.last_first);
    ++scroll_revision_;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

bool PageStrip::scroll_by(int direction) {
    const bool possible = direction < 0 ? can_scroll_backward() : can_scroll_forward();
    if (!possible) return false;
    const Layout& placed = layout();
    if (direction < 0) {
        first_visible_ = placed.first_visible - 1U;
    } else {
        first_visible_ = placed.first_visible + 1U;
    }
    ++scroll_revision_;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    return true;
}

bool PageStrip::scroll_backward() {
    require_mutable();
    const bool scrolled = scroll_by(-1);
    return scrolled;
}

bool PageStrip::scroll_forward() {
    require_mutable();
    const bool scrolled = scroll_by(1);
    return scrolled;
}

bool PageStrip::ensure_visible(const PageId& page) {
    require_mutable();
    if (pages_ == nullptr) return false;
    const std::optional<PagePosition> found = (*pages_).find(page);
    if (!found) return false;
    reveal((*found).value);
    return true;
}

bool PageStrip::overflowing() const {
    const bool result = layout().overflow;
    return result;
}

std::vector<PageId> PageStrip::visible_pages() const {
    std::vector<PageId> visible{};
    const Layout& placed = layout();
    visible.reserve(placed.visible.size());
    for (const std::size_t position : placed.visible) {
        visible.push_back((*pages_).record_at(PagePosition{position}).id);
    }
    return visible;
}

std::size_t PageStrip::hidden_count() const {
    const std::size_t hidden = layout().hidden;
    return hidden;
}

bool PageStrip::select_relative(int direction, PageSelectionCause cause) {
    require_mutable();
    if (pages_ == nullptr || (*pages_).count() == 0U || (direction != 1 && direction != -1)) return false;
    const std::size_t count = (*pages_).count();
    const std::size_t current = selected_position().value_or(0U);
    const std::size_t next = direction > 0 ? (current + 1U) % count : (current + count - 1U) % count;
    const bool chosen = choose(next, cause);
    return chosen;
}

bool PageStrip::move_selected(int direction) {
    require_mutable();
    if (pages_ == nullptr || (direction != 1 && direction != -1)) return false;
    const std::optional<std::size_t> selected = selected_position();
    if (!selected) return false;
    const std::size_t pinned = (*pages_).pinned_count();
    const bool in_pinned = *selected < pinned;
    const std::size_t group_begin = in_pinned ? 0U : pinned;
    const std::size_t group_end = in_pinned ? pinned : (*pages_).count();
    if (direction < 0 && *selected == group_begin) return false;
    if (direction > 0 && *selected + 1U >= group_end) return false;
    const std::size_t destination = direction > 0 ? *selected + 1U : *selected - 1U;
    const PageId page = (*pages_).record_at(PagePosition{*selected}).id;
    const PageEditStatus status = (*pages_).move(page, PagePosition{destination});
    const bool moved = status == PageEditStatus::applied;
    return moved;
}

bool PageStrip::request_close(const PageId& page, PageCloseCause cause) {
    require_mutable();
    if (pages_ == nullptr) return false;
    const std::optional<PagePosition> found = (*pages_).find(page);
    if (!found || !(*pages_).record_at(*found).closable) return false;
    // The request holds its own copy of the identity: a handler may remove
    // the page `page` refers to.
    PageCloseRequest request{page, cause, false};
    const Control::Ptr keep = shared_from_this();
    page_close_requested_.emit(request);
    if (!is_alive()) return request.handled;
    if (!request.handled) close_unhandled(request);
    return request.handled;
}

void PageStrip::close_unhandled(PageCloseRequest&) {}

void PageStrip::request_pin(std::size_t position, bool pinned) {
    if (pages_ == nullptr || position >= (*pages_).count()) return;
    PagePinRequest request{(*pages_).record_at(PagePosition{position}).id, pinned, false};
    const Control::Ptr keep = shared_from_this();
    page_pin_requested_.emit(request);
    if (!is_alive() || pages_ == nullptr || request.handled) return;
    static_cast<void>((*pages_).set_pinned(request.page, request.pinned));
}

// -- dragging ------------------------------------------------------------------

double PageStrip::main_length(Rect bounds) const noexcept {
    const double length = horizontal() ? bounds.width : bounds.height;
    return length;
}

double PageStrip::local_main(Point window_point) const {
    const Rect absolute = absolute_bounds();
    const double local = horizontal() ? window_point.x - absolute.x : window_point.y - absolute.y;
    return local;
}

// A tab can slide when it is laid out in its group's contiguous run: not
// scrolled out of view, and not the selected page shown after the front
// run under shrink_then_list.
bool PageStrip::slidable(std::size_t position) const {
    const Layout& placed = layout();
    if (position >= placed.tabs.size() || placed.tabs[position].bounds.empty()) return false;
    const bool shrinking = overflow_ == PageStripOverflow::shrink_then_list && placed.overflow;
    const bool result = !shrinking || position < placed.front_end;
    return result;
}

std::optional<PageId> PageStrip::dragged_page() const {
    if (drag_.phase != DragState::Phase::sliding) return std::nullopt;
    const PageId page = drag_.page;
    return page;
}

bool PageStrip::cancel_drag() {
    if (drag_.phase != DragState::Phase::sliding) return false;
    drag_ = {};
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
    return true;
}

void PageStrip::arm_drag(std::size_t position, Point press) {
    drag_ = {};
    if (pages_ == nullptr || position >= (*pages_).count()) return;
    const Layout& placed = layout();
    const Rect resting = placed.tabs[position].bounds;
    if (resting.empty()) return;
    drag_.phase = DragState::Phase::armed;
    drag_.page = (*pages_).record_at(PagePosition{position}).id;
    drag_.origin = position;
    drag_.destination = position;
    drag_.press = press;
    drag_.grab = local_main(press) - main_start(resting);
    drag_.shown_start = main_start(resting);
}

// An armed press that moves past the threshold raises page_drag_started; a
// claimed drag is handed over, an unclaimed one slides when reordering is
// enabled. A sliding drag follows the pointer.
void PageStrip::on_drag_move(Point window_point) {
    if (drag_.phase == DragState::Phase::armed) {
        const double dx = std::fabs(window_point.x - drag_.press.x);
        const double dy = std::fabs(window_point.y - drag_.press.y);
        if (dx < page_drag_threshold && dy < page_drag_threshold) return;
        PageDragStart start{drag_.page, drag_.press, window_point, false};
        const Control::Ptr keep = shared_from_this();
        page_drag_started_.emit(start);
        if (!is_alive() || pages_ == nullptr) return;
        if (start.claimed) {
            drag_.phase = DragState::Phase::claimed;
            pressed_ = {};
            // Hand the capture over, unless the claimant has already taken it.
            Window* const host = window();
            if (host != nullptr && (*host).captured_control().get() == this) (*host).release_pointer();
            invalidate(Dirty::paint);
            return;
        }
        const std::optional<PagePosition> found = (*pages_).find(drag_.page);
        if (!reorder_enabled_ || !found || !slidable((*found).value)) {
            drag_ = {};
            return;
        }
        drag_.origin = (*found).value;
        drag_.destination = drag_.origin;
        drag_.phase = DragState::Phase::sliding;
        pressed_ = {};
    }
    if (drag_.phase != DragState::Phase::sliding) return;
    update_slide(local_main(window_point));
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// The dragged tab is drawn at the pointer, kept within the visible run of
// its group (the pinned tabs or the ordinary ones, contiguous in set order);
// it would take the place of the furthest tab of its group whose middle its
// own middle has reached, so held against either end of the run it takes
// the end place. The other tabs' middles are their resting ones.
void PageStrip::update_slide(double pointer) {
    const Layout& placed = layout();
    const std::size_t origin = drag_.origin;
    const Rect resting = placed.tabs[origin].bounds;
    const double length = main_length(resting);
    const std::size_t pinned = (*pages_).pinned_count();
    const bool pinned_group = origin < pinned;
    std::size_t first = origin;
    std::size_t last = origin;
    for (const std::size_t position : placed.visible) {
        if ((position < pinned) != pinned_group || !slidable(position)) continue;
        first = std::min(first, position);
        last = std::max(last, position);
    }
    const double run_start = main_start(placed.tabs[first].bounds);
    const double run_end = main_start(placed.tabs[last].bounds) + main_length(placed.tabs[last].bounds);
    const double shown = std::clamp(pointer - drag_.grab, run_start, std::max(run_start, run_end - length));
    const double middle = shown + length * 0.5;
    std::size_t destination = origin;
    for (std::size_t position = origin; position > first;) {
        --position;
        const Rect other = placed.tabs[position].bounds;
        if (middle > main_start(other) + main_length(other) * 0.5) break;
        destination = position;
    }
    if (destination == origin) {
        for (std::size_t position = origin + 1U; position <= last; ++position) {
            const Rect other = placed.tabs[position].bounds;
            if (middle < main_start(other) + main_length(other) * 0.5) break;
            destination = position;
        }
    }
    drag_.shown_start = shown;
    drag_.destination = destination;
}

// A tab's bounds as drawn: its laid-out bounds, or during a live reorder the
// dragged tab at the pointer and the tabs it passed moved aside by its
// length.
Rect PageStrip::slid_bounds(std::size_t position) const {
    const Layout& placed = layout();
    const Rect bounds = placed.tabs[position].bounds;
    if (drag_.phase != DragState::Phase::sliding || bounds.empty()) return bounds;
    const std::size_t origin = drag_.origin;
    const std::size_t destination = drag_.destination;
    const double length = main_length(placed.tabs[origin].bounds);
    double shift = 0.0;
    if (position == origin) {
        shift = drag_.shown_start - main_start(bounds);
    } else if (origin < destination && position > origin && position <= destination) {
        shift = -length;
    } else if (destination < origin && position >= destination && position < origin) {
        shift = length;
    }
    Rect moved = bounds;
    if (horizontal()) {
        moved.x += shift;
    } else {
        moved.y += shift;
    }
    return moved;
}

std::optional<PagePosition> PageStrip::insertion_position_at(Point window_point) const {
    if (pages_ == nullptr || !absolute_bounds().contains(window_point)) return std::nullopt;
    const Layout& placed = layout();
    const double along = local_main(window_point);
    const std::size_t pinned = (*pages_).pinned_count();
    const bool shrinking = overflow_ == PageStripOverflow::shrink_then_list && placed.overflow;
    std::size_t gap = 0U;
    for (const std::size_t position : placed.visible) {
        if (shrinking && position >= placed.front_end) continue;
        const Rect bounds = placed.tabs[position].bounds;
        if (along < main_start(bounds) + main_length(bounds) * 0.5) {
            gap = position;
            const PagePosition before{std::max(gap, pinned)};
            return before;
        }
        gap = position + 1U;
    }
    const PagePosition after{std::max(gap, pinned)};
    return after;
}

// The mark lies across the row at the gap: centred on the start of the tab
// at that position, or on the end of the tab before it.
Rect PageStrip::insertion_mark_bounds() const {
    if (!insertion_gap_ || pages_ == nullptr) return {};
    const Layout& placed = layout();
    const std::size_t gap = (*insertion_gap_).value;
    const double thickness = insertion_thickness * placed.key.scale;
    const bool shrinking = overflow_ == PageStripOverflow::shrink_then_list && placed.overflow;
    for (const std::size_t position : placed.visible) {
        if (shrinking && position >= placed.front_end) continue;
        const Rect bounds = placed.tabs[position].bounds;
        if (position == gap) {
            const Rect mark = band_rect(main_start(bounds) - thickness * 0.5, thickness);
            return mark;
        }
        if (position + 1U == gap) {
            const Rect mark = band_rect(main_start(bounds) + main_length(bounds) - thickness * 0.5, thickness);
            return mark;
        }
    }
    return {};
}

bool PageStrip::show_insertion_mark(PagePosition gap) {
    require_mutable();
    if (pages_ == nullptr || gap.value > (*pages_).count()) {
        clear_insertion_mark();
        return false;
    }
    insertion_gap_ = gap;
    invalidate(Dirty::paint);
    const bool shown = !insertion_mark_bounds().empty();
    if (!shown) insertion_gap_.reset();
    return shown;
}

void PageStrip::clear_insertion_mark() noexcept {
    if (!insertion_gap_) return;
    insertion_gap_.reset();
    invalidate(Dirty::paint);
}

void PageStrip::request_context_menu(std::size_t position, Point anchor, PageContextMenuCause cause) {
    if (pages_ == nullptr || position >= (*pages_).count()) return;
    PageContextMenuRequest request{(*pages_).record_at(PagePosition{position}).id, anchor, cause, false};
    const Control::Ptr keep = shared_from_this();
    page_context_menu_requested_.emit(request);
}

// The keyboard context menu's anchor for the tab at `position`, in window
// coordinates: the corner of the tab on the content's side where reading
// starts. A tab that is not laid out (scrolled out of view) is scrolled into
// view first. One that still has no bounds (under shrink_then_list, or a
// strip too short to show it) anchors at the same corner of the list
// button, or of the strip itself when there is no list button.
Point PageStrip::keyboard_menu_anchor(std::size_t position) {
    if (layout().tabs[position].bounds.empty()) reveal(position);
    const Layout& placed = layout();
    Rect target = placed.tabs[position].bounds;
    if (target.empty()) target = placed.list_button;
    if (target.empty()) target = placed.rail;
    const Rect absolute = absolute_bounds();
    Point anchor{absolute.x + target.x, absolute.y + target.y + target.height};
    if (placement_ == PageStripPlacement::bottom) anchor = {absolute.x + target.x, absolute.y + target.y};
    if (placement_ == PageStripPlacement::left) {
        anchor = {absolute.x + target.x + target.width, absolute.y + target.y + target.height};
    }
    if (placement_ == PageStripPlacement::right) anchor = {absolute.x + target.x, absolute.y + target.y};
    return anchor;
}

// A secondary press on a tab, released over the same tab, asks for its
// context menu at the pointer; it selects nothing.
void PageStrip::on_secondary_button(PointerEvent& event, Hit hit) {
    const bool on_tab = is_tab_zone(hit.zone);
    if (event.action == PointerAction::down) {
        secondary_pressed_.reset();
        if (!on_tab || pages_ == nullptr) return;
        secondary_pressed_ = (*pages_).record_at(PagePosition{hit.position}).id;
        event.handled = true;
        return;
    }
    if (event.action != PointerAction::up) return;
    const std::optional<PageId> pressed = secondary_pressed_;
    secondary_pressed_.reset();
    if (!pressed || !on_tab || pages_ == nullptr) return;
    if ((*pages_).record_at(PagePosition{hit.position}).id != *pressed) return;
    event.handled = true;
    request_context_menu(hit.position, event.position, PageContextMenuCause::pointer);
}

// Keeps every ordinary tab's current width until the pointer leaves the
// strip, so that after a close the next tab's close button arrives under
// the pointer (owner decision DK-38, interview P16).
void PageStrip::freeze_widths() {
    const Layout& placed = layout();
    frozen_widths_.clear();
    const std::size_t count = placed.tabs.size();
    for (std::size_t position = 0; position < count; ++position) {
        if (placed.tabs[position].pinned) continue;
        const FrozenWidth frozen{(*pages_).record_at(PagePosition{position}).id, placed.tabs[position].width};
        frozen_widths_.push_back(frozen);
    }
    widths_frozen_ = true;
}

void PageStrip::thaw_widths() {
    if (!widths_frozen_) return;
    widths_frozen_ = false;
    frozen_widths_.clear();
    ++settings_revision_;
    invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// The keyboard moves to `position`: under automatic activation that selects
// it; under manual activation only the keyboard's page moves.
void PageStrip::move_keyboard(std::size_t position) {
    if (activation_ == PageStripActivation::automatic) {
        static_cast<void>(choose(position, PageSelectionCause::keyboard));
        return;
    }
    keyboard_page_ = (*pages_).record_at(PagePosition{position}).id;
    reveal(position);
    invalidate(Dirty::paint | Dirty::semantics);
}

// A primary release: a close button or pin pressed and released over itself
// asks for the close or the unpin.
void PageStrip::on_pointer_release(PointerEvent& event, Hit hit) {
    if (drag_.phase == DragState::Phase::sliding || drag_.phase == DragState::Phase::claimed) {
        // A live reorder commits with one move; a claimed drag was handed
        // over and its release is the claimant's.
        const bool sliding = drag_.phase == DragState::Phase::sliding;
        const PageId page = drag_.page;
        const std::size_t origin = drag_.origin;
        const std::size_t destination = drag_.destination;
        drag_ = {};
        pressed_ = {};
        event.handled = true;
        invalidate(Dirty::paint | Dirty::hit_test | Dirty::semantics);
        if (sliding && destination != origin && pages_ != nullptr) {
            static_cast<void>((*pages_).move(page, PagePosition{destination}));
        }
        return;
    }
    drag_ = {};
    const Hit pressed = pressed_;
    const bool engaged = pressed.zone != Zone::none;
    pressed_ = {};
    if (engaged) invalidate(Dirty::paint);
    event.handled = engaged;
    if (hit != pressed || pages_ == nullptr || pressed.position >= (*pages_).count()) return;
    if (pressed.zone == Zone::tab_close) {
        const PageId page = (*pages_).record_at(PagePosition{pressed.position}).id;
        freeze_widths();
        static_cast<void>(request_close(page, PageCloseCause::pointer));
        return;
    }
    if (pressed.zone == Zone::tab_pin) request_pin(pressed.position, false);
}

// A middle press on a tab, released over the same tab, closes it when it is
// closable (owner decision DK-38, interview P17); press and release on
// different tabs do nothing.
void PageStrip::on_middle_button(PointerEvent& event, Hit hit) {
    const bool on_tab = is_tab_zone(hit.zone);
    if (event.action == PointerAction::down) {
        middle_pressed_.reset();
        if (!on_tab || pages_ == nullptr) return;
        middle_pressed_ = (*pages_).record_at(PagePosition{hit.position}).id;
        event.handled = true;
        return;
    }
    if (event.action != PointerAction::up) return;
    const std::optional<PageId> pressed = middle_pressed_;
    middle_pressed_.reset();
    if (!pressed || !on_tab || pages_ == nullptr) return;
    const PageId page = (*pages_).record_at(PagePosition{hit.position}).id;
    if (page != *pressed) return;
    event.handled = true;
    if (!(*pages_).record_at(PagePosition{hit.position}).closable) return;
    freeze_widths();
    static_cast<void>(request_close(page, PageCloseCause::middle_button));
}

void PageStrip::on_pointer(PointerEvent& event) {
    if (event.action == PointerAction::leave) {
        set_hot({});
        middle_pressed_.reset();
        thaw_widths();
        return;
    }
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        const bool dragging = drag_.phase == DragState::Phase::armed || drag_.phase == DragState::Phase::sliding;
        if (dragging) on_drag_move(event.position);
        if (!is_alive()) return;
        set_hot(hit_at(event.position));
        return;
    }
    const Hit hit = hit_at(event.position);
    if (event.action == PointerAction::wheel) {
        // The wheel scrolls an overflowing strip (owner decision DK-37):
        // turned up or left towards the start.
        if (overflow_ != PageStripOverflow::scroll || !layout().overflow) return;
        if (event.wheel_delta.y == 0.0 && event.wheel_delta.x == 0.0) return;
        // A vertical turn decides when there is one; else a turn to the
        // right scrolls towards the end and to the left towards the start.
        int direction = event.wheel_delta.y > 0.0 ? -1 : 1;
        if (event.wheel_delta.y == 0.0) direction = event.wheel_delta.x > 0.0 ? 1 : -1;
        static_cast<void>(scroll_by(direction));
        event.handled = true;
        return;
    }
    if (event.button == PointerButton::middle) {
        on_middle_button(event, hit);
        return;
    }
    if (event.button == PointerButton::secondary) {
        on_secondary_button(event, hit);
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::up) {
        on_pointer_release(event, hit);
        return;
    }
    if (event.action != PointerAction::down || hit.zone == Zone::none) return;
    const Control::Ptr keep = shared_from_this();
    pressed_ = hit;
    invalidate(Dirty::paint);
    event.handled = true;
    if (hit.zone == Zone::scroll_back || hit.zone == Zone::scroll_forward) {
        static_cast<void>(scroll_by(hit.zone == Zone::scroll_back ? -1 : 1));
        return;
    }
    if (hit.zone == Zone::list_button) {
        pressed_ = {};
        if (list_open_) {
            close_page_list();
        } else {
            static_cast<void>(show_page_list(MenuOpenMode::pointer));
        }
        return;
    }
    // A close button or pin acts on its release.
    if (hit.zone == Zone::tab_close || hit.zone == Zone::tab_pin) return;
    // A tab: the selection first, so a view behind the strip can remember
    // the focus its old page held, then the focus; the press arms a drag.
    const PageId page = (*pages_).record_at(PagePosition{hit.position}).id;
    static_cast<void>(choose(hit.position, PageSelectionCause::pointer));
    if (!is_alive()) return;
    if (window() != nullptr) static_cast<void>((*window()).request_focus(shared_from_this()));
    if (!is_alive() || pages_ == nullptr) return;
    const std::optional<PagePosition> found = (*pages_).find(page);
    if (found) arm_drag((*found).value, event.position);
    if (event.click_count == 2U && found) {
        const PageDoubleClick doubled{page};
        page_double_clicked_.emit(doubled);
    }
}

void PageStrip::on_key(KeyEvent& event) {
    if (!focused_ || event.action != KeyAction::down || pages_ == nullptr || (*pages_).count() == 0U) return;
    const std::size_t count = (*pages_).count();
    // The named commands' keys are the window's (owner decision DK-30): the
    // host's defaults, as the application rebound them.
    const KeyMap& keys = effective_key_map();
    const bool plain = event.modifiers == Modifier::none;
    // Along the row: Left and Right on a top or bottom strip, Up and Down on
    // a side strip.
    const std::uint32_t previous_key = horizontal() ? PhysicalKey::left : PhysicalKey::up;
    const std::uint32_t next_key = horizontal() ? PhysicalKey::right : PhysicalKey::down;
    const std::size_t current = keyboard_position().value_or(0U);
    if (plain && event.physical_key == PhysicalKey::escape) {
        // Escape ends a live reorder with nothing moved; otherwise it is not
        // the strip's.
        if (!cancel_drag()) return;
    } else if (keys.matches(KeyCommand::context_menu, event)) {
        // Shift+F10 or the context-menu key asks for the keyboard's tab's
        // context menu.
        const Point anchor = keyboard_menu_anchor(current);
        request_context_menu(current, anchor, PageContextMenuCause::keyboard);
    } else if (keys.matches(KeyCommand::close_document, event)) {
        // Ctrl+F4 (Cmd+W on macOS) closes the selected closable page (owner
        // decision DK-38); Delete closes nothing.
        const std::optional<PageId> selected = (*pages_).selected_page();
        if (selected) static_cast<void>(request_close(*selected, PageCloseCause::keyboard));
    } else if (keys.matches(KeyCommand::move_tab_later, event)) {
        static_cast<void>(move_selected(1));
    } else if (keys.matches(KeyCommand::move_tab_earlier, event)) {
        static_cast<void>(move_selected(-1));
    } else if (keys.matches(KeyCommand::next_tab, event)) {
        static_cast<void>(select_relative(1, PageSelectionCause::keyboard));
    } else if (keys.matches(KeyCommand::previous_tab, event)) {
        static_cast<void>(select_relative(-1, PageSelectionCause::keyboard));
    } else if (plain && event.physical_key == next_key) {
        move_keyboard((current + 1U) % count);
    } else if (plain && event.physical_key == previous_key) {
        move_keyboard((current + count - 1U) % count);
    } else if (plain && event.physical_key == PhysicalKey::home) {
        move_keyboard(0U);
    } else if (plain && event.physical_key == PhysicalKey::end) {
        move_keyboard(count - 1U);
    } else if (plain && (event.physical_key == PhysicalKey::space || event.physical_key == PhysicalKey::enter)) {
        static_cast<void>(choose(current, PageSelectionCause::keyboard));
    } else {
        return;
    }
    event.handled = true;
}

void PageStrip::on_focus_changed(bool focused) {
    focused_ = focused;
    if (!focused_) static_cast<void>(cancel_drag());
    keyboard_page_.reset();
    if (focused_ && pages_ != nullptr) keyboard_page_ = (*pages_).selected_page();
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- mnemonics -----------------------------------------------------------------

bool PageStrip::mnemonic_matches(char32_t character) const noexcept {
    if (!mnemonics_enabled_ || pages_ == nullptr) return false;
    const std::size_t count = (*pages_).count();
    for (std::size_t position = 0; position < count; ++position) {
        if (is_mnemonic(character, (*pages_).record_at(PagePosition{position}).title)) return true;
    }
    return false;
}

// A mnemonic selects its page and focuses the strip on its tab.
bool PageStrip::process_mnemonic_self(char32_t character) {
    if (!mnemonics_enabled_ || pages_ == nullptr) return false;
    const std::size_t count = (*pages_).count();
    for (std::size_t position = 0; position < count; ++position) {
        if (!is_mnemonic(character, (*pages_).record_at(PagePosition{position}).title)) continue;
        const Control::Ptr keep = shared_from_this();
        static_cast<void>(choose(position, PageSelectionCause::keyboard));
        if (is_alive() && window() != nullptr) static_cast<void>((*window()).request_focus(keep));
        return true;
    }
    return false;
}

// -- page list -----------------------------------------------------------------

void PageStrip::prepare_list_commands() {
    const std::size_t count = (*pages_).count();
    const std::string prefix = std::string(stable_id().value()) + ".list.";
    while (list_commands_.size() < count) {
        const std::string identity = prefix + std::to_string(list_commands_.size());
        list_commands_.push_back(std::make_shared<Command>(identity, std::string{}));
    }
    const std::optional<std::size_t> selected = selected_position();
    for (std::size_t position = 0; position < count; ++position) {
        const PageRecord& record = (*pages_).record_at(PagePosition{position});
        Command& command = *list_commands_[position];
        command.set_text(mnemonics_enabled_ ? record.title : literal_menu_text(record.title));
        command.set_checked(selected && *selected == position);
        command.set_enabled(true);
    }
}

// Every page has a row, in order; a page whose tab is not laid out is set in
// italic (owner decision DK-42, PT6). Italic is a font style, not an atom:
// the row is marked through MenuItemSpec::italic.
bool PageStrip::show_page_list(MenuOpenMode mode) {
    require_mutable();
    if (mode != MenuOpenMode::keyboard && mode != MenuOpenMode::pointer) {
        throw std::invalid_argument("PageStrip opening mode is invalid");
    }
    if (pages_ == nullptr || attached_window() == nullptr || !effectively_enabled()) return false;
    const Layout& placed = layout();
    if (!placed.overflow) return false;
    if (list_open_ && (*list_).is_open()) return true;
    prepare_list_commands();
    const std::size_t count = (*pages_).count();
    std::vector<MenuItemSpec> rows{};
    rows.reserve(count);
    for (std::size_t position = 0; position < count; ++position) {
        const PageRecord& record = (*pages_).record_at(PagePosition{position});
        const std::string text = (*list_commands_[position]).state().text;
        rows.emplace_back(record.id.value, MenuItemKind::radio, list_commands_[position], text);
        rows.back().italic = placed.tabs[position].bounds.empty();
    }
    (*list_).set_items(std::move(rows));
    const Rect absolute = absolute_bounds();
    const Rect button = placed.list_button;
    const double right = absolute.x + button.x + button.width;
    // Right-aligned below the list button (beside it on a side strip, on the
    // content's side); the overlay layer keeps it on screen.
    Point anchor{std::max(absolute.x, right - (*list_).preferred_width()), absolute.y + button.y + button.height};
    if (placement_ == PageStripPlacement::left) anchor = {right, absolute.y + button.y};
    if (placement_ == PageStripPlacement::right) {
        anchor = {absolute.x + button.x - (*list_).preferred_width(), absolute.y + button.y};
    }
    (*list_).show(shared_from_this(), anchor, mode);
    list_open_ = true;
    invalidate(Dirty::paint | Dirty::semantics);
    return true;
}

void PageStrip::close_page_list() noexcept {
    if (list_) (*list_).close();
}

bool PageStrip::page_list_open() const noexcept {
    const bool result = list_ && (*list_).is_open();
    return result;
}

std::vector<MenuItemSpec> PageStrip::page_list_items() const {
    if (!page_list_open()) return {};
    const std::vector<MenuItemSpec> rows = (*list_).items();
    return rows;
}

void PageStrip::on_list_open_changed(bool open) {
    list_open_ = open;
    invalidate(Dirty::paint | Dirty::semantics);
}

void PageStrip::on_list_item_invoked(const MenuItemInvocation& invocation) {
    if (pages_ == nullptr) return;
    const PageId page{invocation.item_id};
    const std::optional<PagePosition> found = (*pages_).find(page);
    if (!found) return;
    static_cast<void>(choose_from_list((*found).value));
}

// -- semantics -----------------------------------------------------------------

std::string PageStrip::tab_identity(const PageId& page) const {
    std::string identity = std::string(stable_id().value()) + ".tab." + page.value;
    return identity;
}

std::string PageStrip::page_panel_identity(const PageId&) const {
    return {};
}

std::optional<std::size_t> PageStrip::position_of_tab_identity(std::string_view identity) const {
    if (pages_ == nullptr) return std::nullopt;
    const std::string prefix = std::string(stable_id().value()) + ".tab.";
    if (identity.size() <= prefix.size() || identity.substr(0U, prefix.size()) != prefix) return std::nullopt;
    const PageId page{std::string(identity.substr(prefix.size()))};
    const std::optional<PagePosition> found = (*pages_).find(page);
    if (!found) return std::nullopt;
    const std::size_t position = (*found).value;
    return position;
}

SemanticDescriptor PageStrip::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::tab_group;
    descriptor.name = accessible_name.read().empty() ? "Pages" : accessible_name.read();
    descriptor.description = accessible_description.read();
    const std::optional<std::size_t> selected = selected_position();
    if (selected) descriptor.value = layout().tabs[*selected].label;
    descriptor.orientation = horizontal() ? SemanticOrientation::horizontal : SemanticOrientation::vertical;
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

// Every page is a tab, laid out or not (then without the visible state and
// bounds), so assistive technology reaches each one; the scroll and list
// buttons follow while the strip overflows. Until stage A gives `pinned` and
// `modified` an accessible state, a tab's description names them (PageView
// study §5.6).
std::vector<SemanticNode> PageStrip::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    if (pages_ == nullptr) return nodes;
    const Layout& placed = layout();
    const Rect absolute = absolute_bounds();
    const std::size_t count = placed.tabs.size();
    const bool enabled = effectively_enabled();
    const SemanticState enabled_state = enabled ? SemanticState::enabled : SemanticState::none;
    const std::optional<std::size_t> selected = selected_position();
    const std::optional<std::size_t> keyboard = keyboard_position();
    nodes.reserve(count + 3U);
    for (std::size_t position = 0; position < count; ++position) {
        const PageRecord& record = (*pages_).record_at(PagePosition{position});
        const Rect bounds = placed.tabs[position].bounds;
        SemanticNode node{};
        node.stable_id = tab_identity(record.id);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::tab;
        node.name = placed.tabs[position].label;
        if (mnemonics_enabled_) node.access_key = mnemonic_access_key(record.title);
        if (record.pinned) node.description = "pinned";
        if (record.pinned && record.modified) node.description += ", ";
        if (record.modified) node.description += "modified";
        node.position_in_set = position + 1U;
        node.set_size = count;
        node.controlled_panel = page_panel_identity(record.id);
        node.states = SemanticState::focusable | enabled_state;
        if (!bounds.empty()) {
            node.bounds = to_window(absolute, bounds);
            node.states |= SemanticState::visible;
        }
        if (selected && *selected == position) node.states |= SemanticState::selected;
        if (keyboard && *keyboard == position) node.states |= SemanticState::focused;
        // Tabs publish select and, where closable, close; never press
        // (owner decision DK-35).
        node.actions = {SemanticAction::focus, SemanticAction::select};
        if (record.closable) node.actions.push_back(SemanticAction::close);
        nodes.push_back(std::move(node));
    }
    if (!placed.overflow) return nodes;
    const std::string strip_id(stable_id().value());
    if (overflow_ == PageStripOverflow::scroll) {
        const std::array<Rect, 2> scroll_bounds{placed.scroll_back, placed.scroll_forward};
        const std::array<const char*, 2> scroll_ids{".scroll-back", ".scroll-forward"};
        const std::array<const char*, 2> scroll_names{"Scroll tabs back", "Scroll tabs forward"};
        const std::array<bool, 2> scroll_possible{can_scroll_backward(), can_scroll_forward()};
        for (std::size_t index = 0; index < scroll_bounds.size(); ++index) {
            SemanticNode button{};
            button.stable_id = strip_id + scroll_ids[index];
            button.runtime_id = virtual_semantic_runtime_id(button.stable_id);
            button.role = SemanticRole::button;
            button.name = scroll_names[index];
            button.bounds = to_window(absolute, scroll_bounds[index]);
            button.states = SemanticState::visible;
            if (enabled && scroll_possible[index]) button.states |= SemanticState::enabled;
            button.actions = {SemanticAction::press};
            nodes.push_back(std::move(button));
        }
    }
    SemanticNode list{};
    list.stable_id = strip_id + ".list-button";
    list.runtime_id = virtual_semantic_runtime_id(list.stable_id);
    list.role = SemanticRole::menu_button;
    list.name = "All pages";
    if (overflow_ == PageStripOverflow::shrink_then_list) list.description = placed.list_count_text + " hidden";
    list.has_popup = true;
    list.bounds = to_window(absolute, placed.list_button);
    list.states = SemanticState::visible;
    if (enabled) list.states |= SemanticState::enabled;
    if (list_open_) list.states |= SemanticState::expanded;
    list.actions = {SemanticAction::press, SemanticAction::show_menu, SemanticAction::collapse};
    nodes.push_back(std::move(list));
    return nodes;
}

bool PageStrip::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    if (pages_ == nullptr) return false;
    const std::string strip_id(stable_id().value());
    if (id == strip_id + ".scroll-back" || id == strip_id + ".scroll-forward") {
        if (action != SemanticAction::press) return false;
        const bool scrolled = scroll_by(id == strip_id + ".scroll-back" ? -1 : 1);
        return scrolled;
    }
    if (id == strip_id + ".list-button") {
        if (action == SemanticAction::collapse) {
            if (!list_open_) return false;
            close_page_list();
            return true;
        }
        if (action != SemanticAction::press && action != SemanticAction::show_menu) return false;
        const bool opened = show_page_list(MenuOpenMode::keyboard);
        return opened;
    }
    const std::optional<std::size_t> position = position_of_tab_identity(id);
    if (!position) return false;
    const Control::Ptr keep = shared_from_this();
    if (action == SemanticAction::focus) {
        if (window() != nullptr) static_cast<void>((*window()).request_focus(keep));
        if (!is_alive() || pages_ == nullptr || !focused_) return false;
        if (activation_ == PageStripActivation::automatic) {
            static_cast<void>(choose(*position, PageSelectionCause::semantic));
        } else {
            move_keyboard(*position);
        }
        return true;
    }
    // A press sent anyway still selects, as TabControl's tabs do.
    if (action == SemanticAction::select || action == SemanticAction::press) {
        const bool chosen = choose(*position, PageSelectionCause::semantic);
        if (chosen && is_alive() && pages_ != nullptr) reveal(*position);
        return chosen;
    }
    if (action == SemanticAction::close) {
        const PageId page = (*pages_).record_at(PagePosition{*position}).id;
        const bool handled = request_close(page, PageCloseCause::semantic);
        return handled;
    }
    return false;
}

// -- anatomy -------------------------------------------------------------------

namespace {

constexpr std::array<AnatomyState, 3> scroll_button_states{
    {AnatomyState::hot, AnatomyState::pressed, AnatomyState::disabled}};
constexpr std::array<AnatomyState, 3> list_button_states{
    {AnatomyState::hot, AnatomyState::pressed, AnatomyState::open}};
constexpr std::array<AnatomyState, 2> close_button_states{{AnatomyState::hot, AnatomyState::pressed}};
constexpr std::array<AnatomyState, 3> pin_button_states{
    {AnatomyState::hot, AnatomyState::pressed, AnatomyState::checked}};

constexpr std::array<AnatomyState, 1> insertion_mark_states{{AnatomyState::drop_accepted}};

constexpr std::array<PartField<PageStrip::Parts>, 11> page_strip_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &PageStrip::Parts::region},
    {.declaration = {.name = "rail", .role = PartRole::face, .composed_from = {}},
     .member = &PageStrip::Parts::rail},
    {.declaration = {.name = "scroll_back", .role = PartRole::actuator, .composed_from = "button",
                     .states = scroll_button_states},
     .member = &PageStrip::Parts::scroll_back},
    {.declaration = {.name = "scroll_back_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = scroll_button_states},
     .member = &PageStrip::Parts::scroll_back_glyph},
    {.declaration = {.name = "scroll_forward", .role = PartRole::actuator, .composed_from = "button",
                     .states = scroll_button_states},
     .member = &PageStrip::Parts::scroll_forward},
    {.declaration = {.name = "scroll_forward_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = scroll_button_states},
     .member = &PageStrip::Parts::scroll_forward_glyph},
    {.declaration = {.name = "list_button", .role = PartRole::actuator, .composed_from = "button",
                     .states = list_button_states},
     .member = &PageStrip::Parts::list_button},
    {.declaration = {.name = "list_count", .role = PartRole::label, .composed_from = {},
                     .states = list_button_states},
     .member = &PageStrip::Parts::list_count},
    {.declaration = {.name = "list_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = list_button_states},
     .member = &PageStrip::Parts::list_glyph},
    {.declaration = {.name = "insertion_mark", .role = PartRole::drop, .composed_from = {},
                     .states = insertion_mark_states},
     .member = &PageStrip::Parts::insertion_mark},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &PageStrip::Parts::focus},
}};

constexpr std::array<PartField<PageStrip::ItemParts>, 10> page_strip_item_part_fields{{
    {.declaration = {.name = "tab", .role = PartRole::face, .composed_from = {}},
     .member = &PageStrip::ItemParts::tab},
    {.declaration = {.name = "tab_icon", .role = PartRole::icon, .composed_from = {}},
     .member = &PageStrip::ItemParts::tab_icon},
    {.declaration = {.name = "tab_label", .role = PartRole::label, .composed_from = {}},
     .member = &PageStrip::ItemParts::tab_label},
    // The modified dot: a glyph drawn with the radio_dot mark, the nearest
    // admitted mark (DK-35 admitted the state `modified`, no mark).
    {.declaration = {.name = "tab_modified", .role = PartRole::glyph, .composed_from = {}},
     .member = &PageStrip::ItemParts::tab_modified},
    {.declaration = {.name = "tab_close", .role = PartRole::actuator, .composed_from = "button",
                     .states = close_button_states},
     .member = &PageStrip::ItemParts::tab_close},
    {.declaration = {.name = "tab_close_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = close_button_states},
     .member = &PageStrip::ItemParts::tab_close_glyph},
    {.declaration = {.name = "tab_pin", .role = PartRole::actuator, .composed_from = "button",
                     .states = pin_button_states},
     .member = &PageStrip::ItemParts::tab_pin},
    {.declaration = {.name = "tab_pin_glyph", .role = PartRole::glyph, .composed_from = {},
                     .states = pin_button_states},
     .member = &PageStrip::ItemParts::tab_pin_glyph},
    {.declaration = {.name = "tab_seam", .role = PartRole::seam, .composed_from = {}},
     .member = &PageStrip::ItemParts::tab_seam},
    {.declaration = {.name = "tab_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &PageStrip::ItemParts::tab_focus},
}};

constexpr std::array<StateField<PageStrip::ItemStates>, 6> page_strip_item_state_fields{{
    {.state = AnatomyState::hot, .member = &PageStrip::ItemStates::hot},
    {.state = AnatomyState::pressed, .member = &PageStrip::ItemStates::pressed},
    {.state = AnatomyState::captured, .member = &PageStrip::ItemStates::captured},
    {.state = AnatomyState::selected, .member = &PageStrip::ItemStates::selected},
    {.state = AnatomyState::disabled, .member = &PageStrip::ItemStates::disabled},
    {.state = AnatomyState::focus_visible, .member = &PageStrip::ItemStates::focus_visible},
}};

constexpr std::array<PartDeclaration, 11> page_strip_parts = part_declarations(page_strip_part_fields);
constexpr std::array<PartDeclaration, 10> page_strip_item_parts = part_declarations(page_strip_item_part_fields);
constexpr std::array<AnatomyState, 6> page_strip_item_states = state_declarations(page_strip_item_state_fields);
// The admitted custom states (DK-35, DK-40), in the order of pinned_state and
// modified_state.
constexpr std::array<CustomStateDeclaration, 2> page_strip_item_custom_states{
    {pinned_custom_state, modified_custom_state}};
constexpr std::array<AnatomyState, 5> page_strip_states{
    AnatomyState::normal, AnatomyState::enabled, AnatomyState::disabled,
    AnatomyState::focused, AnatomyState::focus_visible,
};

constexpr std::array<ItemSystemDeclaration, 1> page_strip_item_systems{{
    {.name = "page", .parts = page_strip_item_parts, .states = page_strip_item_states,
     .custom_states = page_strip_item_custom_states},
}};

constexpr ControlAnatomy page_strip_anatomy{
    .control_id = "page_strip",
    .parts = page_strip_parts,
    .states = page_strip_states,
    .custom_states = {},
    .item_systems = page_strip_item_systems,
};

} // namespace

const ControlAnatomy& PageStrip::anatomy() const noexcept {
    return page_strip_anatomy;
}

void PageStrip::publish_part_geometry(std::span<PartGeometry> geometry) const {
    const Parts record = strip_parts();
    write_parts(anatomy(), page_strip_part_fields, record, geometry);
}

PageStrip::Parts PageStrip::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record{};
    read_parts(anatomy(), page_strip_part_fields, published.parts(), record);
    return record;
}

std::size_t PageStrip::realized_item_count() const {
    const std::size_t count = layout().visible.size();
    return count;
}

std::string PageStrip::realized_item_stable_id(std::size_t item) const {
    const std::size_t position = layout().visible[item];
    const std::string identity = tab_identity((*pages_).record_at(PagePosition{position}).id);
    return identity;
}

void PageStrip::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const std::size_t position = layout().visible[item];
    const ItemParts record = tab_parts(position);
    write_item_parts(anatomy().item_systems.front(), page_strip_item_part_fields, record, geometry);
}

AnatomyStateSet PageStrip::realized_item_anatomy_states(std::size_t item) const {
    const std::size_t position = layout().visible[item];
    const AnatomyStateSet states = tab_states(position);
    return states;
}

std::vector<PageStrip::Item> PageStrip::realized_items() const {
    const std::size_t count = realized_item_count();
    std::vector<Item> records{};
    records.reserve(count);
    for (std::size_t item = 0; item < count; ++item) {
        const PartGeometryBuffer published = published_realized_item_geometry(item);
        const std::size_t position = layout().visible[item];
        Item record{};
        record.position = PagePosition{position};
        record.page = (*pages_).record_at(record.position).id;
        record.stable_id = realized_item_stable_id(item);
        read_item_parts(anatomy().item_systems.front(), page_strip_item_part_fields, published.parts(),
                        record.parts);
        const AnatomyStateSet states = realized_item_anatomy_states(item);
        read_states(page_strip_item_state_fields, states, record.states);
        record.states.pinned = states.contains(pinned_state);
        record.states.modified = states.contains(modified_state);
        records.push_back(record);
    }
    return records;
}

namespace {

// The events PageStrip adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 7> page_strip_interactions{{
    {.name = "draw_item_content", .payload = "ItemContentPaint", .mutable_payload = true},
    {.name = "page_invoked", .payload = "PageInvocation", .mutable_payload = false},
    {.name = "page_close_requested", .payload = "PageCloseRequest", .mutable_payload = true},
    {.name = "page_pin_requested", .payload = "PagePinRequest", .mutable_payload = true},
    {.name = "page_drag_started", .payload = "PageDragStart", .mutable_payload = true},
    {.name = "page_context_menu_requested", .payload = "PageContextMenuRequest", .mutable_payload = true},
    {.name = "page_double_clicked", .payload = "PageDoubleClick", .mutable_payload = false},
}};

} // namespace

void PageStrip::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), page_strip_interactions.begin(), page_strip_interactions.end());
}

} // namespace gui_forms
