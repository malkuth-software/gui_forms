// The adaptive order model of DataGridView's virtual rows (owner decision
// W-5): the chunked order index of edit mode checked against a plain vector
// through random inserts, removals and moves; each reported change's cost
// measured by the order's counters; window mode asking its source only for
// the window around the displayed rows; automatic switching by row count and
// by change rate at the thresholds, each switch reported, and pinning; the
// current cell, the selection and the first displayed row kept by ID in
// both modes and across switches; a full republish as a reset; refused
// reports changing nothing.

#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

DataGridViewRowId id(std::string_view text) {
    const DataGridViewRowId identity{std::string(text)};
    return identity;
}

DataGridViewRowPosition row_at(std::size_t position) {
    const DataGridViewRowPosition row{position};
    return row;
}

// IDs "<prefix><first>" onwards.
std::vector<DataGridViewRowId> ids(std::size_t count, std::size_t first = 0U, std::string_view prefix = "r") {
    std::vector<DataGridViewRowId> order;
    order.reserve(count);
    for (std::size_t index = 0; index < count; ++index) order.push_back(id(std::string(prefix) + std::to_string(first + index)));
    return order;
}

// A small deterministic generator for the random edit sequences.
class Random final {
public:
    std::size_t below(std::size_t bound) {
        state_ = state_ * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::size_t value = static_cast<std::size_t>(state_ >> 33U) % bound;
        return value;
    }

private:
    std::uint64_t state_{0x9E3779B97F4A7C15ULL};
};

// The application's order as a plain vector, served as a window source;
// counts the grid's requests.
class VectorSource final : public DataGridViewRowWindowSource {
public:
    [[nodiscard]] std::size_t row_count() const override { return order.size(); }
    void row_ids(DataGridViewRowPosition first, std::span<DataGridViewRowId> out) const override {
        ++requests;
        for (std::size_t index = 0; index < out.size(); ++index) out[index] = order[first.value + index];
    }
    [[nodiscard]] std::optional<DataGridViewRowPosition> row_position(const DataGridViewRowId& row) const override {
        ++position_queries;
        for (std::size_t index = 0; index < order.size(); ++index) {
            if (order[index] == row) return row_at(index);
        }
        return std::nullopt;
    }

    std::vector<DataGridViewRowId> order;
    mutable std::size_t requests{};
    mutable std::size_t position_queries{};
};

// Ten million rows "r0" to "r9999999", computed, never stored.
class SyntheticSource final : public DataGridViewRowWindowSource {
public:
    static constexpr std::size_t rows = 10000000U;

    [[nodiscard]] std::size_t row_count() const override { return rows; }
    void row_ids(DataGridViewRowPosition first, std::span<DataGridViewRowId> out) const override {
        ++requests;
        asked += out.size();
        for (std::size_t index = 0; index < out.size(); ++index) {
            out[index].value = "r" + std::to_string(first.value + index);
        }
    }
    [[nodiscard]] std::optional<DataGridViewRowPosition> row_position(const DataGridViewRowId& row) const override {
        if (row.value.size() < 2U || row.value[0] != 'r') return std::nullopt;
        const std::size_t position = static_cast<std::size_t>(std::stoull(row.value.substr(1U)));
        if (position >= rows) return std::nullopt;
        return row_at(position);
    }

    mutable std::size_t requests{};
    mutable std::size_t asked{};
};

// Values computed from IDs; records the IDs asked for.
class EchoProvider final : public DataGridViewCellValueProvider {
public:
    [[nodiscard]] BindingValue cell_value(const DataGridViewRowId& row, std::size_t) const override {
        asked_ids.insert(row.value);
        return BindingValue{"label " + row.value};
    }
    bool push_cell_value(const DataGridViewRowId&, std::size_t, const BindingValue&) override { return true; }

    mutable std::unordered_set<std::string> asked_ids{};
};

class CountingPainter final : public Painter {
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

struct RecordSwitch final {
    std::vector<DataGridViewRowOrderModeChange>* seen{};
    void operator()(const DataGridViewRowOrderModeChange& change) const { (*seen).push_back(change); }
};

std::vector<DataGridViewColumn> one_column() {
    std::vector<DataGridViewColumn> columns(1U);
    columns[0].name = "label";
    columns[0].header_text = "Label";
    return columns;
}

// -- the chunked index against a plain vector ----------------------------------------------

void check_same(const DataGridViewRowOrder& order, const std::vector<DataGridViewRowId>& reference, const char* what) {
    require(order.count() == reference.size(), what);
    for (std::size_t position = 0; position < reference.size(); ++position) {
        if (order.id_at(position) != reference[position].value) require(false, what);
        const std::optional<std::size_t> found = order.position_of(reference[position].value);
        if (!found || *found != position) require(false, what);
    }
}

void chunked_index_matches_a_vector() {
    DataGridViewRowOrder order{};
    std::vector<DataGridViewRowId> reference = ids(1500U);
    order.reset(reference);
    check_same(order, reference, "a reset order reads back");
    Random random{};
    std::size_t next = reference.size();
    for (std::size_t step = 0; step < 4000U; ++step) {
        const std::size_t choice = random.below(4U);
        if (choice <= 1U || reference.size() < 4U) {
            // Insert one to three rows at a random place.
            const std::size_t count = 1U + random.below(3U);
            const std::size_t position = random.below(reference.size() + 1U);
            const std::vector<DataGridViewRowId> added = ids(count, next);
            next += count;
            order.insert(position, added);
            reference.insert(reference.begin() + static_cast<std::ptrdiff_t>(position), added.begin(), added.end());
        } else if (choice == 2U) {
            const std::size_t position = random.below(reference.size());
            const std::vector<DataGridViewRowId> removed{reference[position]};
            order.remove(removed);
            reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(position));
        } else {
            const std::size_t from = random.below(reference.size());
            const std::size_t to = random.below(reference.size());
            const DataGridViewRowId moved = reference[from];
            order.move(moved.value, to);
            reference.erase(reference.begin() + static_cast<std::ptrdiff_t>(from));
            reference.insert(reference.begin() + static_cast<std::ptrdiff_t>(to), moved);
        }
        if (step % 250U == 0U) check_same(order, reference, "the chunked order matches the vector during the edits");
    }
    check_same(order, reference, "the chunked order matches the vector after 4000 random edits");
    // Emptying it and filling it again.
    order.remove(reference);
    require(order.count() == 0U, "removing every row empties the order");
    order.insert(0U, ids(3U, 0U, "again"));
    require(order.count() == 3U && order.id_at(1U) == "again1", "an emptied order takes rows again");

    // Refused reports change nothing.
    const std::vector<DataGridViewRowId> repeated{id("again0")};
    bool refused = false;
    try {
        order.insert(0U, repeated);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && order.count() == 3U, "inserting a present ID is refused");
    refused = false;
    const std::vector<DataGridViewRowId> absent{id("again0"), id("nope")};
    try {
        order.remove(absent);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && order.count() == 3U && order.position_of("again0") == 0U,
            "removing an absent ID is refused before anything is removed");
    refused = false;
    try {
        order.insert(9U, ids(1U, 0U, "late"));
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && order.count() == 3U, "inserting past the end is refused");
}

// -- each change's cost ---------------------------------------------------------------------

void edit_cost_follows_the_change() {
    DataGridViewRowOrder order{};
    const std::size_t rows = 200000U;
    order.reset(ids(rows));
    const DataGridViewRowOrderStatistics built = order.statistics();
    require(built.full_builds == 1U && built.index_writes == rows, "a reset builds the whole index once");

    // One insertion in the middle.
    order.insert(rows / 2U, ids(1U, 0U, "new"));
    DataGridViewRowOrderStatistics after = order.statistics();
    const std::uint64_t capacity = DataGridViewRowOrder::chunk_capacity;
    require(after.index_writes - built.index_writes <= 1U + capacity + 1U &&
                after.slot_moves - built.slot_moves <= 3U * capacity + 1U && after.full_builds == 1U,
            "one insertion writes one index entry and shifts at most a chunk");

    // A thousand insertions, removals and moves cost a bounded amount each,
    // nothing like the 200000 a republish costs.
    const DataGridViewRowOrderStatistics before = order.statistics();
    Random random{};
    for (std::size_t step = 0; step < 1000U; ++step) {
        const std::size_t position = random.below(order.count());
        order.insert(position, ids(1U, step, "many"));
    }
    for (std::size_t step = 0; step < 1000U; ++step) {
        const std::vector<DataGridViewRowId> gone{id("many" + std::to_string(step))};
        order.remove(gone);
    }
    for (std::size_t step = 0; step < 1000U; ++step) {
        order.move("r" + std::to_string(random.below(rows)), random.below(order.count()));
    }
    after = order.statistics();
    const std::uint64_t changes = after.reported_changes - before.reported_changes;
    const std::uint64_t writes = after.index_writes - before.index_writes;
    const std::uint64_t moves = after.slot_moves - before.slot_moves;
    std::cout << "edit mode, " << rows << " rows: " << changes << " changes, " << writes << " index writes, "
              << moves << " slot moves, " << (after.chunk_table_rebuilds - before.chunk_table_rebuilds)
              << " chunk-table rebuilds\n";
    require(changes == 3000U, "3000 changes reported (a move counts once)");
    require(writes <= changes * 8U && moves <= changes * 3U * capacity && after.full_builds == 1U,
            "the work per change is bounded by a chunk, not by the row count");
}

// -- window mode asks only for its window -----------------------------------------------------

void window_mode_is_bounded() {
    std::shared_ptr<EchoProvider> provider = std::make_shared<EchoProvider>();
    std::shared_ptr<SyntheticSource> source = std::make_shared<SyntheticSource>();
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.window"));
    (*grid).set_columns(one_column());
    std::vector<DataGridViewRowOrderModeChange> switches;
    SubscriptionToken token = (*grid).row_order_mode_changed().subscribe(RecordSwitch{&switches});
    (*grid).set_virtual_rows(provider, source);
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::window && switches.empty() &&
                (*grid).row_count() == SyntheticSource::rows,
            "ten million rows from a window source start in window mode");
    Window window(grid, {300.0, 200.0});
    window.perform_layout();
    const std::size_t shown = (*grid).displayed_row_end().value - (*grid).first_displayed_row().value;
    const std::size_t margin = (*grid).row_order_thresholds.read().window_margin;
    require(shown > 0U && (*source).requests == 1U && (*source).asked <= shown + 2U * margin,
            "the first layout asks once, for the displayed rows and the margin");
    CountingPainter painter;
    (*grid).on_paint(painter, (*grid).client_rectangle());
    window.perform_layout();
    require((*source).requests == 1U, "paint and a second layout ask for nothing more");
    for (const std::string& asked : (*provider).asked_ids) {
        const std::size_t position = static_cast<std::size_t>(std::stoull(asked.substr(1U)));
        require(position < shown, "the provider is asked only for displayed rows");
    }
    // Scrolling within the margin asks for nothing; far away, one window.
    (*grid).set_first_displayed_row(row_at(margin / 2U));
    window.perform_layout();
    require((*source).requests == 1U, "a scroll within the margin needs no request");
    (*grid).set_first_displayed_row(row_at(5000000U));
    window.perform_layout();
    require((*source).requests == 2U && (*source).asked <= 2U * (shown + 2U * margin) &&
                (*grid).realized_rows()[0].stable_id == "r5000000",
            "a far scroll asks once for the new window");
    const DataGridViewRowOrderStatistics statistics = (*grid).row_order_statistics();
    require(statistics.full_builds == 0U && statistics.index_writes == 0U,
            "window mode never builds an index of the rows");
    require((*grid).row_position(id("r9999999")) == row_at(9999999U) && (*grid).row_id(row_at(42U)) == id("r42"),
            "IDs outside the window are asked of the source one at a time");
}

// -- switching by count and by rate, identity kept --------------------------------------------

void switches_keep_identity() {
    std::shared_ptr<EchoProvider> provider = std::make_shared<EchoProvider>();
    std::shared_ptr<VectorSource> source = std::make_shared<VectorSource>();
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("grid.switch"));
    (*grid).set_columns(one_column());
    std::vector<DataGridViewRowOrderModeChange> switches;
    SubscriptionToken token = (*grid).row_order_mode_changed().subscribe(RecordSwitch{&switches});
    DataGridViewRowOrderThresholds limits{};
    limits.window_rows = 1500U;
    limits.edit_rows = 800U;
    limits.window_changes = 5000U;
    limits.edit_changes = 100U;
    (*grid).row_order_thresholds = limits;
    (*source).order = ids(1000U);
    (*grid).set_virtual_rows(provider, (*source).order);
    (*grid).set_virtual_row_window_source(source);
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::edit && switches.empty(),
            "a thousand rows stay in edit mode with a source available");
    Window window(grid, {300.0, 200.0});
    window.perform_layout();
    require((*grid).set_current_cell(DataGridViewCellAddress{row_at(10U), 0U}), "row r10 is current");
    (*grid).selection_mode = DataGridViewSelectionMode::full_row_select;
    require((*grid).set_current_cell(DataGridViewCellAddress{row_at(10U), 0U}), "row r10 is current");
    (*grid).set_row_selected(row_at(20U), true);
    (*grid).set_first_displayed_row(row_at(5U));
    window.perform_layout();

    // 600 rows inserted at the top: 1600 rows cross window_rows.
    const std::vector<DataGridViewRowId> top = ids(600U, 0U, "t");
    (*source).order.insert((*source).order.begin(), top.begin(), top.end());
    (*grid).insert_virtual_rows(row_at(0U), top);
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::window && switches.size() == 1U &&
                switches[0].new_mode == DataGridViewRowOrderMode::window &&
                switches[0].reason == DataGridViewRowOrderSwitchReason::row_count && switches[0].row_count == 1600U,
            "crossing window_rows switches to window mode, reported with the count");
    window.perform_layout();
    require((*grid).current_row_id() == id("r10") && (*(*grid).current_cell()).row == row_at(610U) &&
                (*grid).is_row_selected(row_at(620U)) && (*grid).first_displayed_row() == row_at(605U),
            "the current row, the selection and the first displayed row follow their IDs into window mode");

    // 900 rows removed (t0..t599 and r0..r299; r10 among them).
    std::vector<DataGridViewRowId> gone(top.begin(), top.end());
    const std::vector<DataGridViewRowId> early = ids(300U);
    gone.insert(gone.end(), early.begin(), early.end());
    (*source).order.erase((*source).order.begin(), (*source).order.begin() + 900);
    (*grid).remove_virtual_rows(gone);
    require((*grid).row_count() == 700U && (*grid).current_row_id() == id("r910") &&
                (*(*grid).current_cell()).row == row_at(610U),
            "the removed current row hands over to the row now at its position");
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::window,
            "700 rows are few, but 900 changes in this pass are not quiet: still window mode");
    window.perform_layout();
    // The last pass had 900 changes: a small report now is still not quiet.
    (*source).order.push_back(id("late0"));
    (*grid).insert_virtual_rows(row_at(700U), {id("late0")});
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::window, "a busy last pass keeps window mode");
    window.perform_layout();
    window.perform_layout();
    (*source).order.push_back(id("late1"));
    (*grid).insert_virtual_rows(row_at(701U), {id("late1")});
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::edit && switches.size() == 2U &&
                switches[1].reason == DataGridViewRowOrderSwitchReason::row_count,
            "few rows and quiet passes return to edit mode, for the reason it left");
    require((*grid).current_row_id() == id("r910") && (*(*grid).current_cell()).row == row_at(610U) &&
                (*grid).row_id(row_at(701U)) == id("late1"),
            "the current row keeps its ID back in edit mode");

    // A burst of single inserts crosses window_changes.
    limits.window_changes = 300U;
    (*grid).row_order_thresholds = limits;
    for (std::size_t step = 0; step < 400U; ++step) {
        const DataGridViewRowId added = id("burst" + std::to_string(step));
        (*source).order.insert((*source).order.begin(), added);
        (*grid).insert_virtual_rows(row_at(0U), {added});
    }
    require(switches.size() == 3U && switches[2].new_mode == DataGridViewRowOrderMode::window &&
                switches[2].reason == DataGridViewRowOrderSwitchReason::change_rate && switches[2].changes == 300U,
            "the 300th change in one pass switches to window mode by change rate");
    // A pass is counted when the grid is laid out: one with the burst, then
    // one with a single change.
    window.perform_layout();
    (*source).order.push_back(id("settle"));
    (*grid).insert_virtual_rows(row_at((*grid).row_count()), {id("settle")});
    window.perform_layout();
    (*source).order.push_back(id("quiet"));
    (*grid).insert_virtual_rows(row_at((*grid).row_count()), {id("quiet")});
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::window,
            "1104 rows are more than edit_rows: window mode stays");
    limits.edit_rows = 1200U;
    (*grid).row_order_thresholds = limits;
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::edit && switches.size() == 4U &&
                switches[3].reason == DataGridViewRowOrderSwitchReason::policy,
            "new thresholds are applied at once, reported as the policy's");
    require((*grid).current_row_id() == id("r910") && (*(*grid).current_cell()).row == row_at(1010U),
            "after the burst the current row is still r910, 400 rows lower");

    // Pinning.
    (*grid).row_order_policy = DataGridViewRowOrderPolicy::window;
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::window && switches.size() == 5U &&
                switches[4].reason == DataGridViewRowOrderSwitchReason::policy,
            "pinning window mode switches at once");
    window.perform_layout();
    window.perform_layout();
    (*source).order.push_back(id("pinned"));
    (*grid).insert_virtual_rows(row_at((*grid).row_count()), {id("pinned")});
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::window, "a pinned mode is kept");
    (*grid).row_order_policy = DataGridViewRowOrderPolicy::automatic;
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::edit, "back to automatic, few rows go to edit");

    // A full republish is a reset.
    const std::uint64_t builds = (*grid).row_order_statistics().full_builds;
    (*source).order = ids(50U, 0U, "fresh");
    (*grid).set_virtual_row_order((*source).order);
    require((*grid).row_order_statistics().full_builds == builds + 1U && (*grid).row_count() == 50U &&
                (*grid).current_row_id() == id("fresh49"),
            "a republish rebuilds the order once");

    // Taking the source away from window mode fetches the whole order first.
    (*grid).row_order_policy = DataGridViewRowOrderPolicy::window;
    (*grid).set_virtual_row_window_source(nullptr);
    require((*grid).row_order_mode() == DataGridViewRowOrderMode::edit && (*grid).row_count() == 50U &&
                (*grid).row_order_policy.read() == DataGridViewRowOrderPolicy::automatic,
            "without a source the order is in edit mode and window mode is no longer pinned");
    bool refused = false;
    try {
        (*grid).row_order_policy = DataGridViewRowOrderPolicy::window;
    } catch (const std::logic_error&) {
        refused = true;
    }
    require(refused, "window mode cannot be pinned without a source");
    refused = false;
    try {
        (*grid).insert_virtual_rows(row_at(0U), {id("fresh3")});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*grid).row_count() == 50U, "an edit-mode report of a present ID is refused");
}

} // namespace

int main() {
    chunked_index_matches_a_vector();
    edit_cost_follows_the_change();
    window_mode_is_bounded();
    switches_keep_identity();
    std::cout << "DataGridView row order: chunked index, cost per change, window bounds, switches and identity passed\n";
    return 0;
}
