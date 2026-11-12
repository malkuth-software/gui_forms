#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/control.hpp"
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

// A change of the selected row, after it happened. A row erased while
// selected reports `current` none.
struct GUI_FORMS_API VirtualRowSelection final {
    std::optional<VirtualRowIndex> previous{};
    std::optional<VirtualRowIndex> current{};
};

// Work counters, for bounded-work checks and workload records. The totals
// only grow; the "last" figures describe the most recent settle (a layout
// or a scroll) and paint. index_runs walks the height index's leaves (O(runs));
// the other fields are O(1).
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
};

// A vertically scrolling list of a very large number of owner-drawn rows of
// varying height (CHAT_STRESS_SPEC section 4.6). Rows are not controls and
// the view keeps no row records: it keeps the row count and each row's
// height in a height-indexed order-statistic B-tree (RowHeightIndex), and
// asks the application to measure, draw and describe the rows it shows.
//
// Heights. Every row starts at the estimate (estimated_row_height), so a
// million rows are one run of the index and startup measures nothing but
// the rows in view. A row is measured lazily, once, when it first comes
// within the viewport or the overscan band (half a viewport above and
// below); rows never shown stay estimated. A change of the content width
// forgets every measured height (the index is reassigned to the estimate)
// and measures again around the anchor; refresh_rows forgets chosen rows.
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
// Keyboard (focused): Up and Down move the selection by a row, Page Up and
// Page Down by a viewport, Home and End (with or without Ctrl) to the first
// and last rows, each scrolling the selection into view; Enter and Space
// activate the selected row (row_activated). A primary press selects the
// row under the pointer; a double click activates it.
//
// Accessibility: a list whose size is row_count() and whose children are the
// realized rows only, each a list item named by describe, with its position
// in the set, its selection and the focus, select, and press actions. After
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
    [[nodiscard]] std::optional<VirtualRowIndex> selected_row() const noexcept;
    // Selects a row (none: no row) and raises selection_changed when it
    // changes; does not scroll. Throws std::out_of_range for a row past the
    // end.
    void set_selected_row(std::optional<VirtualRowIndex> row);

    [[nodiscard]] VirtualRowsStatistics statistics() const noexcept;

    [[nodiscard]] Event<const VirtualRowSelection&>& selection_changed() noexcept { return selection_changed_; }
    [[nodiscard]] Event<VirtualRowIndex>& row_activated() noexcept { return row_activated_; }

    // -- Control ------------------------------------------------------------
    [[nodiscard]] Size measure(Size available) override;
    void arrange(Rect final_bounds) override;
    void on_paint(Painter& painter, Rect local_damage) override;
    void on_pointer(PointerEvent& event) override;
    void on_key(KeyEvent& event) override;
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

private:
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
    // returns the heights stored.
    std::size_t measure_band(double top, double bottom, std::size_t budget, std::size_t& asked);
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
    // Selects a row, scrolls it into view (nearest) and raises
    // selection_changed when it changed.
    void select_and_reveal(std::size_t row);
    void raise_selection(std::optional<std::size_t> previous);
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

    std::optional<std::size_t> selected_{};
    std::optional<std::size_t> hot_{};
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
};

} // namespace gui_forms
