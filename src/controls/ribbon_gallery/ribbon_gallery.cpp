#include "gui_forms/controls/ribbon_gallery/ribbon_gallery.hpp"

#include "../scrollable_control/scrolling_utilities.hpp"

#include "gui_forms/delegate.hpp"
#include "gui_forms/text.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace gui_forms {
namespace {

constexpr std::size_t no_category = std::numeric_limits<std::size_t>::max();
constexpr std::size_t maximum_columns_bound = 32U;
constexpr std::size_t maximum_visible_rows = 8U;

bool valid_text(std::string_view text) {
    const bool result = validate_utf8(text).valid();
    return result;
}

// The source ID previews and commits carry: the gallery's stable ID.
std::string source_of(const Control& control) {
    std::string source(control.stable_id().value());
    return source;
}

} // namespace

// Fires once when a dwell ends; the gallery checks the item is still the one
// the pointer or keyboard rests on.
struct RibbonGallery::DwellCallback final {
    std::weak_ptr<Control> target;
    std::string item_id;

    void operator()(FrameTime) const {
        const std::shared_ptr<Control> control = target.lock();
        if (!control || !(*control).is_alive()) return;
        RibbonGallery& gallery = static_cast<RibbonGallery&>(*control);
        gallery.dwell_elapsed(item_id);
    }
};

RibbonGallery::RibbonGallery(StableId stable_id, RibbonGalleryForm form)
    : Control(std::move(stable_id)), form_(form) {
    if (form_ != RibbonGalleryForm::in_ribbon && form_ != RibbonGalleryForm::drop_down) {
        throw std::invalid_argument("RibbonGallery form is invalid");
    }
    set_focusable(true);
    cursor = CursorKind::arrow;
    set_paint_plane(form_ == RibbonGalleryForm::drop_down ? PaintPlane::overlay : PaintPlane::control);
}

RibbonGallery::~RibbonGallery() {
    dwell_timer_.disconnect();
    overlay_revoked_.disconnect();
    overlay_.disconnect();
}

void RibbonGallery::on_dispose() noexcept {
    stop_dwell();
    if (command_ && (*command_).is_alive() && (*command_).previewing()) {
        try {
            (*command_).cancel_preview(source_of(*this));
        } catch (...) {
        }
    }
    overlay_revoked_.disconnect();
    overlay_.disconnect();
    overlay_open_ = false;
    command_value_.disconnect();
    command_state_.disconnect();
    more_open_changed_.disconnect();
    if (more_gallery_) (*more_gallery_).dispose();
    release_layer_content();
    if (layer_) (*layer_).dispose();
    // The layer's content was this gallery: dropping the layer ends the
    // pair even if its content could not be cleared above.
    layer_.reset();
    Control::on_dispose();
}

// -- model -------------------------------------------------------------------

void RibbonGallery::set_items(std::vector<RibbonGalleryCategory> categories, std::vector<RibbonGalleryItem> items) {
    require_mutable();
    std::unordered_map<std::string, std::size_t> category_index{};
    for (std::size_t index = 0; index < categories.size(); ++index) {
        const RibbonGalleryCategory& category = categories[index];
        if (!valid_stable_id(category.stable_id) || !valid_text(category.caption) ||
            !category_index.emplace(category.stable_id, index).second) {
            throw std::invalid_argument(
                "RibbonGallery categories require unique IDs of '.'-separated lowercase dash segments and valid "
                "UTF-8");
        }
    }
    std::unordered_map<std::string, std::size_t> item_ids{};
    std::vector<std::size_t> item_category(items.size(), no_category);
    for (std::size_t index = 0; index < items.size(); ++index) {
        const RibbonGalleryItem& item = items[index];
        if (!valid_stable_id(item.stable_id) || !valid_text(item.label) ||
            !item_ids.emplace(item.stable_id, index).second) {
            throw std::invalid_argument(
                "RibbonGallery items require unique IDs of '.'-separated lowercase dash segments and valid UTF-8");
        }
        if (item.category.empty()) continue;
        const std::unordered_map<std::string, std::size_t>::const_iterator found = category_index.find(item.category);
        if (found == category_index.end()) {
            throw std::invalid_argument("RibbonGallery item names an unknown category");
        }
        item_category[index] = (*found).second;
    }
    if (overlay_open_) close_overlay();
    cancel_preview();
    // Display order: uncategorized items first, then category by category.
    std::vector<std::size_t> order{};
    order.reserve(items.size());
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (item_category[index] == no_category) order.push_back(index);
    }
    for (std::size_t category = 0; category < categories.size(); ++category) {
        for (std::size_t index = 0; index < items.size(); ++index) {
            if (item_category[index] == category) order.push_back(index);
        }
    }
    categories_ = std::move(categories);
    items_ = std::move(items);
    order_ = std::move(order);
    hot_.reset();
    pressed_.reset();
    focused_.reset();
    first_line_ = 0U;
    if (own_selection_ && !position_of(*own_selection_)) own_selection_.reset();
    model_changed();
    if (more_gallery_) (*more_gallery_).set_items(categories_, items_);
}

void RibbonGallery::model_changed() {
    rebuild_lines();
    invalidate(Dirty::measure | Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

std::optional<std::size_t> RibbonGallery::position_of(std::string_view item_id) const noexcept {
    for (std::size_t position = 0; position < order_.size(); ++position) {
        if (items_[order_[position]].stable_id == item_id) return position;
    }
    return std::nullopt;
}

bool RibbonGallery::item_enabled(std::size_t position) const noexcept {
    if (position >= order_.size() || !effectively_enabled()) return false;
    if (command_ && !(*command_).state().enabled) return false;
    const bool result = items_[order_[position]].enabled;
    return result;
}

void RibbonGallery::set_command(std::shared_ptr<GalleryCommand> command) {
    require_mutable();
    if (command_ == command) return;
    cancel_preview();
    command_value_.disconnect();
    command_state_.disconnect();
    command_ = std::move(command);
    if (command_) {
        command_value_ = (*command_).value_changed().subscribe(
            Delegate<const std::optional<std::string>&>::bind<RibbonGallery, &RibbonGallery::on_command_value>(*this));
        command_state_ = (*(*command_).command()).state_changed().subscribe(
            Delegate<const CommandState&>::bind<RibbonGallery, &RibbonGallery::on_command_state>(*this));
    }
    invalidate(Dirty::paint | Dirty::semantics);
    if (more_gallery_) (*more_gallery_).set_command(command_);
}

std::optional<std::string> RibbonGallery::selected_id() const {
    if (command_) return (*command_).value();
    return own_selection_;
}

void RibbonGallery::set_selected_id(std::optional<std::string> item_id) {
    require_mutable();
    if (item_id && !position_of(*item_id)) throw std::invalid_argument("RibbonGallery has no item with this ID");
    if (command_) {
        (*command_).set_value(std::move(item_id));
        return;
    }
    if (own_selection_ == item_id) return;
    own_selection_ = std::move(item_id);
    if (own_selection_) scroll_into_view(*position_of(*own_selection_));
    invalidate(Dirty::paint | Dirty::semantics);
    selection_changed_.emit(own_selection_);
}

void RibbonGallery::on_command_value(const std::optional<std::string>& value) {
    if (value) {
        const std::optional<std::size_t> position = position_of(*value);
        if (position) scroll_into_view(*position);
    }
    invalidate(Dirty::paint | Dirty::semantics);
    selection_changed_.emit(value);
}

void RibbonGallery::on_command_state(const CommandState& state) {
    if (!state.enabled) cancel_preview();
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- layout settings -------------------------------------------------------------

void RibbonGallery::set_item_size(Size size) {
    require_mutable();
    if (!std::isfinite(size.width) || !std::isfinite(size.height) || size.width < 16.0 || size.height < 16.0 ||
        size.width > 256.0 || size.height > 256.0) {
        throw std::out_of_range("RibbonGallery item size must be between 16 and 256");
    }
    if (item_size_ == size) return;
    item_size_ = size;
    model_changed();
    if (more_gallery_) (*more_gallery_).set_item_size(size);
}

void RibbonGallery::set_show_labels(bool show) {
    require_mutable();
    if (show_labels_ == show) return;
    show_labels_ = show;
    model_changed();
    if (more_gallery_) (*more_gallery_).set_show_labels(show);
}

void RibbonGallery::set_visible_rows(std::size_t rows) {
    require_mutable();
    if (rows == 0U || rows > maximum_visible_rows) throw std::out_of_range("RibbonGallery shows 1 to 8 rows");
    if (visible_rows_ == rows) return;
    visible_rows_ = rows;
    model_changed();
}

void RibbonGallery::set_column_range(std::size_t minimum, std::size_t maximum) {
    require_mutable();
    if (minimum == 0U || maximum > maximum_columns_bound || minimum > maximum) {
        throw std::out_of_range("RibbonGallery column range must lie within 1 to 32");
    }
    minimum_columns_ = minimum;
    maximum_columns_ = maximum;
    columns_ = std::clamp(columns_, minimum_columns_, maximum_columns_);
    model_changed();
}

void RibbonGallery::set_columns(std::size_t columns) {
    require_mutable();
    if (columns < minimum_columns_ || columns > maximum_columns_) {
        throw std::out_of_range("RibbonGallery column count is outside its range");
    }
    if (columns_ == columns) return;
    columns_ = columns;
    model_changed();
    if (focused_) scroll_into_view(*focused_);
}

void RibbonGallery::set_drop_down_size(std::size_t columns, std::size_t rows) {
    require_mutable();
    if (columns == 0U || rows == 0U || columns > maximum_columns_bound || rows > maximum_columns_bound) {
        throw std::out_of_range("RibbonGallery drop-down columns and rows must lie within 1 to 32");
    }
    drop_down_columns_ = columns;
    drop_down_rows_ = rows;
    model_changed();
    if (more_gallery_) (*more_gallery_).set_drop_down_size(columns, rows);
}

void RibbonGallery::scroll_to_line(std::size_t line) {
    require_mutable();
    const std::size_t last = lines_.empty() ? 0U : lines_.size() - 1U;
    const std::size_t next = std::min(line, last);
    if (first_line_ == next) return;
    first_line_ = next;
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

void RibbonGallery::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    // Scale or font changes reach the line heights here.
    rebuild_lines();
    const Geometry layout = geometry();
    // Keep the last page full: the first line is at most the one from which
    // the remaining lines fill the viewport.
    if (!lines_.empty()) {
        const double total = lines_.back().top + lines_.back().height;
        std::size_t last_first = lines_.size() - 1U;
        while (last_first > 0U && total - lines_[last_first - 1U].top <= layout.viewport.height + 0.5) --last_first;
        first_line_ = std::min(first_line_, last_first);
    } else {
        first_line_ = 0U;
    }
}

// -- selection, preview and commit --------------------------------------------------

void RibbonGallery::commit(std::size_t position) {
    stop_dwell();
    if (!item_enabled(position)) return;
    const std::string item_id = items_[order_[position]].stable_id;
    if (command_) {
        const std::shared_ptr<GalleryCommand> command = command_;
        // Through the window's command chain (owner decision DK-47).
        if (window() != nullptr) {
            static_cast<void>((*command).commit_routed(item_id, *window(), source_of(*this)));
        } else {
            static_cast<void>((*command).commit(item_id, source_of(*this)));
        }
    } else if (!(own_selection_ && *own_selection_ == item_id)) {
        own_selection_ = item_id;
        invalidate(Dirty::paint | Dirty::semantics);
        selection_changed_.emit(own_selection_);
    }
    if (is_alive() && overlay_open_) close_overlay();
}

void RibbonGallery::start_dwell(std::size_t position) {
    if (!command_ || !item_enabled(position) || attached_window() == nullptr) {
        stop_dwell();
        return;
    }
    const std::string& item_id = items_[order_[position]].stable_id;
    if (dwell_item_ == item_id && dwell_timer_.connected()) return;
    const std::optional<std::string>& previewing = (*command_).previewing();
    stop_dwell();
    if (previewing && *previewing == item_id) return;
    dwell_item_ = item_id;
    const std::weak_ptr<Control> weak = shared_from_this();
    // A UI timer repeats at its interval; a day's interval makes it fire
    // once, at the dwell's end, and dwell_elapsed disconnects it then.
    dwell_timer_ = (*attached_window()).schedule_ui_timer(
        *this, std::chrono::hours(24), FrameClock::now() + preview_dwell, DwellCallback{weak, item_id});
}

void RibbonGallery::stop_dwell() noexcept {
    dwell_timer_.disconnect();
    dwell_item_.clear();
}

void RibbonGallery::dwell_elapsed(const std::string& item_id) {
    if (dwell_item_ != item_id) return;
    stop_dwell();
    const std::optional<std::size_t> position = position_of(item_id);
    if (!command_ || !position || !item_enabled(*position)) return;
    // The pointer or the keyboard must still rest on the item.
    const bool resting = hot_ == position || (has_focus_ && focused_ == position);
    if (!resting) return;
    const std::shared_ptr<GalleryCommand> command = command_;
    static_cast<void>((*command).preview(item_id, source_of(*this)));
}

void RibbonGallery::cancel_preview() {
    stop_dwell();
    if (!command_ || !(*command_).previewing()) return;
    const std::shared_ptr<GalleryCommand> command = command_;
    (*command).cancel_preview(source_of(*this));
}

// -- focus and keyboard ---------------------------------------------------------------

std::optional<std::string> RibbonGallery::focused_id() const {
    if (!focused_ || *focused_ >= order_.size()) return std::nullopt;
    return items_[order_[*focused_]].stable_id;
}

void RibbonGallery::focus_position(std::size_t position) {
    if (position >= order_.size()) return;
    focused_ = position;
    scroll_into_view(position);
    invalidate(Dirty::paint | Dirty::semantics);
    if (has_focus_) start_dwell(position);
}

void RibbonGallery::on_focus_changed(bool focused) {
    has_focus_ = focused;
    if (focused) {
        if (!focused_ && !order_.empty()) {
            const std::optional<std::string> selected = selected_id();
            const std::optional<std::size_t> position = selected ? position_of(*selected) : std::nullopt;
            focused_ = position ? *position : 0U;
        }
        if (focused_) scroll_into_view(*focused_);
    } else {
        cancel_preview();
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

bool RibbonGallery::navigate(std::uint32_t key) {
    if (order_.empty()) return false;
    const std::size_t current = focused_.value_or(0U);
    const std::size_t last = order_.size() - 1U;
    std::size_t target = current;
    if (key == PhysicalKey::left) {
        target = current == 0U ? 0U : current - 1U;
    } else if (key == PhysicalKey::right) {
        target = std::min(last, current + 1U);
    } else if (key == PhysicalKey::home) {
        target = 0U;
    } else if (key == PhysicalKey::end) {
        target = last;
    } else if (key == PhysicalKey::up || key == PhysicalKey::down || key == PhysicalKey::page_up ||
               key == PhysicalKey::page_down) {
        // Up and Down move one item row, Page Up and Page Down a viewport of
        // lines, keeping the column where the target row has one.
        const std::optional<std::size_t> line = line_of(current);
        if (!line) return false;
        const std::size_t column = current - lines_[*line].first;
        const bool upward = key == PhysicalKey::up || key == PhysicalKey::page_up;
        const bool paging = key == PhysicalKey::page_up || key == PhysicalKey::page_down;
        const std::size_t steps = paging ? std::max<std::size_t>(1U, lines_per_page(geometry())) : 1U;
        std::size_t candidate = *line;
        std::size_t moved = 0U;
        std::size_t found = *line;
        while (moved < steps) {
            if (upward) {
                if (candidate == 0U) break;
                --candidate;
            } else {
                if (candidate + 1U >= lines_.size()) break;
                ++candidate;
            }
            if (lines_[candidate].kind != LineKind::items) continue;
            found = candidate;
            ++moved;
        }
        if (found == *line) {
            // At the edge, paging goes to the first or last item.
            if (paging) target = upward ? 0U : last;
        } else {
            const Line& row = lines_[found];
            target = row.first + std::min(column, row.count - 1U);
        }
    } else {
        return false;
    }
    focus_position(target);
    return true;
}

void RibbonGallery::on_key(KeyEvent& event) {
    if (event.action != KeyAction::down || !has_focus_) return;
    const std::uint32_t key = event.physical_key;
    if (key == PhysicalKey::enter || key == PhysicalKey::space) {
        if (focused_) commit(*focused_);
        event.handled = true;
        return;
    }
    if (key == PhysicalKey::escape) {
        // Escape withdraws a preview first; a second Escape closes an overlay.
        if (command_ && (*command_).previewing()) {
            cancel_preview();
            event.handled = true;
        } else if (overlay_open_) {
            close_overlay();
            event.handled = true;
        }
        return;
    }
    if (navigate(key)) event.handled = true;
}

// -- pointer --------------------------------------------------------------------------

void RibbonGallery::on_pointer(PointerEvent& event) {
    const Point local = point_from_window(event.position);
    if (event.action == PointerAction::leave) {
        if (hot_ || hot_zone_ != HitZone::none) {
            hot_.reset();
            hot_zone_ = HitZone::none;
            invalidate(Dirty::paint);
        }
        // Leaving the gallery withdraws the preview.
        cancel_preview();
        return;
    }
    const HitResult hit = hit_test(local);
    if (event.action == PointerAction::move || event.action == PointerAction::enter) {
        const std::optional<std::size_t> hot =
            hit.zone == HitZone::item && item_enabled(hit.position) ? std::optional<std::size_t>(hit.position)
                                                                    : std::nullopt;
        if (hot != hot_ || hit.zone != hot_zone_) {
            hot_ = hot;
            hot_zone_ = hit.zone;
            invalidate(Dirty::paint);
            if (hot_) {
                start_dwell(*hot_);
            } else if (!(has_focus_ && focused_)) {
                cancel_preview();
            } else {
                stop_dwell();
            }
        }
        return;
    }
    if (event.action == PointerAction::wheel) {
        const double delta = event.wheel_delta.y;
        if (delta == 0.0) return;
        // A delta of up to 8 counts lines (whole lines, at least one); a
        // larger one, such as a pixel delta, scrolls one line.
        const double magnitude = std::abs(delta);
        double counted = 1.0;
        if (magnitude <= 8.0) counted = std::max(1.0, magnitude);
        const std::size_t lines = static_cast<std::size_t>(counted);
        if (delta > 0.0) {
            scroll_to_line(first_line_ > lines ? first_line_ - lines : 0U);
        } else if (can_scroll_down()) {
            scroll_to_line(first_line_ + lines);
        }
        event.handled = true;
        return;
    }
    if (event.button != PointerButton::primary) return;
    if (event.action == PointerAction::down) {
        pressed_zone_ = hit.zone;
        pressed_.reset();
        if (hit.zone == HitZone::item && item_enabled(hit.position)) pressed_ = hit.position;
        if (hit.zone == HitZone::scroll_up && can_scroll_up()) scroll_to_line(first_line_ - 1U);
        if (hit.zone == HitZone::scroll_down && can_scroll_down()) scroll_to_line(first_line_ + 1U);
        if (hit.zone == HitZone::more) static_cast<void>(show_more());
        event.handled = hit.zone != HitZone::none;
        invalidate(Dirty::paint);
        return;
    }
    if (event.action == PointerAction::up) {
        const std::optional<std::size_t> pressed = pressed_;
        pressed_.reset();
        pressed_zone_ = HitZone::none;
        invalidate(Dirty::paint);
        // A click is a press and release on the same item.
        if (pressed && hit.zone == HitZone::item && hit.position == *pressed) commit(*pressed);
        event.handled = pressed.has_value();
    }
}

// -- overlays ----------------------------------------------------------------------------

bool RibbonGallery::open_overlay(const Control::Ptr& anchor) {
    require_mutable();
    if (form_ != RibbonGalleryForm::drop_down || overlay_open_ || !anchor || (*anchor).attached_window() == nullptr) {
        return false;
    }
    Window& window = *(*anchor).attached_window();
    AnchoredOverlayPlacement placement;
    placement.preferred_size = measure({std::numeric_limits<double>::max(), std::numeric_limits<double>::max()});
    placement.horizontal_alignment = OverlayHorizontalAlignment::near;
    placement.vertical_preference = OverlayVerticalPreference::below;
    placement.gap = 2.0;
    if (!layer_) {
        layer_ = make_control<AnchoredOverlayLayer>(StableId(std::string(stable_id().value()) + ".layer"), anchor,
                                                  placement);
        layer_dismissed_ = (*layer_).dismiss_requested().subscribe(
            Delegate<OverlayDismissReason>::bind<RibbonGallery, &RibbonGallery::on_layer_dismissed>(*this));
    } else {
        (*layer_).set_anchor(anchor);
        (*layer_).set_placement(placement);
    }
    // The gallery owns its layer, and is the layer's content only while
    // open (release_layer_content).
    (*layer_).set_content(shared_from_this());
    const Size client = window.client_size();
    (*layer_).set_requested_bounds({0.0, 0.0, client.width, client.height});
    try {
        overlay_ = window.open_overlay(anchor, layer_);
    } catch (...) {
        release_layer_content();
        throw;
    }
    overlay_open_ = true;
    if (Event<>* closed = overlay_.closed_event()) {
        overlay_revoked_ = (*closed).subscribe(Delegate<>::bind<RibbonGallery, &RibbonGallery::on_overlay_revoked>(*this));
    }
    scope_ = window.begin_focus_scope(layer_, shared_from_this());
    invalidate(Dirty::paint | Dirty::semantics);
    overlay_open_changed_.emit(true);
    return true;
}

void RibbonGallery::close_overlay() {
    if (!overlay_open_) return;
    // The preview is withdrawn before the overlay goes.
    cancel_preview();
    overlay_open_ = false;
    Window* const window = attached_window();
    if (window != nullptr && scope_) static_cast<void>((*window).end_focus_scope(scope_));
    scope_ = {};
    overlay_revoked_.disconnect();
    overlay_.disconnect();
    release_layer_content();
    hot_.reset();
    pressed_.reset();
    invalidate(Dirty::paint | Dirty::semantics);
    overlay_open_changed_.emit(false);
}

void RibbonGallery::on_layer_dismissed(OverlayDismissReason) {
    close_overlay();
}

void RibbonGallery::on_overlay_revoked() {
    if (!overlay_open_) return;
    cancel_preview();
    overlay_open_ = false;
    scope_ = {};
    overlay_revoked_.disconnect();
    release_layer_content();
    invalidate(Dirty::paint | Dirty::semantics);
    overlay_open_changed_.emit(false);
}

// The gallery owns its layer (layer_), and while open the layer holds the
// gallery as its content. Closed, the gallery leaves the layer, so the two
// never hold each other and both are freed when the gallery's owner lets
// go (house style §1, §4). A layer already being disposed refuses the
// change; on_dispose then drops the layer instead.
void RibbonGallery::release_layer_content() noexcept {
    if (!layer_ || !(*layer_).content()) return;
    try {
        (*layer_).clear_content();
    } catch (...) {
    }
}

bool RibbonGallery::show_more() {
    require_mutable();
    if (form_ != RibbonGalleryForm::in_ribbon || attached_window() == nullptr) return false;
    if (!more_gallery_) {
        more_gallery_ = make_control<RibbonGallery>(StableId(std::string(stable_id().value()) + ".more"),
                                                    RibbonGalleryForm::drop_down);
        RibbonGallery& more = *more_gallery_;
        more.set_item_size(item_size_);
        more.set_show_labels(show_labels_);
        more.set_drop_down_size(drop_down_columns_, drop_down_rows_);
        more.set_items(categories_, items_);
        more.set_command(command_);
        more.accessible_name = accessible_name.read();
        more_open_changed_ = more.overlay_open_changed().subscribe(
            Delegate<bool>::bind<RibbonGallery, &RibbonGallery::on_more_open_changed>(*this));
    }
    if ((*more_gallery_).overlay_open()) return true;
    if (!command_) (*more_gallery_).own_selection_ = own_selection_;
    // The pointer is on the more button now: the in-ribbon preview ends.
    cancel_preview();
    const bool opened = (*more_gallery_).open_overlay(shared_from_this());
    return opened;
}

void RibbonGallery::on_more_open_changed(bool open) {
    // Without a command the full gallery's choice becomes this gallery's.
    if (!open && !command_ && more_gallery_ && (*more_gallery_).own_selection_ != own_selection_) {
        own_selection_ = (*more_gallery_).own_selection_;
        selection_changed_.emit(own_selection_);
    }
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- semantics -------------------------------------------------------------------------

SemanticDescriptor RibbonGallery::semantic_descriptor() const {
    // The vocabulary has no grid role: a gallery is a list of items.
    SemanticDescriptor descriptor{};
    descriptor.role = SemanticRole::list;
    descriptor.name = accessible_name.read().empty() ? "Gallery" : accessible_name.read();
    descriptor.description = accessible_description.read();
    const std::optional<std::string> selected = selected_id();
    const std::optional<std::size_t> position = selected ? position_of(*selected) : std::nullopt;
    if (position) descriptor.value = items_[order_[*position]].label;
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> RibbonGallery::semantic_virtual_children() const {
    std::vector<SemanticNode> nodes{};
    const Rect absolute = absolute_bounds();
    const Geometry layout = geometry();
    const std::string prefix(stable_id().value());
    const std::optional<std::string> selected = selected_id();
    // Every item is published, with bounds only where it is in view, so the
    // whole collection is readable (RIBBON_STUDY.md §5.2).
    const std::size_t end = visible_line_end(layout);
    std::vector<SemanticNode> groups{};
    const bool grouped = form_ == RibbonGalleryForm::drop_down && !categories_.empty();
    std::size_t group_start = 0U;
    for (std::size_t line = 0; line < lines_.size(); ++line) {
        const Line& entry = lines_[line];
        if (entry.kind == LineKind::caption) {
            SemanticNode group{};
            group.stable_id = prefix + ".category." + categories_[entry.category].stable_id;
            group.runtime_id = virtual_semantic_runtime_id(group.stable_id);
            group.role = SemanticRole::group;
            group.name = categories_[entry.category].caption;
            group.states = SemanticState::visible | SemanticState::enabled;
            groups.push_back(std::move(group));
            group_start = entry.first;
            continue;
        }
        const bool in_view = line >= first_line_ && line < end;
        for (std::size_t offset = 0; offset < entry.count; ++offset) {
            const std::size_t position = entry.first + offset;
            const RibbonGalleryItem& item = items_[order_[position]];
            SemanticNode node{};
            node.stable_id = item.stable_id;
            node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
            node.role = SemanticRole::list_item;
            node.name = item.label.empty() ? item.stable_id : item.label;
            if (in_view) {
                const Rect cell = Rect::intersection(cell_bounds(position, layout), layout.viewport);
                node.bounds = {absolute.x + cell.x, absolute.y + cell.y, cell.width, cell.height};
            }
            node.states = SemanticState::visible | SemanticState::focusable;
            if (item_enabled(position)) node.states |= SemanticState::enabled;
            if (selected && *selected == item.stable_id) node.states |= SemanticState::selected;
            if (has_focus_ && focused_ == position) node.states |= SemanticState::focused;
            node.actions = {SemanticAction::focus, SemanticAction::select, SemanticAction::press};
            if (grouped && !groups.empty()) {
                SemanticNode& group = groups.back();
                node.position_in_set = position - group_start + 1U;
                group.children.push_back(std::move(node));
            } else {
                node.position_in_set = position + 1U;
                nodes.push_back(std::move(node));
            }
        }
    }
    // Set sizes once every item of a set is known.
    for (SemanticNode& node : nodes) node.set_size = nodes.size();
    for (SemanticNode& group : groups) {
        for (SemanticNode& child : group.children) child.set_size = group.children.size();
        nodes.push_back(std::move(group));
    }
    const Parts placed = gallery_parts(layout);
    if (placed.scroll_up.present) {
        SemanticNode up{};
        up.stable_id = prefix + ".scroll-up";
        up.runtime_id = virtual_semantic_runtime_id(up.stable_id);
        up.role = SemanticRole::button;
        up.name = "Scroll up";
        up.bounds = {absolute.x + placed.scroll_up.bounds.x, absolute.y + placed.scroll_up.bounds.y,
                     placed.scroll_up.bounds.width, placed.scroll_up.bounds.height};
        up.states = SemanticState::visible;
        if (can_scroll_up()) up.states |= SemanticState::enabled;
        up.actions = {SemanticAction::press};
        nodes.push_back(std::move(up));
        SemanticNode down{};
        down.stable_id = prefix + ".scroll-down";
        down.runtime_id = virtual_semantic_runtime_id(down.stable_id);
        down.role = SemanticRole::button;
        down.name = "Scroll down";
        down.bounds = {absolute.x + placed.scroll_down.bounds.x, absolute.y + placed.scroll_down.bounds.y,
                       placed.scroll_down.bounds.width, placed.scroll_down.bounds.height};
        down.states = SemanticState::visible;
        if (can_scroll_down()) down.states |= SemanticState::enabled;
        down.actions = {SemanticAction::press};
        nodes.push_back(std::move(down));
    }
    if (placed.more_button.present) {
        SemanticNode more{};
        more.stable_id = prefix + ".more-button";
        more.runtime_id = virtual_semantic_runtime_id(more.stable_id);
        more.role = SemanticRole::menu_button;
        more.name = "More";
        more.has_popup = true;
        more.bounds = {absolute.x + placed.more_button.bounds.x, absolute.y + placed.more_button.bounds.y,
                       placed.more_button.bounds.width, placed.more_button.bounds.height};
        more.states = SemanticState::visible | SemanticState::enabled;
        if (more_gallery_ && (*more_gallery_).overlay_open()) more.states |= SemanticState::expanded;
        more.actions = {SemanticAction::press, SemanticAction::expand};
        nodes.push_back(std::move(more));
    }
    return nodes;
}

bool RibbonGallery::on_semantic_child_action(std::string_view id, SemanticAction action, std::string_view) {
    const std::string prefix(stable_id().value());
    if (id == prefix + ".more-button") {
        if (action != SemanticAction::press && action != SemanticAction::expand) return false;
        const bool opened = show_more();
        return opened;
    }
    if (id == prefix + ".scroll-up" || id == prefix + ".scroll-down") {
        if (action != SemanticAction::press) return false;
        const bool up = id == prefix + ".scroll-up";
        if (up && can_scroll_up()) scroll_to_line(first_line_ - 1U);
        if (!up && can_scroll_down()) scroll_to_line(first_line_ + 1U);
        return true;
    }
    const std::optional<std::size_t> position = position_of(id);
    if (!position) return false;
    if (action == SemanticAction::focus || action == SemanticAction::select) {
        if (attached_window() != nullptr) static_cast<void>((*attached_window()).request_focus(shared_from_this()));
        focus_position(*position);
        if (action == SemanticAction::select) commit(*position);
        return true;
    }
    if (action == SemanticAction::press) {
        if (!item_enabled(*position)) return false;
        commit(*position);
        return true;
    }
    return false;
}

} // namespace gui_forms
