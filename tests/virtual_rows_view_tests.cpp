// VirtualRowsView (CHAT_STRESS_SPEC section 4.6): owner-drawn rows of
// varying height over the height index. Refinement invariants (the total
// height is every measured height plus the estimate for the rest, and only
// rows near the viewport are measured), anchor stability through
// refinement, edits, insertions, erasures and a width change, scrolling to
// an index in every alignment, a million rows with bounded work per settle
// and per paint (counted, not timed), the keyboard, accessibility, right to
// left, refusals, following the tail and the coalesced announcement of
// appended rows. Heights are whole numbers so every sum is exact.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/virtual_rows_view.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/layout/types/layout_types.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
};

// A fixed linear congruential sequence, so a failure reproduces.
struct Sequence final {
    std::uint64_t state{0x9E3779B97F4A7C15ULL};

    std::size_t next(std::size_t bound) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::uint64_t high = state >> 33U;
        const std::size_t value = static_cast<std::size_t>(high % static_cast<std::uint64_t>(bound));
        return value;
    }
};

constexpr double estimate = 30.0;
constexpr double narrow_extra = 4.0;
constexpr double narrow_width = 450.0;

// The application's rows: heights from the row number and the width (a
// narrower column wraps into taller rows), a counted paint and a description.
class SyntheticRows final {
public:
    // 10 to 34 units, plus 4 below 450 units of width, plus `shift`.
    [[nodiscard]] double height_for(std::size_t row, double width) const {
        const double base = 10.0 + static_cast<double>((row * 7U) % 5U) * 6.0;
        const double wrapped = width < narrow_width ? base + narrow_extra : base;
        const double height = wrapped + shift;
        return height;
    }

    void measure(VirtualRowMeasure& request) {
        ++measured;
        request.height = height_for(request.row.value, request.width);
    }

    void paint(VirtualRowPaint& request) {
        ++painted;
        if (request.painter == nullptr) null_painter = true;
        if (request.mirrored) mirrored_seen = true;
        if (try_reentry && view != nullptr) {
            try {
                (*view).reset_rows(3U);
            } catch (const std::logic_error&) {
                reentry_refused = true;
            }
        }
    }

    void describe(VirtualRowDescription& request) {
        ++described;
        request.name.append("Row ");
        request.name.append(std::to_string(request.row.value));
        request.value.append("Message text of row ");
        request.value.append(std::to_string(request.row.value));
    }

    [[nodiscard]] VirtualRowsCallbacks callbacks() {
        VirtualRowsCallbacks bound{};
        bound.measure = Delegate<VirtualRowMeasure&>::bind<SyntheticRows, &SyntheticRows::measure>(*this);
        bound.paint = Delegate<VirtualRowPaint&>::bind<SyntheticRows, &SyntheticRows::paint>(*this);
        bound.describe = Delegate<VirtualRowDescription&>::bind<SyntheticRows, &SyntheticRows::describe>(*this);
        return bound;
    }

    double shift{};
    std::size_t measured{};
    std::size_t painted{};
    std::size_t described{};
    bool null_painter{};
    bool mirrored_seen{};
    bool try_reentry{};
    bool reentry_refused{};
    VirtualRowsView* view{};
};

struct RecordSelection final {
    std::vector<std::optional<std::size_t>>* seen{};
    void operator()(const VirtualRowSelection& change) const {
        std::optional<std::size_t> current{};
        if (change.current.has_value()) current = (*change.current).value;
        (*seen).push_back(current);
    }
};

struct RecordActivation final {
    std::vector<std::size_t>* seen{};
    void operator()(VirtualRowIndex row) const { (*seen).push_back(row.value); }
};

std::shared_ptr<VirtualRowsView> make_view(SyntheticRows& rows, std::size_t count) {
    std::shared_ptr<VirtualRowsView> view = make_control<VirtualRowsView>(StableId("rows"));
    (*view).set_estimated_row_height(estimate);
    (*view).set_row_callbacks(rows.callbacks());
    (*view).reset_rows(count);
    rows.view = view.get();
    return view;
}

std::optional<VirtualRowIndex> some_row(std::size_t value) {
    const std::optional<VirtualRowIndex> row = VirtualRowIndex{value};
    return row;
}

bool key(Window& window, std::uint32_t physical) {
    KeyEvent event{KeyAction::down, physical};
    const bool handled = window.dispatch_key(event);
    return handled;
}

bool wheel(Window& window, double lines) {
    PointerEvent event{};
    event.action = PointerAction::wheel;
    event.position = {100.0, 100.0};
    event.wheel_delta = {0.0, lines};
    const bool handled = window.dispatch_pointer(event);
    return handled;
}

// The total height every row contributes: measured rows their own height
// at the current width, the rest the estimate.
double expected_total(const VirtualRowsView& view, const SyntheticRows& rows, std::size_t& measured_rows) {
    const double width = view.viewport_bounds().width;
    double total = 0.0;
    measured_rows = 0U;
    for (std::size_t row = 0; row < view.row_count(); ++row) {
        if (view.row_measured(VirtualRowIndex{row})) {
            ++measured_rows;
            total += rows.height_for(row, width);
        } else {
            total += estimate;
        }
    }
    return total;
}

void refinement_invariants() {
    SyntheticRows rows;
    std::shared_ptr<VirtualRowsView> view = make_view(rows, 20'000U);
    Window window(view, {400.0, 300.0});
    window.perform_layout();
    require((*view).viewport_bounds().width == 383.0 && (*view).viewport_bounds().height == 300.0,
            "the viewport is the client less the scroll bar's column");
    std::size_t measured_rows = 0U;
    double total = expected_total(*view, rows, measured_rows);
    require((*view).content_height() == total, "the total is the measured heights plus the estimate for the rest");
    require(measured_rows == rows.measured && measured_rows > 0U && measured_rows <= 100U,
            "startup measures only the rows near the viewport, each once");
    require((*view).row_measured(VirtualRowIndex{0U}) && !(*view).row_measured(VirtualRowIndex{1'000U}),
            "rows never shown stay estimated");
    (*view).scroll_to_offset((*view).scroll_offset());
    require((*view).statistics().last_settle_measured == 0U && rows.measured == measured_rows,
            "a settle with nothing new measures nothing");

    Sequence sequence;
    for (std::size_t step = 0; step < 40U; ++step) {
        const double target = static_cast<double>(sequence.next(500'000U));
        (*view).scroll_to_offset(target);
        total = expected_total(*view, rows, measured_rows);
        require((*view).content_height() == total, "every refinement keeps the total exact");
        require(measured_rows == rows.measured, "no row is measured twice at one width");
        const VirtualRowIndex first = (*view).first_visible_row();
        const VirtualRowIndex end = (*view).end_visible_row();
        require(first < end && (*view).row_measured(first) &&
                    (*view).row_measured(VirtualRowIndex{end.value - 1U}),
                "every visible row is measured");
        const Rect top = (*view).row_bounds(first);
        require(top.y <= 0.0 && top.y + top.height > 0.0, "the first visible row meets the viewport's top");
    }
}

void anchor_stability() {
    SyntheticRows rows;
    std::shared_ptr<VirtualRowsView> view = make_view(rows, 100'000U);
    Window window(view, {400.0, 300.0});
    window.perform_layout();
    const std::uint64_t corrections = (*view).statistics().anchor_corrections;
    (*view).scroll_to_row(VirtualRowIndex{50'000U}, VirtualRowAlignment::start);
    require((*view).row_bounds(VirtualRowIndex{50'000U}).y == 0.0,
            "a jumped-to row lands at the top though the rows above it were refined");
    require((*view).row_measured(VirtualRowIndex{49'999U}) &&
                (*view).statistics().anchor_corrections > corrections,
            "the overscan above the anchor was measured and the offset corrected");

    // An edit of measured rows above the viewport: they are measured again
    // with new heights, and the anchor stays put.
    rows.shift = 7.0;
    // Rows 49,997 to 49,999 are at most 45 units tall each: all three lie in
    // the 150-unit overscan above the anchor.
    (*view).refresh_rows(VirtualRowIndex{49'997U}, 3U);
    window.perform_layout();
    require((*view).row_measured(VirtualRowIndex{49'998U}) &&
                (*view).row_bounds(VirtualRowIndex{49'998U}).height ==
                    rows.height_for(49'998U, (*view).viewport_bounds().width),
            "refreshed rows in the overscan are measured again");
    require((*view).row_bounds(VirtualRowIndex{50'000U}).y == 0.0, "an edit above the anchor does not move it");
    rows.shift = 0.0;

    // Insertions and erasures above the anchor move its index, not its place.
    (*view).insert_rows(VirtualRowIndex{0U}, 1'000U);
    window.perform_layout();
    require((*view).row_count() == 101'000U && (*view).row_bounds(VirtualRowIndex{51'000U}).y == 0.0,
            "rows inserted above keep the anchor where it was");
    (*view).erase_rows(VirtualRowIndex{10U}, 500U);
    window.perform_layout();
    require((*view).row_bounds(VirtualRowIndex{50'500U}).y == 0.0, "rows erased above keep the anchor where it was");
    // Erasing the anchor row itself: the next row takes its place.
    (*view).erase_rows(VirtualRowIndex{50'500U}, 3U);
    window.perform_layout();
    require((*view).row_bounds(VirtualRowIndex{50'500U}).y == 0.0, "the row after an erased anchor takes its place");

    // A wider column forgets every measured height (the rows rewrap) and
    // measures again around the anchor.
    const std::size_t before = rows.measured;
    window.resize({500.0, 300.0});
    window.perform_layout();
    require((*view).viewport_bounds().width == 483.0 && rows.measured > before,
            "a new width measures the rows in view again");
    require((*view).row_bounds(VirtualRowIndex{50'500U}).y == 0.0 &&
                (*view).row_bounds(VirtualRowIndex{50'500U}).height == rows.height_for(50'500U, 483.0),
            "the anchor keeps its place across a width change, at its new height");
    require(!(*view).row_measured(VirtualRowIndex{10'000U}), "heights measured at the old width are forgotten");

    // Following the tail, at the end the anchor is the end: appended rows
    // keep the view at the bottom.
    (*view).set_follow_tail(true);
    const std::size_t last = (*view).row_count() - 1U;
    (*view).scroll_to_row(VirtualRowIndex{last}, VirtualRowAlignment::end);
    require((*view).scroll_offset() == (*view).content_height() - 300.0, "the last row's bottom is at the bottom");
    (*view).insert_rows(VirtualRowIndex{(*view).row_count()}, 10U);
    window.perform_layout();
    require((*view).scroll_offset() == (*view).content_height() - 300.0 &&
                (*view).row_measured(VirtualRowIndex{(*view).row_count() - 1U}),
            "appended rows are measured and the view follows the end");
}

void scroll_to_index() {
    SyntheticRows rows;
    std::shared_ptr<VirtualRowsView> view = make_view(rows, 10'000U);
    Window window(view, {400.0, 300.0});
    window.perform_layout();
    const VirtualRowIndex target{777U};
    (*view).scroll_to_row(target, VirtualRowAlignment::start);
    Rect bounds = (*view).row_bounds(target);
    require(bounds.y == 0.0 && (*view).row_measured(target), "start puts the row's top at the top");
    (*view).scroll_to_row(target, VirtualRowAlignment::center);
    bounds = (*view).row_bounds(target);
    require(bounds.y == (300.0 - bounds.height) / 2.0, "center puts the row's middle at the middle");
    (*view).scroll_to_row(target, VirtualRowAlignment::end);
    bounds = (*view).row_bounds(target);
    require(bounds.y + bounds.height == 300.0, "end puts the row's bottom at the bottom");
    const double offset = (*view).scroll_offset();
    (*view).scroll_to_row(target, VirtualRowAlignment::nearest);
    require((*view).scroll_offset() == offset, "nearest leaves a row in view where it is");
    (*view).scroll_to_row(VirtualRowIndex{5'000U}, VirtualRowAlignment::nearest);
    bounds = (*view).row_bounds(VirtualRowIndex{5'000U});
    require(bounds.y + bounds.height == 300.0, "nearest brings a row from below to the bottom");
    (*view).scroll_to_row(VirtualRowIndex{4'000U}, VirtualRowAlignment::nearest);
    require((*view).row_bounds(VirtualRowIndex{4'000U}).y == 0.0, "nearest brings a row from above to the top");
    (*view).scroll_to_row(VirtualRowIndex{0U}, VirtualRowAlignment::end);
    require((*view).scroll_offset() == 0.0, "the first row clamps to the top");
    bool threw = false;
    try {
        (*view).scroll_to_row(VirtualRowIndex{10'000U}, VirtualRowAlignment::start);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "a row past the end is refused");
    // A jump before the first layout lands once the view is laid out.
    SyntheticRows early_rows;
    std::shared_ptr<VirtualRowsView> early = make_view(early_rows, 10'000U);
    (*early).scroll_to_row(VirtualRowIndex{6'000U}, VirtualRowAlignment::start);
    Window early_window(early, {400.0, 300.0});
    early_window.perform_layout();
    require((*early).row_bounds(VirtualRowIndex{6'000U}).y == 0.0, "a jump before layout is kept by the anchor");
}

// A million rows: each settle and each paint does work bounded by the
// viewport, whatever the row count. Counted: rows measured per settle, rows
// painted per paint, the index's runs and depth.
void million_rows_bounded_work() {
    SyntheticRows rows;
    std::shared_ptr<VirtualRowsView> view = make_view(rows, 1'000'000U);
    Window window(view, {400.0, 300.0});
    window.perform_layout();
    // Rows are at least 14 units tall at this width: a band of 600 units
    // holds at most 44 rows; refinement passes may measure rows a pass's
    // band met before the offset settled, so the bound allows twice that.
    constexpr std::size_t settle_bound = 100U;
    constexpr std::size_t paint_bound = 300U / 14U + 2U;
    require(rows.measured > 0U && rows.measured <= settle_bound && (*view).statistics().rows_measured == rows.measured,
            "startup measures a viewport's rows, not a million");
    require((*view).statistics().index_runs <= 2U * rows.measured + 1U, "the index holds runs, not rows");
    RecordingPainter painter;
    std::size_t painted = rows.painted;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 400.0, 300.0}));
    require(rows.painted > painted && rows.painted - painted <= paint_bound && !rows.null_painter,
            "a paint draws only the visible rows");

    Sequence sequence;
    std::size_t operations = 0U;
    for (std::size_t step = 0; step < 300U; ++step) {
        const std::size_t kind = step % 3U;
        if (kind == 0U) {
            const VirtualRowIndex target{sequence.next(1'000'000U)};
            (*view).scroll_to_row(target, VirtualRowAlignment::start);
            require((*view).row_bounds(target).y == 0.0, "every jump lands on its row");
        } else if (kind == 1U) {
            require(wheel(window, -2.0), "the wheel scrolls the view");
        } else {
            (*view).scroll_to_offset((*view).scroll_offset() + 2'500.0);
        }
        ++operations;
        window.perform_layout();
        const VirtualRowsStatistics counted = (*view).statistics();
        require(counted.last_settle_measured <= settle_bound &&
                    counted.last_settle_passes <= VirtualRowsView::maximum_settle_passes,
                "each settle measures a bounded number of rows");
        painted = rows.painted;
        static_cast<void>(window.paint(painter, {0.0, 0.0, 400.0, 300.0}));
        require(rows.painted - painted <= paint_bound, "each paint draws a bounded number of rows");
        require(counted.index_depth <= 17U, "the index stays within its depth bound");
    }
    const VirtualRowsStatistics counted = (*view).statistics();
    require(rows.measured <= settle_bound * (operations + 1U) && counted.rows_measured == rows.measured,
            "the whole walk measured at most a bounded number of rows per operation");
    require(counted.index_runs <= 2U * rows.measured + 1U, "the index's runs grow only with the measured rows");
    (*view).scroll_to_row(VirtualRowIndex{999'999U}, VirtualRowAlignment::end);
    require((*view).end_visible_row().value == 1'000'000U, "the last of a million rows is reachable");
}

void keyboard_and_activation() {
    SyntheticRows rows;
    std::shared_ptr<VirtualRowsView> view = make_view(rows, 5'000U);
    Window window(view, {400.0, 300.0});
    window.perform_layout();
    std::vector<std::optional<std::size_t>> selections;
    std::vector<std::size_t> activations;
    SubscriptionToken selected = (*view).selection_changed().subscribe(RecordSelection{&selections});
    SubscriptionToken activated = (*view).row_activated().subscribe(RecordActivation{&activations});
    require(window.request_focus(view), "the view takes focus");
    require(key(window, PhysicalKey::down) && (*view).selected_row() == some_row(0U),
            "Down with no selection selects the first visible row");
    require(key(window, PhysicalKey::down) && (*view).selected_row() == some_row(1U),
            "Down moves the selection a row");
    require(key(window, PhysicalKey::page_down) && (*(*view).selected_row()).value > 5U,
            "Page Down moves the selection by a viewport");
    require(key(window, PhysicalKey::end) && (*view).selected_row() == some_row(4'999U) &&
                (*view).end_visible_row().value == 5'000U,
            "End selects and reveals the last row");
    require(key(window, PhysicalKey::up) && (*view).selected_row() == some_row(4'998U),
            "Up moves the selection back a row");
    require(key(window, PhysicalKey::home) && (*view).selected_row() == some_row(0U) &&
                (*view).scroll_offset() == 0.0,
            "Home selects and reveals the first row");
    require(key(window, PhysicalKey::enter) && activations == std::vector<std::size_t>({0U}),
            "Enter activates the selected row");
    require(selections.size() == 6U && selections.back() == std::optional<std::size_t>(0U),
            "every selection change is raised once");

    // The selection follows its row through insertions and is dropped with
    // it by an erasure.
    (*view).set_selected_row(VirtualRowIndex{10U});
    (*view).insert_rows(VirtualRowIndex{0U}, 5U);
    require((*view).selected_row() == some_row(15U), "an insertion above moves the selection");
    const std::size_t raised = selections.size();
    (*view).erase_rows(VirtualRowIndex{14U}, 3U);
    require(!(*view).selected_row().has_value() && selections.size() == raised + 1U &&
                !selections.back().has_value(),
            "erasing the selected row unselects it and says so");
    static_cast<void>(selected);
    static_cast<void>(activated);
}

void accessibility() {
    SyntheticRows rows;
    std::shared_ptr<VirtualRowsView> view = make_view(rows, 1'000'000U);
    (*view).accessible_name = "Messages";
    Window window(view, {400.0, 300.0});
    window.perform_layout();
    (*view).set_selected_row(VirtualRowIndex{2U});
    const SemanticDescriptor descriptor = (*view).semantic_descriptor();
    require(descriptor.role == SemanticRole::list && descriptor.name == "Messages" &&
                descriptor.row_count == 1'000'000U && descriptor.value == "Row 2",
            "the view is a named list sized by every row, its value the selected row's name");
    std::vector<SemanticNode> children = (*view).semantic_virtual_children();
    const std::size_t realized = (*view).end_visible_row().value - (*view).first_visible_row().value;
    require(children.size() == realized + 1U && realized > 0U && realized <= 300U / 14U + 2U,
            "only the realized rows are children, with the live region after them");
    require(children.back().role == SemanticRole::live_region && children.back().stable_id == "rows.announcements" &&
                children.back().name.empty(),
            "the live region is present, silent before any announcement");
    require(children[0].role == SemanticRole::list_item && children[0].stable_id == "rows.row-0" &&
                children[0].name == "Row 0" && children[0].value == "Message text of row 0" &&
                children[0].position_in_set == 1U && children[0].set_size == 1'000'000U,
            "a row is a list item named by the describe target, placed in the whole set");
    require(has_semantic_state(children[2].states, SemanticState::selected) &&
                !has_semantic_state(children[1].states, SemanticState::selected),
            "the selected row says so");
    require((*view).realized_item_count() == realized && (*view).realized_item_stable_id(1U) == "rows.row-1",
            "the realized items match the semantic children");
    std::vector<std::size_t> activations;
    SubscriptionToken activated = (*view).row_activated().subscribe(RecordActivation{&activations});
    require((*view).on_semantic_child_action("rows.row-4", SemanticAction::select, {}) &&
                (*view).selected_row() == some_row(4U),
            "the select action selects the row");
    require((*view).on_semantic_child_action("rows.row-5", SemanticAction::press, {}) &&
                activations == std::vector<std::size_t>({5U}),
            "the press action activates the row");
    require(!(*view).on_semantic_child_action("rows.row-05", SemanticAction::select, {}) &&
                !(*view).on_semantic_child_action("rows.row-1000000", SemanticAction::select, {}) &&
                !(*view).on_semantic_child_action("rows.row-", SemanticAction::select, {}) &&
                !(*view).on_semantic_child_action("other.row-1", SemanticAction::select, {}) &&
                !(*view).on_semantic_child_action("rows.row-6", SemanticAction::expand, {}),
            "malformed, unknown and unsupported child actions are refused");
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    require(snapshot.node_count > realized, "the window publishes the rows in its snapshot");
    (*view).scroll_to_row(VirtualRowIndex{777'777U}, VirtualRowAlignment::start);
    children = (*view).semantic_virtual_children();
    require(!children.empty() && children[0].stable_id == "rows.row-777777" &&
                children[0].position_in_set == 777'778U,
            "after a jump the children are the rows in view");
    static_cast<void>(activated);
}

void right_to_left() {
    SyntheticRows rows;
    std::shared_ptr<VirtualRowsView> view = make_view(rows, 1'000U);
    (*view).layout_direction = LayoutDirection::right_to_left;
    Window window(view, {400.0, 300.0});
    window.perform_layout();
    const VirtualRowsView::Parts parts = (*view).parts();
    require(parts.vertical_scroll_bar.present && parts.vertical_scroll_bar.bounds.x == 0.0 &&
                (*view).viewport_bounds().x == 17.0,
            "right to left the scroll bar is at the left edge");
    require((*view).row_bounds(VirtualRowIndex{0U}).x == 17.0, "rows fill the viewport");
    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 400.0, 300.0}));
    require(rows.mirrored_seen, "the paint target is told the rows are mirrored");
}

void refusals_and_reentrancy() {
    SyntheticRows rows;
    std::shared_ptr<VirtualRowsView> view = make_view(rows, 100U);
    Window window(view, {400.0, 300.0});
    window.perform_layout();
    bool threw = false;
    try {
        (*view).reset_rows(VirtualRowsView::maximum_rows + 1U);
    } catch (const std::length_error&) {
        threw = true;
    }
    require(threw && (*view).row_count() == 100U, "too many rows are refused and nothing changes");
    threw = false;
    try {
        (*view).erase_rows(VirtualRowIndex{90U}, 11U);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw && (*view).row_count() == 100U, "an erasure past the end is refused");
    threw = false;
    try {
        (*view).set_estimated_row_height(0.0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*view).estimated_row_height() == estimate, "an estimate below the minimum is refused");
    threw = false;
    try {
        (*view).scroll_to_offset(std::numeric_limits<double>::quiet_NaN());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a scroll offset that is not finite is refused");
    rows.try_reentry = true;
    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 400.0, 300.0}));
    require(rows.reentry_refused && (*view).row_count() == 100U,
            "a target cannot change the rows while the view calls it");
    rows.try_reentry = false;
    // Empty targets: rows stay at the estimate and are named by position.
    (*view).set_row_callbacks(VirtualRowsCallbacks{});
    (*view).reset_rows(50U);
    window.perform_layout();
    require((*view).row_bounds(VirtualRowIndex{3U}).height == estimate, "without a measure target rows keep the estimate");
    const std::vector<SemanticNode> children = (*view).semantic_virtual_children();
    require(!children.empty() && children[0].name == "1", "without a describe target rows are named by position");
}

// Follow tail: at the end, appended rows keep the view at the end; a scroll
// away stops following, so appended rows leave the content still, and a
// return to the end resumes. Without follow_tail a view at the end keeps
// its bottom row anchored.
void follow_tail() {
    SyntheticRows rows;
    std::shared_ptr<VirtualRowsView> view = make_view(rows, 1'000U);
    Window window(view, {400.0, 300.0});
    window.perform_layout();

    (*view).scroll_to_offset((*view).content_height());
    const double still = (*view).scroll_offset();
    require(!(*view).follow_tail() && !(*view).following_tail(), "follow_tail is off by default");
    (*view).insert_rows(VirtualRowIndex{(*view).row_count()}, 5U);
    window.perform_layout();
    require((*view).scroll_offset() == still && (*view).scroll_offset() < (*view).content_height() - 300.0,
            "without follow_tail appended rows arrive below the viewport");

    (*view).set_follow_tail(true);
    require(!(*view).following_tail(), "turning it on away from the end does not follow");
    (*view).scroll_to_offset((*view).content_height());
    require((*view).following_tail(), "reaching the end follows");
    for (std::size_t burst = 0; burst < 3U; ++burst) {
        (*view).insert_rows(VirtualRowIndex{(*view).row_count()}, 4U);
        window.perform_layout();
        require((*view).following_tail() && (*view).scroll_offset() == (*view).content_height() - 300.0,
                "appended rows keep a following view at the end, measured");
    }

    // A user scroll away (one wheel notch up) stops following.
    require(wheel(window, 1.0), "the wheel scrolls up");
    require(!(*view).following_tail(), "a scroll away from the end stops following");
    const double away = (*view).scroll_offset();
    const VirtualRowIndex top_row = (*view).first_visible_row();
    const double top_y = (*view).row_bounds(top_row).y;
    (*view).insert_rows(VirtualRowIndex{(*view).row_count()}, 20U);
    window.perform_layout();
    require((*view).scroll_offset() == away && (*view).row_bounds(top_row).y == top_y && !(*view).following_tail(),
            "while not following appended rows leave the content still");

    // The program's scroll to the last row returns to the end and resumes.
    (*view).scroll_to_row(VirtualRowIndex{(*view).row_count() - 1U}, VirtualRowAlignment::end);
    require((*view).following_tail(), "returning to the end resumes following");
    (*view).insert_rows(VirtualRowIndex{(*view).row_count()}, 2U);
    window.perform_layout();
    require((*view).scroll_offset() == (*view).content_height() - 300.0, "following again after the return");

    // Erasing rows at the end while following keeps the view at the end.
    (*view).erase_rows(VirtualRowIndex{(*view).row_count() - 3U}, 3U);
    window.perform_layout();
    require((*view).following_tail() && (*view).scroll_offset() == (*view).content_height() - 300.0,
            "erasing the last rows keeps a following view at the end");

    (*view).set_follow_tail(false);
    require(!(*view).following_tail(), "turning it off stops following at once");
}

// Appended rows are announced once per layout through the live region,
// coalesced, with the new total; insertions before the end and resets are
// not announced, and announces_appends turns it off.
void append_announcements() {
    SyntheticRows rows;
    std::shared_ptr<VirtualRowsView> view = make_view(rows, 100U);
    Window window(view, {400.0, 300.0});
    window.perform_layout();
    require((*view).announces_appends() && (*view).announcement().empty(), "nothing is announced at first");

    std::vector<SemanticNode> before = (*view).semantic_virtual_children();
    (*view).insert_rows(VirtualRowIndex{100U}, 3U);
    (*view).insert_rows(VirtualRowIndex{103U}, 1U);
    (*view).insert_rows(VirtualRowIndex{104U}, 2U);
    require((*view).announcement().empty(), "appends wait for the layout");
    window.perform_layout();
    require((*view).announcement() == "6 new rows, 106 in all", "three appends make one announcement");
    std::vector<SemanticNode> after = (*view).semantic_virtual_children();
    require(after.back().role == SemanticRole::live_region && after.back().name == "6 new rows, 106 in all" &&
                semantic_announcement_due(&before.back(), after.back()),
            "the live region is renamed, so hosts announce it politely");

    // A layout with nothing appended leaves the region alone.
    window.perform_layout();
    std::vector<SemanticNode> again = (*view).semantic_virtual_children();
    require(!semantic_announcement_due(&after.back(), again.back()), "no append, no announcement");

    // One row is singular; an equal count still renames by its total.
    (*view).insert_rows(VirtualRowIndex{106U}, 1U);
    window.perform_layout();
    require((*view).announcement() == "1 new row, 107 in all", "one appended row");

    // Insertions before the end are not announced.
    (*view).insert_rows(VirtualRowIndex{10U}, 5U);
    window.perform_layout();
    require((*view).announcement() == "1 new row, 107 in all", "an insertion before the end is not announced");

    // A reset drops a pending count.
    (*view).insert_rows(VirtualRowIndex{(*view).row_count()}, 9U);
    (*view).reset_rows(50U);
    window.perform_layout();
    require((*view).announcement() == "1 new row, 107 in all", "a reset is not announced");

    // Turned off, appends are not counted.
    (*view).set_announces_appends(false);
    (*view).insert_rows(VirtualRowIndex{50U}, 4U);
    window.perform_layout();
    require((*view).announcement() == "1 new row, 107 in all", "announces_appends off is silent");
    (*view).set_announces_appends(true);
    (*view).insert_rows(VirtualRowIndex{54U}, 2U);
    window.perform_layout();
    require((*view).announcement() == "2 new rows, 56 in all", "announcing again once turned back on");
}

} // namespace

int main() {
    refinement_invariants();
    anchor_stability();
    scroll_to_index();
    million_rows_bounded_work();
    keyboard_and_activation();
    accessibility();
    right_to_left();
    refusals_and_reentrancy();
    follow_tail();
    append_announcements();
    std::cout << "VirtualRowsView: refinement invariants, anchor stability, scrolling to an index, a million rows "
                 "with bounded work, keyboard, accessibility, right to left, refusals, follow tail and append announcements passed\n";
    return 0;
}
