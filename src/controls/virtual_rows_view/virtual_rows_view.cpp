#include "gui_forms/controls/virtual_rows_view.hpp"

#include "../collections/row_height_index.hpp"

#include "gui_forms/controls/v_scroll_bar.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace gui_forms {
namespace {

// Geometry in logical units. The scroll bar's column matches ScrollBar's
// own arrow extent and is always reserved, so the content width (which the
// rows are measured at) never depends on whether the content overflows: a
// bar appearing cannot shrink the rows, re-measure them and hide itself.
constexpr double scroll_bar_extent = 17.0;
// The overscan band, above and below the viewport, as a fraction of the
// viewport's height: rows there are measured before they are shown.
constexpr double overscan_fraction = 0.5;
// A wheel notch in lines scrolls this many estimated rows.
constexpr double wheel_rows_per_line = 3.0;
// The digits of the largest row index, for the "row-<n>" identities.
constexpr std::size_t row_digits_capacity = 24U;
constexpr std::string_view row_segment = ".row-";
constexpr double preferred_extent = 240.0;
// The live region's identity suffix and the announcement's wording.
constexpr std::string_view announcements_segment = ".announcements";
constexpr std::string_view announcement_one_row = " new row, ";
constexpr std::string_view announcement_rows = " new rows, ";
constexpr std::string_view announcement_total = " in all";

[[nodiscard]] std::optional<VirtualRowIndex> as_row_index(std::optional<std::size_t> row) noexcept {
    if (!row.has_value()) return std::nullopt;
    const VirtualRowIndex index{*row};
    return index;
}

constexpr std::array<PartField<VirtualRowsView::Parts>, 4> virtual_rows_view_part_fields{{
    {.declaration = {.name = "region", .role = PartRole::region, .composed_from = {}},
     .member = &VirtualRowsView::Parts::region},
    {.declaration = {.name = "well", .role = PartRole::well, .composed_from = {}},
     .member = &VirtualRowsView::Parts::well},
    // The child scroll bar, a whole scroll bar system; a ranged path is the
    // nearest role to a composed scroll bar (as TreeListView declares it).
    {.declaration = {.name = "vertical_scroll_bar", .role = PartRole::track, .composed_from = "v_scroll_bar"},
     .member = &VirtualRowsView::Parts::vertical_scroll_bar},
    {.declaration = {.name = "focus", .role = PartRole::focus, .composed_from = {}},
     .member = &VirtualRowsView::Parts::focus},
}};

constexpr std::array<StateField<VirtualRowsView::States>, 5> virtual_rows_view_state_fields{{
    {.state = AnatomyState::normal, .member = &VirtualRowsView::States::normal},
    {.state = AnatomyState::enabled, .member = &VirtualRowsView::States::enabled},
    {.state = AnatomyState::disabled, .member = &VirtualRowsView::States::disabled},
    {.state = AnatomyState::focused, .member = &VirtualRowsView::States::focused},
    {.state = AnatomyState::focus_visible, .member = &VirtualRowsView::States::focus_visible},
}};

constexpr std::array<PartField<VirtualRowsView::RowParts>, 3> virtual_rows_view_row_part_fields{{
    {.declaration = {.name = "row", .role = PartRole::region, .composed_from = {}},
     .member = &VirtualRowsView::RowParts::row},
    {.declaration = {.name = "row_face", .role = PartRole::face, .composed_from = {}},
     .member = &VirtualRowsView::RowParts::row_face},
    {.declaration = {.name = "row_focus", .role = PartRole::focus, .composed_from = {}},
     .member = &VirtualRowsView::RowParts::row_focus},
}};

constexpr std::array<StateField<VirtualRowsView::RowStates>, 5> virtual_rows_view_row_state_fields{{
    {.state = AnatomyState::selected, .member = &VirtualRowsView::RowStates::selected},
    {.state = AnatomyState::hot, .member = &VirtualRowsView::RowStates::hot},
    {.state = AnatomyState::current, .member = &VirtualRowsView::RowStates::current},
    {.state = AnatomyState::disabled, .member = &VirtualRowsView::RowStates::disabled},
    {.state = AnatomyState::focus_visible, .member = &VirtualRowsView::RowStates::focus_visible},
}};

constexpr std::array<PartDeclaration, 4> virtual_rows_view_parts = part_declarations(virtual_rows_view_part_fields);
constexpr std::array<AnatomyState, 5> virtual_rows_view_states = state_declarations(virtual_rows_view_state_fields);
constexpr std::array<PartDeclaration, 3> virtual_rows_view_row_parts =
    part_declarations(virtual_rows_view_row_part_fields);
constexpr std::array<AnatomyState, 5> virtual_rows_view_row_states =
    state_declarations(virtual_rows_view_row_state_fields);

constexpr ItemSystemIndex row_system{0U};

constexpr std::array<ItemSystemDeclaration, 1> virtual_rows_view_item_systems{{
    {.name = "row", .parts = virtual_rows_view_row_parts, .states = virtual_rows_view_row_states,
     .custom_states = {}},
}};

constexpr ControlAnatomy virtual_rows_view_anatomy{
    .control_id = "virtual_rows_view",
    .parts = virtual_rows_view_parts,
    .states = virtual_rows_view_states,
    .custom_states = {},
    .item_systems = virtual_rows_view_item_systems,
};

// The events VirtualRowsView adds to those of Control, in declaration order.
constexpr std::array<InteractionDeclaration, 2> virtual_rows_view_interactions{{
    {.name = "selection_changed", .payload = "VirtualRowSelection", .mutable_payload = false},
    {.name = "row_activated", .payload = "VirtualRowIndex", .mutable_payload = false},
}};

} // namespace

// Marks the view as running a target for the scope's life, restoring the
// previous mark on exit (also when the target throws).
class VirtualRowsView::CallbackScope final {
public:
    explicit CallbackScope(const VirtualRowsView& view) noexcept
        : flag_(view.in_callback_), previous_(view.in_callback_) {
        flag_ = true;
    }
    ~CallbackScope() { flag_ = previous_; }
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;

private:
    bool& flag_;
    bool previous_{};
};

VirtualRowsView::VirtualRowsView(StableId stable_id)
    : Control(std::move(stable_id)), heights_(std::make_unique<RowHeightIndex>(default_estimated_row_height)) {
    focusable = true;
    set_paint_plane(PaintPlane::control);
}

VirtualRowsView::~VirtualRowsView() {}

void VirtualRowsView::initialize_control_tree() {
    const std::string prefix(stable_id().value());
    vertical_bar_ = make_control<VScrollBar>(StableId(prefix + ".vertical-scroll-bar"));
    // The bar scrolls with the pointer; the keyboard scrolls the view itself.
    (*vertical_bar_).focusable = false;
    add_child(vertical_bar_);
    vertical_scrolled_ = (*vertical_bar_).value_changed().subscribe(
        *this, Delegate<double>::bind<VirtualRowsView, &VirtualRowsView::on_vertical_scroll>(*this));
    set_child_layout_collapsed(vertical_bar_, true);
}

void VirtualRowsView::require_outside_callback() const {
    if (in_callback_) {
        throw std::logic_error("VirtualRowsView cannot change its rows, callbacks or scroll from a row target");
    }
}

// -- rows -----------------------------------------------------------------------------

void VirtualRowsView::set_row_callbacks(VirtualRowsCallbacks callbacks) {
    require_outside_callback();
    require_mutable();
    callbacks_ = callbacks;
    rows_changed();
}

void VirtualRowsView::reset_rows(std::size_t count) {
    require_outside_callback();
    require_mutable();
    if (count > maximum_rows) throw std::length_error("VirtualRowsView holds at most maximum_rows rows");
    const std::optional<std::size_t> previous = selected_;
    (*heights_).assign(count, (*heights_).estimated_height());
    offset_ = 0.0;
    anchor_row_ = 0U;
    anchor_top_ = 0.0;
    anchored_at_end_ = false;
    pending_appended_ = 0U;
    selected_.reset();
    hot_.reset();
    rows_changed();
    if (previous.has_value()) raise_selection(previous);
}

std::size_t VirtualRowsView::row_count() const noexcept {
    return (*heights_).count();
}

void VirtualRowsView::insert_rows(VirtualRowIndex at, std::size_t count) {
    require_outside_callback();
    require_mutable();
    const std::size_t rows = row_count();
    if (at.value > rows) throw std::out_of_range("VirtualRowsView insertion point past the end");
    if (count > maximum_rows - rows) throw std::length_error("VirtualRowsView holds at most maximum_rows rows");
    if (count == 0U) return;
    (*heights_).insert(at.value, count);
    // Rows appended at the end are counted for the next announcement,
    // saturating rather than wrapping.
    if (announces_appends_ && at.value == rows) {
        const std::size_t room = SIZE_MAX - pending_appended_;
        pending_appended_ = count > room ? SIZE_MAX : pending_appended_ + count;
    }
    if (selected_.has_value() && *selected_ >= at.value) selected_ = *selected_ + count;
    if (hot_.has_value() && *hot_ >= at.value) hot_ = *hot_ + count;
    // Rows inserted at or before the anchor push it down with its content;
    // an end anchor stays the end.
    if (anchored_at_end_) {
        anchor_row_ = row_count();
    } else if (anchor_row_ >= at.value) {
        anchor_row_ = anchor_row_ + count;
    }
    rows_changed();
}

void VirtualRowsView::erase_rows(VirtualRowIndex at, std::size_t count) {
    require_outside_callback();
    require_mutable();
    const std::size_t rows = row_count();
    if (at.value > rows || count > rows - at.value) {
        throw std::out_of_range("VirtualRowsView erased range past the end");
    }
    if (count == 0U) return;
    const std::size_t end = at.value + count;
    const std::optional<std::size_t> previous = selected_;
    bool selection_erased = false;
    (*heights_).erase(at.value, count);
    if (selected_.has_value() && *selected_ >= at.value && *selected_ < end) {
        selected_.reset();
        selection_erased = true;
    } else if (selected_.has_value() && *selected_ >= end) {
        selected_ = *selected_ - count;
    }
    if (hot_.has_value() && *hot_ >= at.value && *hot_ < end) {
        hot_.reset();
    } else if (hot_.has_value() && *hot_ >= end) {
        hot_ = *hot_ - count;
    }
    if (anchored_at_end_) {
        anchor_row_ = row_count();
    } else if (anchor_row_ >= end) {
        anchor_row_ = anchor_row_ - count;
    } else if (anchor_row_ >= at.value) {
        // The anchor was erased: the first row after the erased ones takes
        // its place on screen.
        anchor_row_ = at.value;
    }
    rows_changed();
    if (selection_erased) raise_selection(previous);
}

void VirtualRowsView::refresh_rows(VirtualRowIndex first, std::size_t count) {
    require_outside_callback();
    require_mutable();
    const std::size_t rows = row_count();
    if (first.value > rows || count > rows - first.value) {
        throw std::out_of_range("VirtualRowsView refreshed range past the end");
    }
    if (count == 0U) return;
    // The index has no "unmeasure": the rows are replaced by as many
    // estimated rows, which leaves every position (and so the anchor and
    // the selection) where it was.
    (*heights_).erase(first.value, count);
    (*heights_).insert(first.value, count);
    rows_changed();
}

void VirtualRowsView::set_estimated_row_height(double height) {
    require_outside_callback();
    require_mutable();
    if (!std::isfinite(height) || height < minimum_row_height || height > maximum_row_height) {
        throw std::invalid_argument("VirtualRowsView estimated row height outside its bounds");
    }
    if ((*heights_).estimated_height() == height) return;
    (*heights_).assign(row_count(), height);
    rows_changed();
}

double VirtualRowsView::estimated_row_height() const noexcept {
    return (*heights_).estimated_height();
}

bool VirtualRowsView::row_measured(VirtualRowIndex row) const noexcept {
    if (row.value >= row_count()) return false;
    const bool measured = (*heights_).measured(row.value);
    return measured;
}

// After a change of the rows or their heights: settles at once when laid
// out (so geometry reads are current before the next layout), and asks for
// a layout, a paint and new semantics.
void VirtualRowsView::rows_changed() {
    static_cast<void>(settle());
    update_realized();
    update_scroll_bar();
    invalidate(Dirty::arrange | Dirty::paint | Dirty::hit_test | Dirty::semantics);
}

// -- settle -----------------------------------------------------------------------------

double VirtualRowsView::maximum_offset() const noexcept {
    const double excess = (*heights_).total_height() - viewport_.height;
    const double maximum = std::max(0.0, excess);
    return maximum;
}

double VirtualRowsView::anchored_offset() const noexcept {
    const double maximum = maximum_offset();
    if (anchored_at_end_) return maximum;
    const double top = (*heights_).offset_of(anchor_row_) - anchor_top_;
    const double offset = std::clamp(top, 0.0, maximum);
    return offset;
}

void VirtualRowsView::capture_anchor() noexcept {
    const std::size_t count = row_count();
    const double maximum = maximum_offset();
    // Following the tail is derived from the position each time: on at the
    // end while follow_tail_ is set, off as soon as the offset leaves it.
    anchored_at_end_ = follow_tail_ && offset_ >= maximum;
    if (anchored_at_end_) {
        anchor_row_ = count;
        anchor_top_ = (*heights_).total_height() - offset_;
        return;
    }
    if (count == 0U) {
        anchor_row_ = 0U;
        anchor_top_ = 0.0;
        return;
    }
    std::size_t row = (*heights_).row_at(offset_);
    double top = (*heights_).offset_of(row);
    // The first row whose top is in the viewport; the straddling row when
    // it is the last.
    if (top < offset_ && row + 1U < count) {
        row = row + 1U;
        top = (*heights_).offset_of(row);
    }
    anchor_row_ = row;
    anchor_top_ = top - offset_;
}

bool VirtualRowsView::measure_row(std::size_t row) {
    VirtualRowMeasure request{};
    request.row = VirtualRowIndex{row};
    request.width = viewport_.width;
    request.height = (*heights_).height_of(row);
    if (callbacks_.measure) {
        const CallbackScope scope(*this);
        callbacks_.measure(request);
    }
    ++statistics_.rows_measured;
    if (!std::isfinite(request.height)) return false;
    const double height = std::clamp(request.height, minimum_row_height, maximum_row_height);
    static_cast<void>((*heights_).set_height(row, height));
    return true;
}

std::size_t VirtualRowsView::measure_band(double top, double bottom, std::size_t budget, std::size_t& asked) {
    const std::size_t count = row_count();
    std::size_t row = (*heights_).row_at(std::max(0.0, top));
    std::size_t stored = 0U;
    // Each row is at least minimum_row_height tall, so the walk visits at
    // most (bottom - top) / minimum_row_height + 1 rows.
    while (row < count && asked < budget) {
        const double row_top = (*heights_).offset_of(row);
        if (row_top >= bottom) break;
        if (!(*heights_).measured(row)) {
            ++asked;
            if (measure_row(row)) ++stored;
        }
        ++row;
    }
    return stored;
}

bool VirtualRowsView::settle() {
    const double width = viewport_.width;
    const double height = viewport_.height;
    const std::size_t count = row_count();
    // A new content width forgets every measured height; the anchor keeps
    // its row and its place, and the rows around it are measured again.
    if (width != measured_width_) {
        if (measured_width_ >= 0.0 && count > 0U) {
            (*heights_).assign(count, (*heights_).estimated_height());
        }
        measured_width_ = width;
    }
    std::size_t asked = 0U;
    std::size_t passes = 0U;
    bool complete = true;
    if (width > 0.0 && height > 0.0 && count > 0U) {
        const double overscan = height * overscan_fraction;
        complete = false;
        while (passes < maximum_settle_passes && asked < maximum_measured_per_settle) {
            ++passes;
            const double placed = anchored_offset();
            if (placed != offset_) ++statistics_.anchor_corrections;
            offset_ = placed;
            const std::size_t stored =
                measure_band(offset_ - overscan, offset_ + height + overscan, maximum_measured_per_settle, asked);
            if (stored == 0U) {
                complete = asked < maximum_measured_per_settle;
                break;
            }
        }
    }
    const double placed = anchored_offset();
    if (placed != offset_) ++statistics_.anchor_corrections;
    offset_ = placed;
    capture_anchor();
    band_complete_ = complete;
    statistics_.last_settle_measured = asked;
    statistics_.last_settle_passes = passes;
    return complete;
}

void VirtualRowsView::update_realized() noexcept {
    const std::size_t count = row_count();
    if (count == 0U || viewport_.width <= 0.0 || viewport_.height <= 0.0) {
        first_realized_ = 0U;
        end_realized_ = 0U;
        return;
    }
    const double bottom = offset_ + viewport_.height;
    first_realized_ = (*heights_).row_at(offset_);
    std::size_t end = first_realized_;
    while (end < count && (*heights_).offset_of(end) < bottom) ++end;
    end_realized_ = end;
}

// -- scrolling --------------------------------------------------------------------------

double VirtualRowsView::content_height() const noexcept {
    return (*heights_).total_height();
}

void VirtualRowsView::note_scroll_start() noexcept {
    scroll_copyable_ = band_complete_ && first_realized_ < end_realized_;
    scroll_reference_row_ = first_realized_;
    scroll_reference_top_ = 0.0;
    if (scroll_copyable_) scroll_reference_top_ = (*heights_).offset_of(scroll_reference_row_) - offset_;
}

// Settles at the new anchor, then moves the painted pixels by what the
// reference row moved on screen. The copy is exact when every row the old
// viewport showed was already measured (band_complete_), since rows keep
// their heights and only the offset changed; otherwise the viewport is
// repainted.
void VirtualRowsView::finish_scroll() {
    static_cast<void>(settle());
    update_realized();
    update_scroll_bar();
    invalidate(Dirty::hit_test | Dirty::semantics);
    if (!scroll_copyable_ || scroll_reference_row_ >= row_count()) {
        ++statistics_.scroll_repaints;
        invalidate(Dirty::paint);
        return;
    }
    const double now = (*heights_).offset_of(scroll_reference_row_) - offset_;
    const double moved = now - scroll_reference_top_;
    if (moved == 0.0) return;
    if (scroll_content(viewport_, Point{0.0, moved})) {
        ++statistics_.scroll_copies;
    } else {
        ++statistics_.scroll_repaints;
    }
}

void VirtualRowsView::set_follow_tail(bool follow) {
    require_outside_callback();
    follow_tail_ = follow;
    capture_anchor();
}

// -- announcements ----------------------------------------------------------------------

void VirtualRowsView::set_announces_appends(bool announce) {
    require_outside_callback();
    announces_appends_ = announce;
    if (!announce) pending_appended_ = 0U;
}

// Writes "<n> new row(s), <total> in all" into announcement_, reusing its
// storage, and clears the pending count. The total makes two equal counts in
// a row still rename the live region, so each is announced.
void VirtualRowsView::compose_announcement() {
    std::array<char, row_digits_capacity> digits{};
    const std::to_chars_result appended =
        std::to_chars(digits.data(), digits.data() + digits.size(), pending_appended_);
    announcement_.assign(digits.data(), static_cast<std::size_t>(appended.ptr - digits.data()));
    announcement_.append(pending_appended_ == 1U ? announcement_one_row : announcement_rows);
    const std::to_chars_result total = std::to_chars(digits.data(), digits.data() + digits.size(), row_count());
    announcement_.append(digits.data(), static_cast<std::size_t>(total.ptr - digits.data()));
    announcement_.append(announcement_total);
    pending_appended_ = 0U;
}

void VirtualRowsView::scroll_to_offset(double offset) {
    require_outside_callback();
    if (!std::isfinite(offset)) throw std::invalid_argument("VirtualRowsView scroll offset must be finite");
    note_scroll_start();
    offset_ = std::clamp(offset, 0.0, maximum_offset());
    capture_anchor();
    finish_scroll();
}

void VirtualRowsView::scroll_to_row(VirtualRowIndex row, VirtualRowAlignment alignment) {
    require_outside_callback();
    if (row.value >= row_count()) throw std::out_of_range("VirtualRowsView row past the end");
    note_scroll_start();
    // Measure the row first, so its height is final when it is placed.
    if (!(*heights_).measured(row.value) && viewport_.width > 0.0) static_cast<void>(measure_row(row.value));
    const double top = (*heights_).offset_of(row.value);
    const double extent = (*heights_).height_of(row.value);
    const double shown = top - offset_;
    double anchor_top = shown;
    if (alignment == VirtualRowAlignment::start) {
        anchor_top = 0.0;
    } else if (alignment == VirtualRowAlignment::center) {
        anchor_top = (viewport_.height - extent) / 2.0;
    } else if (alignment == VirtualRowAlignment::end) {
        anchor_top = viewport_.height - extent;
    } else if (shown < 0.0 || extent > viewport_.height) {
        anchor_top = 0.0;
    } else if (shown + extent > viewport_.height) {
        anchor_top = viewport_.height - extent;
    }
    anchored_at_end_ = false;
    anchor_row_ = row.value;
    anchor_top_ = anchor_top;
    finish_scroll();
}

VirtualRowIndex VirtualRowsView::first_visible_row() const noexcept {
    const VirtualRowIndex index{first_realized_};
    return index;
}

VirtualRowIndex VirtualRowsView::end_visible_row() const noexcept {
    const VirtualRowIndex index{end_realized_};
    return index;
}

Rect VirtualRowsView::row_rectangle(std::size_t row) const noexcept {
    const double top = viewport_.y + (*heights_).offset_of(row) - offset_;
    const Rect bounds{viewport_.x, top, viewport_.width, (*heights_).height_of(row)};
    return bounds;
}

Rect VirtualRowsView::row_bounds(VirtualRowIndex row) const {
    if (row.value >= row_count()) throw std::out_of_range("VirtualRowsView row past the end");
    const Rect bounds = row_rectangle(row.value);
    return bounds;
}

std::optional<VirtualRowIndex> VirtualRowsView::row_at(Point local_point) const noexcept {
    if (!viewport_.contains(local_point) || row_count() == 0U) return std::nullopt;
    const double content = offset_ + (local_point.y - viewport_.y);
    if (content >= (*heights_).total_height()) return std::nullopt;
    const VirtualRowIndex index{(*heights_).row_at(content)};
    return index;
}

void VirtualRowsView::update_scroll_bar() {
    const bool shown = maximum_offset() > 0.0 && !bar_.empty();
    // Showing or hiding the bar is the next arrange's to place.
    if (shown != bar_shown_) {
        invalidate(Dirty::arrange);
        return;
    }
    sync_scroll_bar();
}

void VirtualRowsView::sync_scroll_bar() {
    if (!vertical_bar_ || !bar_shown_) return;
    const double maximum = maximum_offset();
    if (!(maximum > 0.0)) return;
    synchronizing_scroll_ = true;
    VScrollBar& bar = *vertical_bar_;
    bar.set_range(0.0, maximum);
    bar.small_change = (*heights_).estimated_height();
    bar.large_change = std::max(1.0, viewport_.height);
    bar.value = std::clamp(offset_, 0.0, maximum);
    synchronizing_scroll_ = false;
}

void VirtualRowsView::on_vertical_scroll(double value) {
    if (synchronizing_scroll_ || in_callback_ || !is_alive()) return;
    if (value == offset_) return;
    scroll_to_offset(value);
}

// -- selection --------------------------------------------------------------------------

std::optional<VirtualRowIndex> VirtualRowsView::selected_row() const noexcept {
    return as_row_index(selected_);
}

void VirtualRowsView::set_selected_row(std::optional<VirtualRowIndex> row) {
    require_outside_callback();
    require_mutable();
    if (row.has_value() && (*row).value >= row_count()) {
        throw std::out_of_range("VirtualRowsView selected row past the end");
    }
    const std::optional<std::size_t> previous = selected_;
    std::optional<std::size_t> next{};
    if (row.has_value()) next = (*row).value;
    if (next == previous) return;
    selected_ = next;
    invalidate(Dirty::paint | Dirty::semantics);
    raise_selection(previous);
}

void VirtualRowsView::select_and_reveal(std::size_t row) {
    const std::optional<std::size_t> previous = selected_;
    selected_ = row;
    scroll_to_row(VirtualRowIndex{row}, VirtualRowAlignment::nearest);
    invalidate(Dirty::paint | Dirty::semantics);
    if (previous != selected_) raise_selection(previous);
}

void VirtualRowsView::raise_selection(std::optional<std::size_t> previous) {
    VirtualRowSelection change{};
    change.previous = as_row_index(previous);
    change.current = as_row_index(selected_);
    selection_changed_.emit(change);
}

void VirtualRowsView::activate(std::size_t row) {
    row_activated_.emit(VirtualRowIndex{row});
}

VirtualRowsStatistics VirtualRowsView::statistics() const noexcept {
    VirtualRowsStatistics record = statistics_;
    record.index_runs = (*heights_).run_count();
    record.index_nodes = (*heights_).node_count();
    record.index_depth = (*heights_).depth();
    return record;
}

// -- layout and paint -------------------------------------------------------------------

Size VirtualRowsView::measure(Size available) {
    const Size preferred{std::min(available.width, preferred_extent), std::min(available.height, preferred_extent)};
    return preferred;
}

void VirtualRowsView::place_parts(Rect client) {
    client_ = client;
    mirrored_ = effective_layout_direction() == LayoutDirection::right_to_left;
    const double bar_width = std::min(scroll_bar_extent, client.width);
    const double content_width = client.width - bar_width;
    if (mirrored_) {
        bar_ = {client.x, client.y, bar_width, client.height};
        viewport_ = {client.x + bar_width, client.y, content_width, client.height};
    } else {
        bar_ = {client.x + content_width, client.y, bar_width, client.height};
        viewport_ = {client.x, client.y, content_width, client.height};
    }
}

void VirtualRowsView::arrange(Rect final_bounds) {
    arrange_self(final_bounds);
    place_parts({0.0, 0.0, final_bounds.width, final_bounds.height});
    static_cast<void>(settle());
    update_realized();
    // One announcement per layout for every row appended since the last
    // (insert_rows already asked for new semantics).
    if (pending_appended_ > 0U) compose_announcement();
    bar_shown_ = maximum_offset() > 0.0 && !bar_.empty();
    if (vertical_bar_) {
        set_child_layout_collapsed(vertical_bar_, !bar_shown_);
        if (bar_shown_) set_child_layout(vertical_bar_, bar_);
    }
    sync_scroll_bar();
}

AnatomyStateSet VirtualRowsView::row_states(std::size_t row) const {
    AnatomyStateSet states;
    const bool selected = selected_.has_value() && *selected_ == row;
    if (selected) states.insert(AnatomyState::selected);
    if (hot_.has_value() && *hot_ == row) states.insert(AnatomyState::hot);
    if (selected && focused_) states.insert(AnatomyState::current);
    if (!effectively_enabled()) states.insert(AnatomyState::disabled);
    const Window* const host = attached_window();
    if (selected && focused_ && host != nullptr && (*host).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    return states;
}

void VirtualRowsView::on_paint(Painter& painter, Rect) {
    PartPaint well;
    well.family = ControlVisualRole::editor;
    well.role = PartRole::well;
    well.bounds = client_;
    paint_part(painter, *this, well);
    // An opaque back colour covers the viewport, which lets a scroll move
    // the painted pixels (scroll_content needs the cover).
    const std::optional<Color> background = back_color.read();
    if (background.has_value() && (*background).alpha == 255U && !viewport_.empty()) {
        painter.fill_rect(viewport_, *background);
        report_opaque_cover(painter, viewport_);
    }
    std::size_t painted = 0U;
    if (!viewport_.empty() && first_realized_ < end_realized_) {
        painter.save();
        painter.clip_rect(viewport_);
        VirtualRowPaint request{};
        request.painter = &painter;
        request.enabled = effectively_enabled();
        request.mirrored = mirrored_;
        PartPaint plate;
        // The row is the selection family's part: transparent until hot or
        // selected.
        plate.family = ControlVisualRole::selection;
        double top = viewport_.y + (*heights_).offset_of(first_realized_) - offset_;
        for (std::size_t row = first_realized_; row < end_realized_; ++row) {
            const double extent = (*heights_).height_of(row);
            const Rect bounds{viewport_.x, top, viewport_.width, extent};
            const AnatomyStateSet states = row_states(row);
            plate.role = PartRole::face;
            plate.bounds = bounds;
            plate.states = states;
            paint_part(painter, *this, plate);
            request.row = VirtualRowIndex{row};
            request.bounds = bounds;
            request.selected = states.contains(AnatomyState::selected);
            request.hot = states.contains(AnatomyState::hot);
            request.focused = states.contains(AnatomyState::current);
            if (callbacks_.paint) {
                painter.save();
                painter.clip_rect(bounds);
                {
                    const CallbackScope scope(*this);
                    callbacks_.paint(request);
                }
                painter.restore();
            }
            if (states.contains(AnatomyState::focus_visible)) {
                plate.role = PartRole::focus;
                paint_part(painter, *this, plate);
            }
            ++painted;
            top = top + extent;
        }
        painter.restore();
    }
    statistics_.rows_painted = statistics_.rows_painted + painted;
    statistics_.last_paint_rows = painted;
    // With no row to carry the focus cue, the view itself shows it.
    const Window* const host = attached_window();
    if (painted == 0U && focused_ && host != nullptr && (*host).focus_cue_visible()) {
        PartPaint focus;
        focus.family = ControlVisualRole::editor;
        focus.role = PartRole::focus;
        focus.bounds = client_;
        focus.states.insert(AnatomyState::focus_visible);
        paint_part(painter, *this, focus);
    }
}

// -- input ------------------------------------------------------------------------------

void VirtualRowsView::on_pointer(PointerEvent& event) {
    if (!eligible_for_input() || in_callback_) return;
    const Rect absolute = absolute_bounds();
    const Point local{event.position.x - absolute.x, event.position.y - absolute.y};
    const Control::Ptr lifetime = shared_from_this();
    if (event.action == PointerAction::wheel) {
        // Nothing to scroll: the wheel is left to the parent.
        if (event.wheel_delta.y == 0.0 || !(maximum_offset() > 0.0)) return;
        double step = event.wheel_delta.y;
        if (event.wheel_unit == WheelDeltaUnit::lines) {
            step = event.wheel_delta.y * wheel_rows_per_line * (*heights_).estimated_height();
        }
        scroll_to_offset(offset_ - step);
        event.handled = true;
        return;
    }
    if (event.action == PointerAction::move || event.action == PointerAction::leave) {
        std::optional<std::size_t> hot{};
        if (event.action == PointerAction::move) {
            const std::optional<VirtualRowIndex> under = row_at(local);
            if (under.has_value()) hot = (*under).value;
        }
        if (hot != hot_) {
            hot_ = hot;
            invalidate(Dirty::paint);
        }
        return;
    }
    if (event.action != PointerAction::down || event.button != PointerButton::primary) return;
    if (window() != nullptr) static_cast<void>((*window()).request_focus(lifetime));
    if (!is_alive()) return;
    event.handled = true;
    const std::optional<VirtualRowIndex> under = row_at(local);
    if (!under.has_value()) return;
    const std::size_t row = (*under).value;
    const bool again = selected_.has_value() && *selected_ == row;
    select_and_reveal(row);
    if (event.click_count >= 2U && again && is_alive()) activate(row);
}

void VirtualRowsView::on_key(KeyEvent& event) {
    if (!focused_ || in_callback_ || !effectively_enabled() || event.action != KeyAction::down) return;
    const std::size_t count = row_count();
    if (count == 0U) return;
    const Control::Ptr lifetime = shared_from_this();
    const std::uint32_t key = event.physical_key;
    const bool has_selection = selected_.has_value();
    const std::size_t from = has_selection ? *selected_ : std::min(first_realized_, count - 1U);
    if (key == PhysicalKey::enter || key == PhysicalKey::space) {
        if (has_selection) activate(from);
        event.handled = true;
        return;
    }
    std::size_t target = from;
    if (key == PhysicalKey::up) {
        if (has_selection && from > 0U) target = from - 1U;
    } else if (key == PhysicalKey::down) {
        if (has_selection && from + 1U < count) target = from + 1U;
    } else if (key == PhysicalKey::page_up) {
        const double top = (*heights_).offset_of(from) - viewport_.height;
        target = top > 0.0 ? (*heights_).row_at(top) : 0U;
        if (target == from && from > 0U) target = from - 1U;
    } else if (key == PhysicalKey::page_down) {
        const double bottom = (*heights_).offset_of(from) + viewport_.height;
        target = (*heights_).row_at(bottom);
        if (target == from && from + 1U < count) target = from + 1U;
    } else if (key == PhysicalKey::home) {
        target = 0U;
    } else if (key == PhysicalKey::end) {
        target = count - 1U;
    } else {
        return;
    }
    select_and_reveal(target);
    event.handled = true;
}

void VirtualRowsView::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- semantics --------------------------------------------------------------------------

void VirtualRowsView::row_identity(std::size_t row, std::string& identity) const {
    std::array<char, row_digits_capacity> digits{};
    const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), row);
    const std::size_t length = static_cast<std::size_t>(written.ptr - digits.data());
    identity.assign(stable_id().value());
    identity.append(row_segment);
    identity.append(digits.data(), length);
}

std::optional<std::size_t> VirtualRowsView::row_of_child_id(std::string_view stable_id_text) const noexcept {
    const std::string_view view_id = stable_id().value();
    const std::size_t prefix = view_id.size() + row_segment.size();
    if (stable_id_text.size() <= prefix) return std::nullopt;
    if (stable_id_text.substr(0U, view_id.size()) != view_id) return std::nullopt;
    if (stable_id_text.substr(view_id.size(), row_segment.size()) != row_segment) return std::nullopt;
    const std::string_view digits = stable_id_text.substr(prefix);
    // One canonical spelling per row: no leading zero but "0" itself.
    if (digits.size() > 1U && digits.front() == '0') return std::nullopt;
    std::size_t row = 0U;
    const std::from_chars_result parsed = std::from_chars(digits.data(), digits.data() + digits.size(), row);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) return std::nullopt;
    if (row >= row_count()) return std::nullopt;
    return row;
}

SemanticDescriptor VirtualRowsView::semantic_descriptor() const {
    SemanticDescriptor descriptor{};
    // A list sized by every row; only the realized rows are its children.
    descriptor.role = SemanticRole::list;
    descriptor.name = accessible_name.read();
    descriptor.description = accessible_description.read();
    descriptor.row_count = row_count();
    if (selected_.has_value()) {
        description_.row = VirtualRowIndex{*selected_};
        description_.name.clear();
        description_.value.clear();
        if (callbacks_.describe) {
            const CallbackScope scope(*this);
            callbacks_.describe(description_);
        }
        if (validate_utf8(description_.name).valid()) descriptor.value = description_.name;
    }
    descriptor.actions = {SemanticAction::focus};
    descriptor.exposed = true;
    return descriptor;
}

std::vector<SemanticNode> VirtualRowsView::semantic_virtual_children() const {
    std::vector<SemanticNode> children{};
    const std::size_t count = row_count();
    const bool realized = first_realized_ < end_realized_ && end_realized_ <= count;
    const std::size_t first = realized ? first_realized_ : 0U;
    const std::size_t end = realized ? end_realized_ : 0U;
    // The realized rows, then the live region.
    children.reserve(end - first + 1U);
    const Rect absolute = absolute_bounds();
    const bool enabled = effectively_enabled();
    for (std::size_t row = first; row < end; ++row) {
        description_.row = VirtualRowIndex{row};
        description_.name.clear();
        description_.value.clear();
        if (callbacks_.describe) {
            const CallbackScope scope(*this);
            callbacks_.describe(description_);
        }
        ++statistics_.rows_described;
        SemanticNode node{};
        row_identity(row, node.stable_id);
        node.runtime_id = virtual_semantic_runtime_id(node.stable_id);
        node.role = SemanticRole::list_item;
        if (!callbacks_.describe) {
            // Without a describe target a row is named by its position.
            std::array<char, row_digits_capacity> digits{};
            const std::to_chars_result written =
                std::to_chars(digits.data(), digits.data() + digits.size(), row + 1U);
            node.name.assign(digits.data(), static_cast<std::size_t>(written.ptr - digits.data()));
        } else if (validate_utf8(description_.name).valid()) {
            node.name = description_.name;
        }
        if (validate_utf8(description_.value).valid()) node.value = description_.value;
        node.position_in_set = row + 1U;
        node.set_size = count;
        const Rect shown = Rect::intersection(row_rectangle(row), viewport_);
        node.bounds = {absolute.x + shown.x, absolute.y + shown.y, shown.width, shown.height};
        node.states = SemanticState::visible | SemanticState::focusable;
        if (enabled) node.states |= SemanticState::enabled;
        const bool selected = selected_.has_value() && *selected_ == row;
        if (selected) node.states |= SemanticState::selected;
        if (selected && focused_) node.states |= SemanticState::focused;
        node.actions = {SemanticAction::focus, SemanticAction::select, SemanticAction::press};
        children.push_back(std::move(node));
    }
    // Always present, so a host sees the name change rather than a new
    // node (semantic_announcement_due).
    SemanticNode announcements{};
    announcements.stable_id.assign(stable_id().value());
    announcements.stable_id.append(announcements_segment);
    announcements.runtime_id = virtual_semantic_runtime_id(announcements.stable_id);
    announcements.role = SemanticRole::live_region;
    announcements.name = announcement_;
    announcements.bounds = {absolute.x + viewport_.x, absolute.y + viewport_.y, viewport_.width, viewport_.height};
    announcements.states = SemanticState::visible;
    if (enabled) announcements.states |= SemanticState::enabled;
    children.push_back(std::move(announcements));
    return children;
}

bool VirtualRowsView::on_semantic_child_action(std::string_view stable_id_text, SemanticAction action,
                                               std::string_view) {
    if (!effectively_enabled() || in_callback_) return false;
    const std::optional<std::size_t> row = row_of_child_id(stable_id_text);
    if (!row.has_value()) return false;
    if (action != SemanticAction::focus && action != SemanticAction::select && action != SemanticAction::press) {
        return false;
    }
    const Control::Ptr lifetime = shared_from_this();
    if (window() != nullptr) static_cast<void>((*window()).request_focus(lifetime));
    if (!is_alive()) return true;
    select_and_reveal(*row);
    if (action == SemanticAction::press && is_alive()) activate(*row);
    return true;
}

// -- anatomy ----------------------------------------------------------------------------

const ControlAnatomy& VirtualRowsView::anatomy() const noexcept {
    return virtual_rows_view_anatomy;
}

void VirtualRowsView::append_interactions(std::vector<InteractionDeclaration>& interactions) const {
    Control::append_interactions(interactions);
    interactions.insert(interactions.end(), virtual_rows_view_interactions.begin(),
                        virtual_rows_view_interactions.end());
}

void VirtualRowsView::publish_part_geometry(std::span<PartGeometry> geometry) const {
    Parts record;
    record.region = PartGeometry::from_bounds(client_);
    record.well = PartGeometry::from_bounds(client_);
    if (bar_shown_) record.vertical_scroll_bar = PartGeometry::from_bounds(bar_);
    record.focus = PartGeometry::from_bounds(client_);
    write_parts(anatomy(), virtual_rows_view_part_fields, record, geometry);
}

VirtualRowsView::Parts VirtualRowsView::parts() const {
    const PartGeometryBuffer published = published_part_geometry();
    Parts record;
    read_parts(anatomy(), virtual_rows_view_part_fields, published.parts(), record);
    return record;
}

VirtualRowsView::States VirtualRowsView::states() const {
    States record;
    read_states(virtual_rows_view_state_fields, anatomy_states(), record);
    return record;
}

std::size_t VirtualRowsView::realized_item_count() const {
    const std::size_t count = end_realized_ - first_realized_;
    return count;
}

std::string VirtualRowsView::realized_item_stable_id(std::size_t item) const {
    if (item >= realized_item_count()) throw std::out_of_range("VirtualRowsView realized item past the end");
    std::string identity;
    row_identity(first_realized_ + item, identity);
    return identity;
}

void VirtualRowsView::publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const {
    const std::size_t row = first_realized_ + item;
    const Rect bounds = Rect::intersection(row_rectangle(row), viewport_);
    RowParts record;
    record.row = PartGeometry::from_bounds(bounds);
    record.row_face = PartGeometry::from_bounds(bounds);
    if (row_states(row).contains(AnatomyState::focus_visible)) record.row_focus = PartGeometry::from_bounds(bounds);
    const ItemSystemDeclaration& system = anatomy().item_systems[row_system.value];
    write_item_parts(system, virtual_rows_view_row_part_fields, record, geometry);
}

AnatomyStateSet VirtualRowsView::realized_item_anatomy_states(std::size_t item) const {
    if (item >= realized_item_count()) throw std::out_of_range("VirtualRowsView realized item past the end");
    const AnatomyStateSet states = row_states(first_realized_ + item);
    return states;
}

} // namespace gui_forms
