// The one data engine's DataGridView workload (cp33): a million virtual rows
// from a window source, so the grid holds no ID or row object per row; the
// row index (the stx btree VirtualRowsView uses) for the heights and the
// shared column strip for twenty columns. It checks what each step must
// leave behind and, with GUI_FORMS_GRID_WORKLOAD_TIMING set, prints each
// step's time for the integrator's scaling comparison with VirtualRowsView.
// Timings are printed, never asserted.

#include "gui_forms/binding/value/binding_value.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/column_strip.hpp"
#include "gui_forms/controls/data_grid_view.hpp"
#include "gui_forms/controls/data_grid_view_row_order.hpp"
#include "gui_forms/controls/data_grid_view_rows.hpp"
#include "gui_forms/controls/data_grid_view_types.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stxlib/optional.hpp>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "DataGridView workload: " << message << '\n';
        std::exit(1);
    }
}

constexpr std::size_t row_total = 1000000U;
constexpr std::size_t column_total = 20U;

DataGridViewRowPosition row_at(std::size_t position) {
    const DataGridViewRowPosition result{position};
    return result;
}

// A million rows "w0" to "w999999", computed, never stored.
class MillionSource final : public DataGridViewRowWindowSource {
public:
    [[nodiscard]] std::size_t row_count() const override { return row_total; }
    void row_ids(DataGridViewRowPosition first, std::span<DataGridViewRowId> out) const override {
        asked += out.size();
        for (std::size_t index = 0; index < out.size(); ++index) {
            out[index].value = "w" + std::to_string(first.value + index);
        }
    }
    [[nodiscard]] stxlib::Optional<DataGridViewRowPosition> row_position(const DataGridViewRowId& row) const override {
        if (row.value.size() < 2U || row.value[0] != 'w') return stxlib::Optional<DataGridViewRowPosition>::none();
        const std::size_t position = static_cast<std::size_t>(std::stoull(row.value.substr(1U)));
        if (position >= row_total) return stxlib::Optional<DataGridViewRowPosition>::none();
        return stxlib::Optional<DataGridViewRowPosition>::of(row_at(position));
    }

    mutable std::size_t asked{};
};

// Values computed from the ID and column; counts the cells asked for.
class MillionProvider final : public DataGridViewCellValueProvider {
public:
    [[nodiscard]] BindingValue cell_value(const DataGridViewRowId& row, std::size_t column) const override {
        ++asked;
        std::string text = row.value;
        text += ".";
        text += std::to_string(column);
        return BindingValue{std::move(text)};
    }
    bool push_cell_value(const DataGridViewRowId&, std::size_t, const BindingValue&) override { return true; }

    mutable std::size_t asked{};
};

class NullPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++texts; }
    void draw_image(ImageId, Rect, double) override {}

    std::size_t texts{};
};

// A fixed linear congruential sequence, so every run visits the same rows.
class Sequence final {
public:
    std::uint64_t next() {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::uint64_t result = state_ >> 33U;
        return result;
    }

private:
    std::uint64_t state_{0x9e3779b97f4a7c15ULL};
};

double seconds_since(std::chrono::steady_clock::time_point start) {
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    return elapsed.count();
}

// Prints one step's time when the switch is set.
void report(bool timing, const char* step, double seconds, std::size_t operations) {
    if (!timing) return;
    const double each = operations == 0U ? 0.0 : seconds * 1.0e6 / static_cast<double>(operations);
    std::cout << "  " << step << ": " << seconds * 1.0e3 << " ms, " << operations << " operations, " << each
              << " us each\n";
}

std::vector<DataGridViewColumn> twenty_columns() {
    std::vector<DataGridViewColumn> columns;
    columns.reserve(column_total);
    for (std::size_t index = 0; index < column_total; ++index) {
        DataGridViewColumn column{};
        column.name = "c" + std::to_string(index);
        column.header_text = "Column " + std::to_string(index);
        column.width = 60.0 + static_cast<double>(index % 4U) * 10.0;
        columns.push_back(std::move(column));
    }
    return columns;
}

// The shared column placement against its definition.
void column_strip_lookups() {
    ColumnStrip strip;
    require(strip.count() == 0U && strip.total() == 0.0 && strip.position_at(0.0) == 0U, "an empty strip");
    const std::vector<double> widths{100.0, 0.0, 50.0, 25.0};
    strip.assign(widths, 2U);
    require(strip.count() == 4U && strip.frozen() == 2U && strip.frozen_width() == 100.0 && strip.total() == 175.0,
            "the strip sums its widths and the frozen run");
    require(strip.start(2U) == 100.0 && strip.start(9U) == 175.0 && strip.width(1U) == 0.0 && strip.width(9U) == 0.0,
            "starts are prefix sums, held at the total");
    require(strip.position_at(0.0) == 0U && strip.position_at(99.5) == 0U && strip.position_at(100.0) == 2U &&
                strip.position_at(174.0) == 3U && strip.position_at(175.0) == 4U && strip.position_at(-1.0) == 4U,
            "the column holding an offset skips zero-width columns");
    require(strip.first_start_at_or_past(100.0, 0U, 4U) == 1U && strip.first_start_at_or_past(101.0, 0U, 4U) == 3U &&
                strip.first_start_at_or_past(500.0, 0U, 4U) == 4U && strip.first_start_at_or_past(0.0, 2U, 4U) == 2U,
            "the first start at or past a value, within a range");
    require(strip.first_end_past(100.0, 0U, 4U) == 2U && strip.first_end_past(99.0, 0U, 4U) == 0U &&
                strip.first_end_past(175.0, 0U, 4U) == 4U && strip.first_end_past(0.0, 2U, 4U) == 2U,
            "the first end past a value, within a range");
}

void million_row_workload(bool timing) {
    std::shared_ptr<MillionProvider> provider = std::make_shared<MillionProvider>();
    std::shared_ptr<MillionSource> source = std::make_shared<MillionSource>();
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.workload"));
    (*grid).set_columns(twenty_columns());

    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    (*grid).set_virtual_rows(provider, source);
    Window window(grid, {640.0, 480.0});
    window.perform_layout();
    report(timing, "bind and first layout", seconds_since(start), 1U);
    require((*grid).row_count() == row_total && (*grid).row_order_mode() == DataGridViewRowOrderMode::window,
            "a million rows from a window source are held as a window");
    require((*source).asked < 1000U, "the first layout asks only for the rows around those shown");

    // Random jumps: each layout, hit test and paint costs the index's
    // descent, and the provider is asked only for the cells shown.
    Sequence sequence;
    const std::size_t jumps = 2000U;
    std::size_t asked_most = 0U;
    start = std::chrono::steady_clock::now();
    for (std::size_t round = 0; round < jumps; ++round) {
        const std::size_t first = static_cast<std::size_t>(sequence.next() % row_total);
        (*grid).set_first_displayed_row(row_at(first));
        window.perform_layout();
        (*provider).asked = 0U;
        NullPainter painter;
        (*grid).on_paint(painter, (*grid).client_rectangle());
        asked_most = std::max(asked_most, (*provider).asked);
    }
    report(timing, "random jumps (layout and paint)", seconds_since(start), jumps);
    require(asked_most <= 40U * column_total * 2U, "a paint asks only for the cells shown");

    // Own heights on every thousandth row: the index stores each in place.
    start = std::chrono::steady_clock::now();
    for (std::size_t position = 500U; position < row_total; position += 1000U) {
        (*grid).set_row_height_at(row_at(position), 44.0);
    }
    report(timing, "1000 own heights", seconds_since(start), 1000U);
    require((*grid).row_height_at(row_at(500U)) == 44.0 && (*grid).row_height_at(row_at(501U)) == 22.0,
            "rows keep their own heights among a million");

    // Single-row steps, as a wheel scrolls: each is a scroll copy where the
    // window can copy, else a repaint of the scrolling pane; never more.
    (*grid).set_first_displayed_row(row_at(400000U));
    window.perform_layout();
    const DataGridViewScrollStatistics before = (*grid).scroll_statistics();
    const std::size_t steps = 500U;
    start = std::chrono::steady_clock::now();
    for (std::size_t step = 0; step < steps; ++step) {
        const DataGridViewRowPosition first = (*grid).first_displayed_row();
        (*grid).set_first_displayed_row(row_at(first.value + 1U));
        window.perform_layout();
    }
    report(timing, "single-row scroll steps", seconds_since(start), steps);
    const DataGridViewScrollStatistics after = (*grid).scroll_statistics();
    require((*grid).first_displayed_row() == row_at(400000U + steps), "each step moves one row");
    require((after.scroll_copies - before.scroll_copies) + (after.scroll_repaints - before.scroll_repaints) == steps,
            "each step is counted once, as a copy or a repaint");

    // Hit testing across the data area finds a shown row and column.
    const Rect well = (*grid).parts().well.bounds;
    std::size_t found = 0U;
    const std::size_t probes = 5000U;
    start = std::chrono::steady_clock::now();
    for (std::size_t probe = 0; probe < probes; ++probe) {
        const double x = well.x + static_cast<double>(sequence.next() % 400U);
        const double y = well.y + 30.0 + static_cast<double>(sequence.next() % 300U);
        const DataGridViewHitTestInfo hit = (*grid).hit_test_at(Point{x, y});
        if (hit.type == DataGridViewHitTestType::cell && hit.row.present() && (*hit.row.value()).value < row_total) ++found;
    }
    report(timing, "hit tests", seconds_since(start), probes);
    require(found > probes / 2U, "points over the cells hit cells");

    // Frozen panes over a million rows: the frozen rows stay first while the
    // scrolling rows and columns move.
    (*grid).frozen_row_count = 2U;
    (*grid).set_column_frozen(1U, true);
    const std::size_t sideways = 200U;
    start = std::chrono::steady_clock::now();
    for (std::size_t step = 0; step < sideways; ++step) {
        (*grid).set_horizontal_scroll_offset(static_cast<double>(step % 40U) * 10.0);
        (*grid).set_first_displayed_row(row_at(static_cast<std::size_t>(sequence.next() % row_total)));
        window.perform_layout();
        NullPainter painter;
        (*grid).on_paint(painter, (*grid).client_rectangle());
    }
    report(timing, "frozen-pane scrolls", seconds_since(start), sideways);
    const std::vector<DataGridView::Row> rows = (*grid).realized_rows();
    require(rows.size() > 3U && rows[0].position == row_at(0U) && rows[1].position == row_at(1U),
            "the frozen rows are shown first");

    // A row made on demand, as Windows Forms' shared rows are.
    const DataGridViewRow last = (*grid).shared_row(row_at(row_total - 1U));
    require(last.stable_id.value == "w999999" && last.cells.size() == column_total &&
                std::get<std::string>(last.cells[3]) == "w999999.3",
            "a row is made on demand from its source");

    // A change of the default height rebuilds the index as one run plus
    // the own heights.
    start = std::chrono::steady_clock::now();
    (*grid).row_height = 24.0;
    window.perform_layout();
    report(timing, "default height change", seconds_since(start), 1U);
    require((*grid).row_height_at(row_at(1500U)) == 44.0 && (*grid).row_height_at(row_at(1501U)) == 24.0,
            "own heights stand and the others follow the default");
}

} // namespace

int main() {
    const bool timing = std::getenv("GUI_FORMS_GRID_WORKLOAD_TIMING") != nullptr;
    if (timing) std::cout << "DataGridView workload, " << row_total << " rows, " << column_total << " columns:\n";
    column_strip_lookups();
    million_row_workload(timing);
    std::cout << "DataGridView workload: a million rows, own heights, scroll steps, hit tests, frozen panes and "
                 "shared rows passed\n";
    return 0;
}
