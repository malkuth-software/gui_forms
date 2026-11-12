#include "gui_forms/controls/virtual_rows_view.hpp"

#include "../collections/row_height_index.hpp"
#include "../panel/input_control_utilities.hpp"

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
#include <span>
#include <system_error>
#include <utility>
#include <vector>

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

using input_control_detail::command_modifier;
using input_control_detail::includes;

// Writes value's decimal digits at the start of digits and returns their
// count. The span carries its own length, so the end pointer is formed from
// a length-bearing view; row_digits_capacity holds every 64-bit value.
[[nodiscard]] std::size_t write_decimal(std::span<char> digits, std::size_t value) noexcept {
    const std::to_chars_result written = std::to_chars(digits.data(), digits.data() + digits.size(), value);
    const std::size_t length = static_cast<std::size_t>(written.ptr - digits.data());
    return length;
}

[[nodiscard]] VirtualRowRange range_of(std::size_t first, std::size_t count) noexcept {
    const VirtualRowRange range{VirtualRowIndex{first}, count};
    return range;
}

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
constexpr std::array<InteractionDeclaration, 3> virtual_rows_view_interactions{{
    {.name = "selection_changed", .payload = "VirtualRowSelection", .mutable_payload = false},
    {.name = "row_activated", .payload = "VirtualRowIndex", .mutable_payload = false},
    {.name = "scrolled", .payload = "VirtualRowScroll", .mutable_payload = false},
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
    report_scroll();
}

void VirtualRowsView::reset_rows(std::size_t count) {
    require_outside_callback();
    require_mutable();
    if (count > maximum_rows) throw std::length_error("VirtualRowsView holds at most maximum_rows rows");
    const Control::Ptr lifetime = shared_from_this();
    const std::optional<std::size_t> previous = selected_;
    const bool had_selection = previous.has_value() || !ranges_.empty();
    (*heights_).assign(count, (*heights_).estimated_height());
    offset_ = 0.0;
    anchor_row_ = 0U;
    anchor_top_ = 0.0;
    anchored_at_end_ = false;
    pending_appended_ = 0U;
    selected_.reset();
    ranges_.clear();
    selection_anchor_.reset();
    dragging_ = false;
    hot_.reset();
    rows_changed();
    report_scroll();
    if (!is_alive()) return;
    if (had_selection) raise_selection(previous);
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
    if (selection_anchor_.has_value() && *selection_anchor_ >= at.value) {
        selection_anchor_ = *selection_anchor_ + count;
    }
    shift_selection_for_insert(at.value, count);
    if (hot_.has_value() && *hot_ >= at.value) hot_ = *hot_ + count;
    // Rows inserted at or before the anchor push it down with its content;
    // an end anchor stays the end.
    if (anchored_at_end_) {
        anchor_row_ = row_count();
    } else if (anchor_row_ >= at.value) {
        anchor_row_ = anchor_row_ + count;
    }
    rows_changed();
    report_scroll();
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
    const Control::Ptr lifetime = shared_from_this();
    const std::optional<std::size_t> previous = selected_;
    (*heights_).erase(at.value, count);
    bool selection_erased = shift_selection_for_erase(at.value, count);
    if (selected_.has_value() && *selected_ >= at.value && *selected_ < end) {
        selected_.reset();
        selection_erased = true;
    } else if (selected_.has_value() && *selected_ >= end) {
        selected_ = *selected_ - count;
    }
    // An erased anchor moves to the first row after the erased ones.
    if (selection_anchor_.has_value() && *selection_anchor_ >= end) {
        selection_anchor_ = *selection_anchor_ - count;
    } else if (selection_anchor_.has_value() && *selection_anchor_ >= at.value) {
        selection_anchor_ = at.value;
        if (at.value >= row_count()) selection_anchor_.reset();
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
    report_scroll();
    if (!is_alive()) return;
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
    // Every position (and so the anchor and the selection) stays where it
    // was.
    (*heights_).forget(first.value, count);
    rows_changed();
    report_scroll();
}

void VirtualRowsView::invalidate_rows(VirtualRowIndex first, std::size_t count) {
    require_outside_callback();
    require_mutable();
    const std::size_t rows = row_count();
    if (first.value > rows || count > rows - first.value) {
        throw std::out_of_range("VirtualRowsView invalidated range past the end");
    }
    if (count == 0U) return;
    // The invalidated rows in view before, and the content span they took.
    const std::size_t end = first.value + count;
    const std::size_t shown_first = std::max(first.value, first_realized_);
    const std::size_t shown_end = std::min(end, end_realized_);
    const bool shown = shown_first < shown_end;
    const double offset_before = offset_;
    double top_before = 0.0;
    double bottom_before = 0.0;
    if (shown) {
        top_before = (*heights_).offset_of(shown_first);
        bottom_before = (*heights_).offset_of(shown_end);
    }
    // Forgotten, then measured again by the settle where they are in the
    // band; the anchor keeps the rows on screen where they were.
    (*heights_).forget(first.value, count);
    static_cast<void>(settle());
    update_realized();
    update_scroll_bar();
    invalidate(Dirty::hit_test | Dirty::semantics);
    bool partial = shown && offset_ == offset_before;
    if (partial) {
        partial = (*heights_).offset_of(shown_first) == top_before && (*heights_).offset_of(shown_end) == bottom_before;
    }
    if (partial) {
        // Nothing moved: only the rows' own rectangles change.
        const Rect span{viewport_.x, viewport_.y + top_before - offset_, viewport_.width, bottom_before - top_before};
        invalidate(Rect::intersection(span, viewport_));
        ++statistics_.partial_repaints;
    } else if (shown) {
        invalidate(Dirty::paint);
    }
    report_scroll();
}

void VirtualRowsView::set_measured_row_budget(std::optional<std::size_t> rows) {
    require_outside_callback();
    require_mutable();
    measured_budget_ = rows;
    rows_changed();
    report_scroll();
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
    report_scroll();
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
        const std::size_t band_first = (*heights_).row_at(std::max(0.0, offset_ - overscan));
        const std::size_t band_end = std::min(count, (*heights_).row_at(offset_ + height + overscan) + 1U);
        enforce_measured_budget(band_first, band_end);
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

void VirtualRowsView::enforce_measured_budget(std::size_t band_first, std::size_t band_end) {
    if (!measured_budget_.has_value()) return;
    const std::size_t budget = *measured_budget_;
    const std::size_t measured = (*heights_).measured_count();
    if (measured <= budget) return;
    const std::size_t count = row_count();
    // The keep window: budget / 2 rows on each side of the first visible
    // row, widened to the band.
    const std::size_t reference = std::min((*heights_).row_at(offset_), count - 1U);
    const std::size_t half = budget / 2U;
    const std::size_t window_first = reference - std::min(reference, half);
    const std::size_t window_end = reference + 1U + std::min(half, count - reference - 1U);
    const std::size_t keep_first = std::min(window_first, band_first);
    const std::size_t keep_end = std::max(window_end, band_end);
    if (keep_end < count) (*heights_).forget(keep_end, count - keep_end);
    if (keep_first > 0U) (*heights_).forget(0U, keep_first);
    const std::size_t remaining = (*heights_).measured_count();
    statistics_.rows_forgotten = statistics_.rows_forgotten + (measured - remaining);
    ++statistics_.budget_compactions;
}

void VirtualRowsView::report_scroll() {
    if (offset_ == reported_offset_ && first_realized_ == reported_first_) return;
    reported_offset_ = offset_;
    reported_first_ = first_realized_;
    VirtualRowScroll change{};
    change.first_visible = VirtualRowIndex{first_realized_};
    change.offset = offset_;
    change.following_tail = anchored_at_end_;
    scrolled_.emit(change);
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
    } else {
        const double now = (*heights_).offset_of(scroll_reference_row_) - offset_;
        const double moved = now - scroll_reference_top_;
        if (moved != 0.0 && scroll_content(viewport_, Point{0.0, moved})) {
            ++statistics_.scroll_copies;
        } else if (moved != 0.0) {
            ++statistics_.scroll_repaints;
        }
    }
    report_scroll();
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
    const std::size_t appended = write_decimal(digits, pending_appended_);
    announcement_.assign(digits.data(), appended);
    announcement_.append(pending_appended_ == 1U ? announcement_one_row : announcement_rows);
    const std::size_t total = write_decimal(digits, row_count());
    announcement_.append(digits.data(), total);
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

void VirtualRowsView::set_selection_mode(ListSelectionMode mode) {
    require_outside_callback();
    require_mutable();
    if (mode == selection_mode_) return;
    selection_mode_ = mode;
    dragging_ = false;
    invalidate(Dirty::semantics);
    if (mode != ListSelectionMode::one) return;
    // The single mode keeps the current row selected alone.
    const std::size_t wanted = selected_.has_value() ? 1U : 0U;
    const bool same = ranges_.size() == wanted &&
                      (wanted == 0U || ranges_[0] == range_of(*selected_, 1U));
    if (same) return;
    if (selected_.has_value()) {
        select_only(*selected_, 1U);
    } else {
        ranges_.clear();
    }
    invalidate(Dirty::paint);
    raise_selection(selected_);
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
    const std::size_t wanted = next.has_value() ? 1U : 0U;
    const bool same_set = ranges_.size() == wanted && (wanted == 0U || ranges_[0] == range_of(*next, 1U));
    if (same_set && next == previous) return;
    if (next.has_value()) {
        select_only(*next, 1U);
    } else {
        ranges_.clear();
    }
    selected_ = next;
    selection_anchor_ = next;
    invalidate(Dirty::paint | Dirty::semantics);
    raise_selection(previous);
}

bool VirtualRowsView::row_selected(VirtualRowIndex row) const noexcept {
    if (row.value >= row_count()) return false;
    const bool selected = selected_at(row.value);
    return selected;
}

std::size_t VirtualRowsView::selected_row_count() const noexcept {
    std::size_t total = 0U;
    for (const VirtualRowRange& range : ranges_) {
        total = total + range.count;
    }
    return total;
}

void VirtualRowsView::select_range(VirtualRowIndex anchor, VirtualRowIndex current) {
    require_outside_callback();
    require_mutable();
    if (selection_mode_ != ListSelectionMode::multiple_extended) {
        throw std::logic_error("VirtualRowsView selects ranges only in the extended selection mode");
    }
    const std::size_t rows = row_count();
    if (anchor.value >= rows || current.value >= rows) {
        throw std::out_of_range("VirtualRowsView selected range past the end");
    }
    const std::optional<std::size_t> previous = selected_;
    const std::size_t first = std::min(anchor.value, current.value);
    const std::size_t count = std::max(anchor.value, current.value) - first + 1U;
    const bool same_set = ranges_.size() == 1U && ranges_[0] == range_of(first, count);
    selection_anchor_ = anchor.value;
    if (same_set && previous == current.value) return;
    select_only(first, count);
    selected_ = current.value;
    invalidate(Dirty::paint | Dirty::semantics);
    raise_selection(previous);
}

void VirtualRowsView::clear_selection() {
    require_outside_callback();
    require_mutable();
    if (ranges_.empty()) return;
    ranges_.clear();
    invalidate(Dirty::paint | Dirty::semantics);
    raise_selection(selected_);
}

// The last range starting at or before the row holds it, or none does.
bool VirtualRowsView::selected_at(std::size_t row) const noexcept {
    std::size_t low = 0U;
    std::size_t high = ranges_.size();
    while (low < high) {
        const std::size_t middle = low + (high - low) / 2U;
        if (ranges_[middle].first.value <= row) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    if (low == 0U) return false;
    const VirtualRowRange& range = ranges_[low - 1U];
    const bool inside = row - range.first.value < range.count;
    return inside;
}

void VirtualRowsView::select_only(std::size_t first, std::size_t count) {
    ranges_.clear();
    if (count > 0U) ranges_.push_back(range_of(first, count));
}

void VirtualRowsView::toggle_row(std::size_t row) {
    // The index of the first range starting after the row.
    std::size_t after = 0U;
    while (after < ranges_.size() && ranges_[after].first.value <= row) ++after;
    if (after > 0U && row - ranges_[after - 1U].first.value < ranges_[after - 1U].count) {
        // Selected: split its range around it.
        const VirtualRowRange range = ranges_[after - 1U];
        const std::size_t before_count = row - range.first.value;
        const std::size_t after_count = range.first.value + range.count - row - 1U;
        const std::ptrdiff_t at = static_cast<std::ptrdiff_t>(after - 1U);
        if (before_count == 0U && after_count == 0U) {
            ranges_.erase(ranges_.begin() + at);
        } else if (before_count == 0U) {
            ranges_[after - 1U] = range_of(row + 1U, after_count);
        } else if (after_count == 0U) {
            ranges_[after - 1U].count = before_count;
        } else {
            ranges_[after - 1U].count = before_count;
            ranges_.insert(ranges_.begin() + at + 1, range_of(row + 1U, after_count));
        }
        return;
    }
    // Unselected: a new range of one, joined with an adjacent neighbour.
    ranges_.insert(ranges_.begin() + static_cast<std::ptrdiff_t>(after), range_of(row, 1U));
    std::size_t placed = after;
    if (placed > 0U && ranges_[placed - 1U].first.value + ranges_[placed - 1U].count == row) {
        ranges_[placed - 1U].count = ranges_[placed - 1U].count + 1U;
        ranges_.erase(ranges_.begin() + static_cast<std::ptrdiff_t>(placed));
        placed = placed - 1U;
    }
    if (placed + 1U < ranges_.size() && ranges_[placed + 1U].first.value == row + 1U) {
        ranges_[placed].count = ranges_[placed].count + ranges_[placed + 1U].count;
        ranges_.erase(ranges_.begin() + static_cast<std::ptrdiff_t>(placed + 1U));
    }
}

// Inserted rows are unselected: a range they fall inside is split around
// them, and ranges after them move down.
void VirtualRowsView::shift_selection_for_insert(std::size_t at, std::size_t count) {
    for (std::size_t index = 0U; index < ranges_.size(); ++index) {
        VirtualRowRange& range = ranges_[index];
        const std::size_t range_end = range.first.value + range.count;
        if (range.first.value >= at) {
            range.first.value = range.first.value + count;
        } else if (at < range_end) {
            const VirtualRowRange tail = range_of(at + count, range_end - at);
            range.count = at - range.first.value;
            ranges_.insert(ranges_.begin() + static_cast<std::ptrdiff_t>(index + 1U), tail);
            ++index;
        }
    }
}

// Erased rows leave their ranges; ranges after them move up, and two ranges
// the erasure brought together are joined. Compacts in place.
bool VirtualRowsView::shift_selection_for_erase(std::size_t at, std::size_t count) {
    const std::size_t end = at + count;
    bool erased = false;
    std::size_t kept = 0U;
    for (std::size_t index = 0U; index < ranges_.size(); ++index) {
        VirtualRowRange range = ranges_[index];
        const std::size_t range_end = range.first.value + range.count;
        if (range.first.value >= end) {
            range.first.value = range.first.value - count;
        } else if (range_end > at) {
            erased = true;
            const std::size_t before = at > range.first.value ? at - range.first.value : 0U;
            const std::size_t after = range_end > end ? range_end - end : 0U;
            range.first.value = std::min(range.first.value, at);
            range.count = before + after;
        }
        if (range.count == 0U) continue;
        if (kept > 0U && ranges_[kept - 1U].first.value + ranges_[kept - 1U].count == range.first.value) {
            ranges_[kept - 1U].count = ranges_[kept - 1U].count + range.count;
            continue;
        }
        ranges_[kept] = range;
        ++kept;
    }
    ranges_.resize(kept);
    return erased;
}

void VirtualRowsView::select_and_reveal(std::size_t row, SelectGesture gesture) {
    const Control::Ptr lifetime = shared_from_this();
    const std::optional<std::size_t> previous = selected_;
    const bool extended = selection_mode_ == ListSelectionMode::multiple_extended;
    bool changed = false;
    if (extended && gesture == SelectGesture::toggle) {
        toggle_row(row);
        selection_anchor_ = row;
        changed = true;
    } else if (extended && gesture == SelectGesture::extend && selection_anchor_.has_value()) {
        const std::size_t first = std::min(*selection_anchor_, row);
        const std::size_t count = std::max(*selection_anchor_, row) - first + 1U;
        changed = !(ranges_.size() == 1U && ranges_[0] == range_of(first, count));
        select_only(first, count);
    } else {
        changed = !(ranges_.size() == 1U && ranges_[0] == range_of(row, 1U));
        select_only(row, 1U);
        selection_anchor_ = row;
    }
    selected_ = row;
    scroll_to_row(VirtualRowIndex{row}, VirtualRowAlignment::nearest);
    if (!is_alive()) return;
    invalidate(Dirty::paint | Dirty::semantics);
    if (changed || previous != selected_) raise_selection(previous);
}

void VirtualRowsView::raise_selection(std::optional<std::size_t> previous) {
    VirtualRowSelection change{};
    change.previous = as_row_index(previous);
    change.current = as_row_index(selected_);
    change.selected_count = selected_row_count();
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
    record.index_bytes = (*heights_).storage_bytes();
    record.index_measured = (*heights_).measured_count();
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
    report_scroll();
}

AnatomyStateSet VirtualRowsView::row_states(std::size_t row) const {
    AnatomyStateSet states;
    if (selected_at(row)) states.insert(AnatomyState::selected);
    if (hot_.has_value() && *hot_ == row) states.insert(AnatomyState::hot);
    // The current row carries the focus; in the extended mode it may be
    // unselected.
    const bool current = selected_.has_value() && *selected_ == row && focused_;
    if (current) states.insert(AnatomyState::current);
    if (!effectively_enabled()) states.insert(AnatomyState::disabled);
    const Window* const host = attached_window();
    if (current && host != nullptr && (*host).focus_cue_visible()) {
        states.insert(AnatomyState::focus_visible);
    }
    return states;
}

void VirtualRowsView::on_paint(Painter& painter, Rect) {
    PartPaint well;
    well.family = ControlVisualRole::editor;
    well.role = PartRole::well;
    well.bounds = client_;
    // back_color is the per-control override of the theme's ground, as
    // DataGridView's background colour is. paint_part reports the cover
    // when the resolved well (the theme's material, or back_color over it
    // outside high contrast) is opaque, and only then can a scroll move the
    // painted pixels (scroll_content needs the cover).
    well.authored_background = back_color.read();
    paint_part(painter, *this, well);
    std::size_t painted = 0U;
    if (!viewport_.empty() && first_realized_ < end_realized_) {
        painter.save();
        painter.clip_rect(viewport_);
        VirtualRowPaint request{};
        request.painter = &painter;
        request.view = this;
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
            request.states = states;
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

void VirtualRowsView::handle_pointer(PointerEvent& event) {
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
        if (event.action == PointerAction::move && dragging_) drag_to(local);
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
    const bool extended = selection_mode_ == ListSelectionMode::multiple_extended;
    SelectGesture gesture = SelectGesture::replace;
    if (extended && includes(event.modifiers, Modifier::shift)) {
        gesture = SelectGesture::extend;
    } else if (extended && command_modifier(event.modifiers)) {
        gesture = SelectGesture::toggle;
    }
    select_and_reveal(row, gesture);
    if (!is_alive()) return;
    // A plain or Shift press drags a range until the release.
    dragging_ = extended && gesture != SelectGesture::toggle && event.click_count < 2U;
    if (event.click_count >= 2U && again) activate(row);
}

// The row under a dragging pointer, or the row just past the viewport's
// edge when the pointer is beyond it; extending to it scrolls it in.
void VirtualRowsView::drag_to(Point local) {
    const std::size_t count = row_count();
    if (count == 0U || first_realized_ >= end_realized_) return;
    std::size_t row = 0U;
    if (local.y < viewport_.y) {
        row = first_realized_ > 0U ? first_realized_ - 1U : 0U;
    } else if (local.y >= viewport_.y + viewport_.height) {
        row = std::min(end_realized_, count - 1U);
    } else {
        const double content = offset_ + (local.y - viewport_.y);
        row = std::min((*heights_).row_at(content), count - 1U);
    }
    if (selected_.has_value() && *selected_ == row) return;
    select_and_reveal(row, SelectGesture::extend);
}

void VirtualRowsView::handle_key(KeyEvent& event) {
    if (!focused_ || in_callback_ || !effectively_enabled() || event.action != KeyAction::down) return;
    const std::size_t count = row_count();
    if (count == 0U) return;
    const Control::Ptr lifetime = shared_from_this();
    const std::uint32_t key = event.physical_key;
    const bool has_selection = selected_.has_value();
    const std::size_t from = has_selection ? *selected_ : std::min(first_realized_, count - 1U);
    const bool extended = selection_mode_ == ListSelectionMode::multiple_extended;
    if (key == PhysicalKey::space && extended && command_modifier(event.modifiers)) {
        // Ctrl+Space toggles the current row (the row in view without one).
        select_and_reveal(from, SelectGesture::toggle);
        event.handled = true;
        return;
    }
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
    const bool extend = extended && includes(event.modifiers, Modifier::shift);
    select_and_reveal(target, extend ? SelectGesture::extend : SelectGesture::replace);
    event.handled = true;
}

void VirtualRowsView::on_mouse_down(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_down(arguments);
}

void VirtualRowsView::on_mouse_move(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_move(arguments);
}

void VirtualRowsView::on_mouse_up(MouseEventArgs& arguments) {
    if (arguments.button == PointerButton::primary) dragging_ = false;
    Control::on_mouse_up(arguments);
}

void VirtualRowsView::on_mouse_capture_changed() {
    dragging_ = false;
    Control::on_mouse_capture_changed();
}

void VirtualRowsView::on_mouse_wheel(MouseEventArgs& arguments) {
    PointerEvent event = pointer_event_of(arguments);
    handle_pointer(event);
    arguments.handled = event.handled;
    if (!is_alive()) return;
    Control::on_mouse_wheel(arguments);
}

void VirtualRowsView::on_mouse_leave() {
    PointerEvent leave{};
    leave.action = PointerAction::leave;
    handle_pointer(leave);
    if (!is_alive()) return;
    Control::on_mouse_leave();
}

void VirtualRowsView::on_key_down(KeyEventArgs& arguments) {
    Control::on_key_down(arguments);
    if (arguments.handled) return;
    KeyEvent event = key_event_of(arguments, KeyAction::down);
    handle_key(event);
    if (event.handled) arguments.handled = true;
}

void VirtualRowsView::on_focus_changed(bool focused) {
    focused_ = focused;
    invalidate(Dirty::paint | Dirty::semantics);
}

// -- semantics --------------------------------------------------------------------------

void VirtualRowsView::row_identity(std::size_t row, std::string& identity) const {
    std::array<char, row_digits_capacity> digits{};
    const std::size_t length = write_decimal(digits, row);
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
    if (selection_mode_ == ListSelectionMode::multiple_extended) {
        descriptor.states |= SemanticState::multiselectable;
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
            const std::size_t length = write_decimal(digits, row + 1U);
            node.name.assign(digits.data(), length);
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
        if (selected_at(row)) node.states |= SemanticState::selected;
        if (selected_.has_value() && *selected_ == row && focused_) node.states |= SemanticState::focused;
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
    select_and_reveal(*row, SelectGesture::replace);
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
    record.well.authored_background = back_color.read();
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
