#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/controls/list_box_values.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/event.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

class RowHeightIndex;
class VScrollBar;

// A row's position in a VirtualRowsView, from 0 to row_count() - 1. It is a
// position, not an identity: inserting or erasing rows before it moves the
// row to another position.
struct GUI_FORMS_API VirtualRowIndex final {
    std::size_t value{};

    friend bool operator==(VirtualRowIndex left, VirtualRowIndex right) noexcept {
        return left.value == right.value;
    }
    friend bool operator!=(VirtualRowIndex left, VirtualRowIndex right) noexcept {
        return left.value != right.value;
    }
    friend bool operator<(VirtualRowIndex left, VirtualRowIndex right) noexcept {
        return left.value < right.value;
    }
};

// A run of rows [first, first + count), count at least 1: one piece of an
// extended selection (VirtualRowsView::selected_ranges).
struct GUI_FORMS_API VirtualRowRange final {
    VirtualRowIndex first{};
    std::size_t count{};

    friend bool operator==(VirtualRowRange left, VirtualRowRange right) noexcept {
        return left.first == right.first && left.count == right.count;
    }
    friend bool operator!=(VirtualRowRange left, VirtualRowRange right) noexcept {
        return !(left == right);
    }
};

// Where scroll_to_row puts the row in the viewport: its top at the top, its
// middle at the middle, its bottom at the bottom, or the least scroll that
// shows it whole (none when it already is; a row taller than the viewport
// goes to the top).
enum class VirtualRowAlignment : std::uint8_t { start, center, end, nearest };

// The view asks for one row's height (VirtualRowsCallbacks::measure). `width`
// is the content width in logical units: the viewport less the scroll bar.
// `height` holds the view's estimate on entry; the handler replaces it with
// the row's height in logical units at that width. A height that is not
// finite is ignored (the row stays estimated and is asked again when next
// shown); one outside [minimum_row_height, maximum_row_height] is held at
// the nearer bound.
struct GUI_FORMS_API VirtualRowMeasure final {
    VirtualRowIndex row{};
    double width{};
    double height{};
};

// One row to draw (VirtualRowsCallbacks::paint). `painter` is borrowed for
// the call only and is never null in a call; `bounds` is the row's
// rectangle in the view's local coordinates, logical units, and may reach
// past the viewport (the painter is clipped to it). The view has already
// painted the row's plate by role (transparent, hot or selected) and paints
// the focus cue over the handler's drawing. Right to left, `mirrored` is set
// and the row's reading start is bounds' right edge. `view` (never null in a
// call) and `states` (the row item's anatomy states: selected, hot,
// current, disabled) let the handler draw its content as theme parts with
// paint_part(*painter, *view, part), so the row follows the theme.
struct GUI_FORMS_API VirtualRowPaint final {
    Painter* painter{};
    const Control* view{};
    VirtualRowIndex row{};
    Rect bounds{};
    AnatomyStateSet states{};
    bool selected{};
    bool hot{};
    bool focused{};
    bool enabled{};
    bool mirrored{};
};

// Text for one row's accessibility node (VirtualRowsCallbacks::describe).
// The view clears both strings, keeping their storage, before each call; the
// handler writes the row's name (what a screen reader says first, kept
// short) and its value (the full text, or empty). Invalid UTF-8 is replaced
// by an empty string.
struct GUI_FORMS_API VirtualRowDescription final {
    VirtualRowIndex row{};
    std::string name{};
    std::string value{};
};

// The application's row targets. Each is a Delegate: one object and one
// member function, bound by the application, never owned by the view. A
// bound target must outlive the view, or be replaced (set_row_callbacks
// with empty delegates) before it is destroyed. An empty measure keeps
// every row at the estimate; an empty paint draws the plates only; an empty
// describe names each row by its position.
//
// The view calls the targets on its own thread: measure during layout and
// scrolling, paint during paint, describe while semantics are read. A target
// must not change the view's rows, callbacks or scroll; those calls throw
// std::logic_error while a target runs, and leave the view unchanged.
struct GUI_FORMS_API VirtualRowsCallbacks final {
    Delegate<VirtualRowMeasure&> measure{};
    Delegate<VirtualRowPaint&> paint{};
    Delegate<VirtualRowDescription&> describe{};
};

// A change of the selection, after it happened: the current row before and
// after (the row the keyboard and the last press are at; in the single
// mode the selected row) and how many rows are selected now. Raised when
// the current row or the set of selected rows changes. A current row
// erased reports `current` none.
struct GUI_FORMS_API VirtualRowSelection final {
    std::optional<VirtualRowIndex> previous{};
    std::optional<VirtualRowIndex> current{};
    std::size_t selected_count{};
};

// A scroll, after it settled (VirtualRowsView::scrolled): the first row
// meeting the viewport, the scroll offset (logical units) and whether the
// view now follows the tail.
struct GUI_FORMS_API VirtualRowScroll final {
    VirtualRowIndex first_visible{};
    double offset{};
    bool following_tail{};
};

// Work counters, for bounded-work checks and workload records. The totals
// only grow; the "last" figures describe the most recent settle (a layout
// or a scroll) and paint. Every field is O(1) to read. index_runs is the
// height index's packed entries plus its dense rows, index_bytes the heap
// it holds (its pages and its fixed edit scratch), index_measured the rows
// holding a measured height. rows_forgotten counts measured rows the
// budget coarsened in budget_compactions compactions
// (set_measured_row_budget);
// partial_repaints counts invalidate_rows calls that repainted only the
// rows' rectangles.
struct GUI_FORMS_API VirtualRowsStatistics final {
    std::uint64_t rows_measured{};
    std::uint64_t rows_painted{};
    std::uint64_t rows_described{};
    std::uint64_t anchor_corrections{};
    std::uint64_t scroll_copies{};
    std::uint64_t scroll_repaints{};
    std::size_t last_settle_measured{};
    std::size_t last_settle_passes{};
    std::size_t last_paint_rows{};
    std::size_t index_runs{};
    std::size_t index_nodes{};
    std::size_t index_depth{};
    std::size_t index_bytes{};
    std::size_t index_measured{};
    std::uint64_t rows_forgotten{};
    std::uint64_t budget_compactions{};
    std::uint64_t partial_repaints{};
};

// A vertically scrolling list of a very large number of owner-drawn rows of
// varying height (CHAT_STRESS_SPEC section 4.6). Rows are not controls and
// the view keeps no row records: it keeps the row count and each row's
// height in a height-indexed sequence B-tree (RowHeightIndex, over
// stx_auxlib's stx_btree), and
// asks the application to measure, draw and describe the rows it shows.
//
// Heights. Every row starts at the estimate (estimated_row_height), so a
// million rows are one run of the index and startup measures nothing but
// the rows in view. A row is measured lazily, once, when it first comes
// within the viewport or the overscan band (half a viewport above and
// below); rows never shown stay estimated. A change of the content width
// forgets every measured height (the index is reassigned to the estimate)
// and measures again around the anchor; refresh_rows forgets chosen rows,
// and invalidate_rows forgets them, measures those near the viewport again
// at once and repaints them.
//
// Heights are kept in 1/64 logical units (a measured height is rounded to
// the nearest), and every offset is the exact sum of the heights above it.
//
// Memory. The index holds 4 bytes a measured row in dense stretches (8 in
// a mixed one) plus a page header a leaf. A measured-row budget
// (set_measured_row_budget, none by default) bounds it: after a settle that
// leaves more rows measured than the budget, the rows outside the keep
// window are coarsened: each side becomes one approximate run holding its
// exact total, so no offset in the keep window moves, the total and the
// scroll bar stay put, and the coarsened rows are measured again (as
// unmeasured rows) when they come back into the band. The keep window is
// the rows within budget / 2 of the first visible row on each side,
// widened to cover the settle's band (the viewport and its overscan),
// which is never coarsened; so at most budget + 1 rows plus the band stay
// measured.
//
// Anchoring. The anchor is the first row whose top is at or below the
// viewport's top (the row straddling the top when no later row starts in
// the viewport), with that top's distance below the viewport's top. When
// rows change height, are inserted or are erased above the anchor, the
// scroll offset moves by the same amount, so the anchor row stays where it
// is on screen and the content does not jump; the scroll bar's thumb may
// move.
//
// Follow tail (follow_tail, off by default; ScrollableControl has no such
// property, so the name is this view's). While it is on and the viewport
// is at the end of the content, the anchor is the end: the view stays at
// the bottom as rows are appended or measured (following_tail() is true).
// Scrolling away from the end, by the user or the program, stops following
// until the viewport returns to the end; following is derived from the
// position at each settle, never latched. With it off, a view at the end
// keeps its bottom row anchored like any other, so appended rows arrive
// below the viewport.
//
// Bounded work. A settle measures at most maximum_measured_per_settle rows
// and repeats its measure-and-place step at most maximum_settle_passes
// times; a paint draws only the rows meeting the viewport, at most the
// viewport's height over minimum_row_height plus one. Index operations are
// O(log n) (offset to row, row to offset, a re-measure, an insertion or an
// erasure). The paint and measure paths allocate nothing per row; the
// semantic children are built per read (Control's interface returns them
// by value), one node per realized row.
//
// Scrolling: the wheel (lines scroll by small_change, pixels exactly), the
// child scroll bar, and the keys below. An offset change of whole device
// pixels moves the painted pixels with a scroll copy (scroll_content) and
// repaints only the exposed band; otherwise the viewport is repainted.
//
// scrolled is raised after a scroll, a change of the rows or a layout moved
// the offset or the first visible row, once the view is settled; when one
// call raises both, scrolled comes before selection_changed.
//
// Selection (selection_mode, ListBox's modes and conventions). In
// ListSelectionMode::one a row is selected or none. In multiple_extended
// the selection is a set of rows kept as sorted, disjoint, non-adjacent
// ranges (selected_ranges), with a current row and an anchor: a press
// selects its row alone and sets the anchor, Shift with a press or a
// navigation key selects the rows from the anchor to the new row, Ctrl (or
// Command) with a press toggles its row and sets the anchor there, and a
// drag from a press extends from the anchor to the row under the pointer
// (with the pointer above or below the viewport, to the row just past that
// edge, which scrolls into view: the view scrolls a row per pointer move).
// Ctrl+Space toggles the current row.
// Inserted rows are unselected; erased rows leave the selection.
//
// Keyboard (focused): Up and Down move the current row by a row, Page Up
// and Page Down by a viewport, Home and End (with or without Ctrl) to the
// first and last rows, each scrolling it into view; Enter and Space
// activate the current row (row_activated). A double click activates the
// row pressed.
//
// Accessibility: a list whose size is row_count() and whose children are the
// realized rows only, each a list item named by describe, with its position
// in the set, its selection and the focus, select, and press actions (select
// selects that row alone); the list is multiselectable in the extended
// mode. After
// the rows comes one live region, "<id>.announcements", always present and
// named by the latest announcement: rows appended at the end (insert_rows
// at row_count()) are counted and announced once per settle as "<n> new
// rows, <total> in all", never one announcement per row; hosts announce it
// politely when its name changes. announces_appends (on by default) turns
// this off; reset_rows and insertions before the end are not announced.
//
// Right to left (layout_direction or right_to_left), the scroll bar is at
// the left edge and the paint context reports mirrored rows.
//
// One thread. The view owns its index and its scroll bar; the callbacks are
// borrowed (see VirtualRowsCallbacks).
class GUI_FORMS_API VirtualRowsView final : public Control {
public:
    static constexpr bool initialize_tree_after_construction = true;
    // Development bounds, not measured capacity.
    static constexpr std::size_t maximum_rows = 1'000'000'000U;
    static constexpr double minimum_row_height = 1.0;
    static constexpr double maximum_row_height = 1'000'000.0;
    static constexpr double default_estimated_row_height = 24.0;
    static constexpr std::size_t maximum_measured_per_settle = 4096U;
    static constexpr std::size_t maximum_settle_passes = 8U;

    explicit VirtualRowsView(StableId stable_id);
    ~VirtualRowsView() override;
    VirtualRowsView(const VirtualRowsView&) = delete;
    VirtualRowsView& operator=(const VirtualRowsView&) = delete;
    void initialize_control_tree();

    // -- rows ---------------------------------------------------------------
    // Replaces the targets. Measured heights are kept (call refresh_rows when
    // the new targets measure differently).
    void set_row_callbacks(VirtualRowsCallbacks callbacks);
    [[nodiscard]] const VirtualRowsCallbacks& row_callbacks() const noexcept { return callbacks_; }

    // Replaces every row with `count` estimated rows, scrolls to the top and
    // clears the selection (raising selection_changed when there was one).
    // Throws std::length_error for more than maximum_rows rows.
    void reset_rows(std::size_t count);
    [[nodiscard]] std::size_t row_count() const noexcept;
    // Inserts `count` estimated rows before `at` (at most row_count()). The
    // selection and the anchor follow their rows. Throws std::out_of_range
    // for `at` past the end, std::length_error past maximum_rows.
    void insert_rows(VirtualRowIndex at, std::size_t count);
    // Erases rows [at, at + count). A selected row among them is unselected
    // (raising selection_changed); an anchor among them moves to the first
    // row after them. Throws std::out_of_range for a range past the end.
    void erase_rows(VirtualRowIndex at, std::size_t count);
    // Forgets the measured heights of rows [first, first + count) (their
    // content changed): they return to the estimate and are measured again
    // when shown. Throws std::out_of_range for a range past the end.
    void refresh_rows(VirtualRowIndex first, std::size_t count);
    // Rows whose content changed, keeping their count: forgets their
    // measured heights, measures again at once those in the viewport's band
    // (so the view keeps its anchor), and repaints them. When none of them
    // in view changed height, only their rectangles are repainted;
    // otherwise the viewport is. Throws std::out_of_range for a range past
    // the end.
    void invalidate_rows(VirtualRowIndex first, std::size_t count);

    // The most rows kept measured (see the class comment); none, the
    // default, keeps every measured row. Takes effect at the next settle,
    // which a call starts.
    void set_measured_row_budget(std::optional<std::size_t> rows);
    [[nodiscard]] std::optional<std::size_t> measured_row_budget() const noexcept { return measured_budget_; }

    // The height unmeasured rows take. Changing it forgets every measured
    // height. Throws std::invalid_argument outside [minimum_row_height,
    // maximum_row_height] or for a value that is not finite.
    void set_estimated_row_height(double height);
    [[nodiscard]] double estimated_row_height() const noexcept;
    // Whether a row has been measured at the current width; false past the
    // end.
    [[nodiscard]] bool row_measured(VirtualRowIndex row) const noexcept;

    // -- scrolling ----------------------------------------------------------
    // The viewport's top in content coordinates, logical units.
    [[nodiscard]] double scroll_offset() const noexcept { return offset_; }
    // Every row's height, measured or estimated.
    [[nodiscard]] double content_height() const noexcept;
    // The viewport (local coordinates): the client area less the scroll bar.
    [[nodiscard]] Rect viewport_bounds() const noexcept { return viewport_; }
    // Scrolls so the content at `offset` is at the viewport's top, clamped to
    // the content, then measures the rows the new viewport shows and keeps
    // the row there anchored. Throws std::invalid_argument for an offset
    // that is not finite.
    void scroll_to_offset(double offset);
    // Scrolls a row into view, measuring it and its neighbours first so it
    // lands where asked. Throws std::out_of_range for a row past the end.
    void scroll_to_row(VirtualRowIndex row, VirtualRowAlignment alignment);
    // The rows meeting the viewport, [first, end); empty without rows or
    // before the first layout.
    [[nodiscard]] VirtualRowIndex first_visible_row() const noexcept;
    [[nodiscard]] VirtualRowIndex end_visible_row() const noexcept;
    // A row's rectangle in local coordinates (it may lie outside the
    // viewport). Throws std::out_of_range for a row past the end.
    [[nodiscard]] Rect row_bounds(VirtualRowIndex row) const;
    // The row under a local point inside the viewport, or none.
    [[nodiscard]] std::optional<VirtualRowIndex> row_at(Point local_point) const noexcept;

    // Whether a view at the end of its content stays there as rows are
    // appended (see the class comment). Turning it on while at the end
    // starts following at once.
    void set_follow_tail(bool follow);
    [[nodiscard]] bool follow_tail() const noexcept { return follow_tail_; }
    // Whether the view is following now: follow_tail() and the viewport at
    // the end at the last settle or scroll.
    [[nodiscard]] bool following_tail() const noexcept { return anchored_at_end_; }

    // -- announcements ------------------------------------------------------
    // Whether appended rows are announced through the live region. Turning
    // it off drops a pending count; the last announcement stays.
    void set_announces_appends(bool announce);
    [[nodiscard]] bool announces_appends() const noexcept { return announces_appends_; }
    // The live region's current text; empty before the first announcement.
    // Valid until the next layout.
    [[nodiscard]] std::string_view announcement() const& noexcept { return announcement_; }
    std::string_view announcement() const&& = delete;

    // -- selection ----------------------------------------------------------
    // Switching to ListSelectionMode::one keeps the current row selected
    // alone (raising selection_changed when that changes the set).
    void set_selection_mode(ListSelectionMode mode);
    [[nodiscard]] ListSelectionMode selection_mode() const noexcept { return selection_mode_; }
    // The current row: in the single mode the selected row; in the extended
    // mode the row the keyboard and the last press are at, which Ctrl may
    // have unselected.
    [[nodiscard]] std::optional<VirtualRowIndex> selected_row() const noexcept;
    // Makes a row current and selects it alone (none: clears the
    // selection), setting the anchor, and raises selection_changed when
    // that changes anything; does not scroll. Throws std::out_of_range for
    // a row past the end.
    void set_selected_row(std::optional<VirtualRowIndex> row);
    // The selected rows, sorted, disjoint and non-adjacent; valid until the
    // next change of the rows or the selection.
    [[nodiscard]] std::span<const VirtualRowRange> selected_ranges() const& noexcept { return ranges_; }
    std::span<const VirtualRowRange> selected_ranges() const&& = delete;
    // Whether a row is selected (false past the end); O(log ranges).
    [[nodiscard]] bool row_selected(VirtualRowIndex row) const noexcept;
    // O(ranges).
    [[nodiscard]] std::size_t selected_row_count() const noexcept;
    // Selects the rows from `anchor` to `current` inclusive, in either
    // order, alone; `current` becomes the current row and `anchor` the
    // anchor. Throws std::logic_error in the single mode and
    // std::out_of_range for a row past the end.
    void select_range(VirtualRowIndex anchor, VirtualRowIndex current);
    // Unselects every row; the current row stays.
    void clear_selection();

    [[nodiscard]] VirtualRowsStatistics statistics() const noexcept;

    [[nodiscard]] Event<const VirtualRowSelection&>& selection_changed() noexcept { return selection_changed_; }
    [[nodiscard]] Event<VirtualRowIndex>& row_activated() noexcept { return row_activated_; }
    [[nodiscard]] Event<const VirtualRowScroll&>& scrolled() noexcept { return scrolled_; }

    // -- Control ------------------------------------------------------------
    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_focus_changed(bool focused) override;
    [[nodiscard]] SemanticDescriptor semantic_descriptor() const override;
    [[nodiscard]] std::vector<SemanticNode> semantic_virtual_children() const override;
    bool on_semantic_child_action(std::string_view stable_id, SemanticAction action,
                                  std::string_view value) override;
    [[nodiscard]] const ControlAnatomy& anatomy() const noexcept override;
    void append_interactions(std::vector<InteractionDeclaration>& interactions) const override;
    [[nodiscard]] std::size_t realized_item_count() const override;
    [[nodiscard]] std::string realized_item_stable_id(std::size_t item) const override;
    [[nodiscard]] AnatomyStateSet realized_item_anatomy_states(std::size_t item) const override;

    // The anatomy, as dot members (see Control::Parts).
    struct GUI_FORMS_API Parts final {
        PartGeometry region;
        PartGeometry well;
        PartGeometry vertical_scroll_bar;
        PartGeometry focus;
    };
    struct GUI_FORMS_API States final {
        bool normal{};
        bool enabled{};
        bool disabled{};
        bool focused{};
        bool focus_visible{};
    };
    // One realized row's parts and states (the "row" item system).
    struct GUI_FORMS_API RowParts final {
        PartGeometry row;
        PartGeometry row_face;
        PartGeometry row_focus;
    };
    struct GUI_FORMS_API RowStates final {
        bool selected{};
        bool hot{};
        bool current{};
        bool disabled{};
        bool focus_visible{};
    };
    [[nodiscard]] Parts parts() const;
    [[nodiscard]] States states() const;

protected:
    void publish_part_geometry(std::span<PartGeometry> geometry) const override;
    void publish_realized_item_geometry(std::size_t item, std::span<PartGeometry> geometry) const override;
    // The mouse raisers hand the target phase to handle_pointer and the
    // key raiser an unhandled key to handle_key; then the base raises.
    void on_mouse_down(MouseEventArgs& arguments) override;
    void on_mouse_move(MouseEventArgs& arguments) override;
    void on_mouse_up(MouseEventArgs& arguments) override;
    void on_mouse_capture_changed() override;
    void on_mouse_wheel(MouseEventArgs& arguments) override;
    void on_mouse_leave() override;
    void on_key_down(KeyEventArgs& arguments) override;

private:
    // The routed form of one pointer event or key on the view: hot row,
    // wheel scrolling, the primary press's selection and activation; the
    // navigation and activation keys.
    void handle_pointer(PointerEvent& event);
    void handle_key(KeyEvent& event);
    // A drag's move: extends the selection to the row under the pointer
    // (see the class comment).
    void drag_to(Point local);
    // Raised targets run under this guard: the view refuses changes to its
    // rows, callbacks and scroll until it ends (VirtualRowsCallbacks).
    class CallbackScope;

    // Throws std::logic_error while a target runs.
    void require_outside_callback() const;
    // Places the scroll bar and the viewport in `client` (local
    // coordinates), mirrored right to left. The bar's column is always
    // reserved, so the content width does not depend on the content.
    void place_parts(Rect client);
    // After a change of the rows, their heights or the callbacks: settles,
    // updates the realized rows and the scroll bar, and invalidates.
    void rows_changed();
    // The settle: on a width change forgets the heights; then, up to
    // maximum_settle_passes times, places the offset from the anchor and
    // measures the unmeasured rows of the viewport and its overscan band,
    // until a pass stores no height (or maximum_measured_per_settle rows
    // were asked); then places the offset once more and takes the anchor
    // from it. Returns whether every row of the band is measured.
    bool settle();
    // Asks for the unmeasured rows meeting [top, bottom) in content
    // coordinates while `asked` (counted up here) is below `budget`;
    // returns the heights stored. One cursor walks the band, and each
    // stretch of rows is stored in one batch when it ends, with the same
    // result as storing them a row at a time.
    std::size_t measure_band(double top, double bottom, std::size_t budget, std::size_t& asked);
    // Asks the measure target for one row's height (`current` is offered
    // as the starting value) and counts the request; the height clamped to
    // [minimum_row_height, maximum_row_height], or none for one that is not
    // finite.
    std::optional<double> request_row_height(std::size_t row, double current);
    // Asks the measure target for one row's height and records it; returns
    // whether a height was stored (false for a height that is not finite).
    bool measure_row(std::size_t row);
    // The offset the anchor asks for, clamped to [0, maximum_offset()].
    [[nodiscard]] double anchored_offset() const noexcept;
    // Records the anchor for the current offset.
    void capture_anchor() noexcept;
    [[nodiscard]] double maximum_offset() const noexcept;
    // A scroll: note_scroll_start records the first realized row's screen
    // top; finish_scroll settles at the new anchor, then moves the painted
    // pixels by what that row moved (scroll_content) or repaints.
    void note_scroll_start() noexcept;
    void finish_scroll();
    // Asks for an arrange when the bar must appear or disappear, else
    // brings its range and value up to date.
    void update_scroll_bar();
    void sync_scroll_bar();
    void on_vertical_scroll(double value);
    // How a press, a key or an action changes the selection.
    enum class SelectGesture : std::uint8_t { replace, extend, toggle };
    // Makes a row current, changes the selection by `gesture` (extend and
    // toggle only in the extended mode; replace otherwise), scrolls the row
    // into view (nearest) and raises selection_changed when anything
    // changed.
    void select_and_reveal(std::size_t row, SelectGesture gesture);
    // Selection storage: one range alone, a toggled row, the ranges moved
    // by an insertion or an erasure (returns whether a selected row was
    // erased). The vector's storage is reused.
    void select_only(std::size_t first, std::size_t count);
    void toggle_row(std::size_t row);
    void shift_selection_for_insert(std::size_t at, std::size_t count);
    bool shift_selection_for_erase(std::size_t at, std::size_t count);
    [[nodiscard]] bool selected_at(std::size_t row) const noexcept;
    void raise_selection(std::optional<std::size_t> previous);
    // Coarsens the rows outside the keep window when more than the budget
    // are measured (see the class comment); [band_first, band_end)
    // is the settle's band.
    void enforce_measured_budget(std::size_t band_first, std::size_t band_end);
    // Raises scrolled when the offset or the first visible row moved since
    // the last report.
    void report_scroll();
    void activate(std::size_t row);
    [[nodiscard]] Rect row_rectangle(std::size_t row) const noexcept;
    [[nodiscard]] AnatomyStateSet row_states(std::size_t row) const;
    // The realized rows: [first_realized_, end_realized_), the rows meeting
    // the viewport after the last settle.
    void update_realized() noexcept;
    // The row a virtual child's stable ID names, or none.
    [[nodiscard]] std::optional<std::size_t> row_of_child_id(std::string_view stable_id) const noexcept;
    // Writes "<view id>.row-<row>" into `identity`, reusing its storage.
    void row_identity(std::size_t row, std::string& identity) const;
    void compose_announcement();

    std::unique_ptr<RowHeightIndex> heights_;
    std::shared_ptr<VScrollBar> vertical_bar_{};
    SubscriptionToken vertical_scrolled_{};
    VirtualRowsCallbacks callbacks_{};

    // Local geometry from the last arrange.
    Rect client_{};
    Rect viewport_{};
    Rect bar_{};
    bool bar_shown_{};
    bool mirrored_{};

    // The viewport's top in content coordinates, and the anchor that keeps
    // it: anchor_row_ (row_count() when anchored at the end) and the
    // distance of its top below the viewport's top.
    double offset_{};
    std::size_t anchor_row_{};
    double anchor_top_{};
    // Following the tail: set by capture_anchor only when follow_tail_ is on
    // and the viewport is at the end.
    bool anchored_at_end_{};
    bool follow_tail_{};
    // The content width the stored heights were measured at; below zero
    // before the first settle.
    double measured_width_{-1.0};
    // Whether the last settle measured every row of its band.
    bool band_complete_{true};
    std::size_t first_realized_{};
    std::size_t end_realized_{};

    // The current row (selected_row()), the extended selection's ranges
    // and anchor, and whether a press is dragging a range.
    std::optional<std::size_t> selected_{};
    ListSelectionMode selection_mode_{ListSelectionMode::one};
    std::vector<VirtualRowRange> ranges_{};
    std::optional<std::size_t> selection_anchor_{};
    bool dragging_{};
    std::optional<std::size_t> hot_{};
    std::optional<std::size_t> measured_budget_{};
    // The last scrolled report: the offset and the first visible row.
    double reported_offset_{};
    std::size_t reported_first_{};
    bool focused_{};
    // Set while a target runs (CallbackScope); semantics run targets from
    // const members, hence mutable.
    mutable bool in_callback_{};
    bool synchronizing_scroll_{};
    // The scroll in progress (note_scroll_start, finish_scroll).
    bool scroll_copyable_{};
    std::size_t scroll_reference_row_{};
    double scroll_reference_top_{};

    // Reused by describe calls (semantics are read from const members).
    mutable VirtualRowDescription description_{};
    mutable VirtualRowsStatistics statistics_{};

    // Rows appended at the end since the last announcement, and the live
    // region's text (its storage is reused across announcements).
    bool announces_appends_{true};
    std::size_t pending_appended_{};
    std::string announcement_{};

    Event<const VirtualRowSelection&> selection_changed_{};
    Event<VirtualRowIndex> row_activated_{};
    Event<const VirtualRowScroll&> scrolled_{};
};

} // namespace gui_forms
