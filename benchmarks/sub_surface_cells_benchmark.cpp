// The threaded sub-surfaces measurements (design note section 12) for the
// DataGridView cell region (stage S7), on a synthetic 100,000-row grid
// resolved into DataGridViewCellsSnapshots, painted by the CPU Skia raster.
// Headless: the harness plays a host's present path at 60 Hz (it paints the
// window into a Skia framebuffer and releases the receipt, takes the live
// surface presentations and reports each one shown with display-phased
// timing), so the numbers measure this library's work and agreement, not a
// display's or a native host's.
//
//   M1  UI thread cost per scroll step: the cells painted on the UI thread
//       against the cells on a sub-surface (snapshot + redraw_all + flush),
//       for the bare cells and for a DataGridView of 100,000 rows with
//       cells_on_sub_surface off and on (the grid's headers, layout and
//       resolution stay on the UI thread either way).
//   M2  update to publish: redraw_all until the frame tagged with its epoch
//       is published; and the input path on the real grid (cells on its
//       region): a click on a cell, and a key moving the current cell, each
//       dispatched through the window, to the publication of the epoch its
//       paint sent and to the 60 Hz present tick that shows that frame
//       paired (input to photon, the display's scanout excluded). Inputs
//       land at phases spread over the refresh. Target: the present that
//       shows it within one display frame of the input.
//   M4  agreement while scrolling: pairing waits and unpaired presentations
//       at the present ticks (target: no unpaired presentation).
//   M5  publish interval spread while scrolling at 60 Hz.
//   M6  memory per sub-surface: LiveSurface buffers, snapshot, thread stack.
//   M7  a posted call's round trip; a 1,000-cell snapshot from build to
//       publish.
//   M8  not applicable to the cell region: its cells are not exposed (the
//       grid publishes them on the UI thread).
//   M9  stop and join: idle, and with a redraw-all and a busy posted call
//       pending.
//
// Usage: gui_forms_sub_surface_cells_benchmark [steps]  (default 600)

#include "gui_forms/gui_forms.hpp"
#include "gui_forms/controls/data_grid_view/data_grid_view.hpp"
#include "gui_forms/controls/data_grid_view/data_grid_view_cells.hpp"
#include "../src/controls/data_grid_view/data_grid_view_cells_control.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#include "skia_raster.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace gui_forms;
using Clock = std::chrono::steady_clock;

// The synthetic grid: rows of fixed height, eight columns, a region the
// size of a typical grid's data area.
constexpr std::size_t row_total = 100000U;
constexpr std::size_t column_total = 8U;
constexpr double row_height = 22.0;
constexpr double column_width = 100.0;
constexpr double region_width = 800.0;
constexpr double region_height = 600.0;
constexpr std::chrono::nanoseconds refresh{16666667};
constexpr std::chrono::seconds wait_limit{10};

void require(const bool condition, const char* const message) {
    if (condition) return;
    std::fprintf(stderr, "sub-surface cells benchmark: %s\n", message);
    std::exit(1);
}

[[nodiscard]] double milliseconds(Clock::duration duration) noexcept {
    const std::chrono::duration<double, std::milli> taken = duration;
    return taken.count();
}

// Sorted samples' median, 99th percentile and maximum, in milliseconds.
struct Spread final {
    double median{};
    double p99{};
    double maximum{};
    double deviation{};
};

[[nodiscard]] Spread spread_of(std::vector<double> samples) {
    Spread result;
    if (samples.empty()) return result;
    std::sort(samples.begin(), samples.end());
    const std::size_t count = samples.size();
    result.median = samples[count / 2U];
    const std::size_t p99_index = std::min(count - 1U, (count * 99U) / 100U);
    result.p99 = samples[p99_index];
    result.maximum = samples.back();
    double sum = 0.0;
    for (const double sample : samples) sum += sample;
    const double mean = sum / static_cast<double>(count);
    double squares = 0.0;
    for (const double sample : samples) squares += (sample - mean) * (sample - mean);
    result.deviation = std::sqrt(squares / static_cast<double>(count));
    return result;
}

void report(const char* label, const Spread& spread) {
    std::printf("%-44s median %.3f ms  p99 %.3f ms  max %.3f ms  sd %.3f ms\n", label, spread.median, spread.p99,
                spread.maximum, spread.deviation);
}

// The visible row window from `first_row`, resolved as the grid will resolve
// it: one text cell per visible row and column, formatted values in one
// text buffer, region coordinates.
[[nodiscard]] DataGridViewCellsSnapshot snapshot_from(std::size_t first_row, std::size_t cell_limit) {
    const std::size_t visible_rows = static_cast<std::size_t>(std::ceil(region_height / row_height));
    const std::size_t row_end = std::min(row_total, first_row + visible_rows);
    DataGridViewCellsSnapshot snapshot{};
    snapshot.well = PartGeometry::from_bounds(Rect{-2.0, -2.0, region_width + 4.0, region_height + 4.0});
    const std::size_t cell_count = std::min(cell_limit, (row_end - first_row) * column_total);
    snapshot.cells.reserve(cell_count);
    // "R" + six digits + "C" + one digit per cell.
    snapshot.text.reserve(cell_count * 10U);
    std::size_t made = 0U;
    for (std::size_t row = first_row; row < row_end && made < cell_count; ++row) {
        const double y = static_cast<double>(row - first_row) * row_height;
        for (std::size_t column = 0U; column < column_total && made < cell_count; ++column) {
            const Rect bounds{static_cast<double>(column) * column_width, y, column_width, row_height};
            DataGridViewCellsCell cell{};
            cell.parts.cell = PartGeometry::from_bounds(bounds);
            cell.parts.selection = PartGeometry::from_bounds(bounds);
            cell.parts.content = PartGeometry::from_bounds(Rect{bounds.x + 4.0, bounds.y, bounds.width - 8.0,
                                                                bounds.height});
            cell.parts.vertical_rule =
                PartGeometry::from_bounds(Rect{bounds.x + bounds.width - 1.0, bounds.y, 1.0, bounds.height});
            cell.parts.horizontal_rule =
                PartGeometry::from_bounds(Rect{bounds.x, bounds.y + bounds.height - 1.0, bounds.width, 1.0});
            cell.text.begin_byte = snapshot.text.size();
            snapshot.text += 'R';
            snapshot.text += std::to_string(row);
            snapshot.text += 'C';
            snapshot.text += std::to_string(column);
            cell.text.end_byte = snapshot.text.size();
            snapshot.cells.push_back(cell);
            ++made;
        }
    }
    return snapshot;
}

[[nodiscard]] std::unique_ptr<SubSurfaceContentUpdate> update_from(std::size_t first_row, std::size_t cell_limit) {
    std::unique_ptr<SubSurfaceContentUpdate> update =
        std::make_unique<DataGridViewCellsUpdate>(snapshot_from(first_row, cell_limit));
    return update;
}

constexpr std::size_t every_cell = row_total * column_total;

// Waits, spinning, until the surface publishes a frame tagged `tag` or
// later; the time it took.
[[nodiscard]] Clock::duration wait_for_tag(const LiveSurface& surface, std::uint64_t tag) {
    const Clock::time_point start = Clock::now();
    const Clock::time_point limit = start + wait_limit;
    while (Clock::now() < limit) {
        const LiveSurfaceSnapshot snapshot = surface.snapshot();
        if (snapshot.has_frame && !snapshot.carries_previous_frame && snapshot.content_tag >= tag) {
            return Clock::now() - start;
        }
        std::this_thread::yield();
    }
    require(false, "the sub-surface did not publish in time");
    return Clock::duration{};
}

// A host's present path for one window: paints the window's damage into its
// framebuffer and releases the receipt, then, at a present tick, takes the
// live surface presentations and reports each surface's frame as shown.
class PresentPath final {
public:
    PresentPath(Window& window, render::SkiaRaster& raster)
        : window_(window), framebuffer_(raster.create_framebuffer(Size{region_width, region_height}, 1.0)) {
        require(static_cast<bool>(framebuffer_), "the raster makes a framebuffer");
    }
    // The UI thread's paint of one frame.
    void paint() {
        const DamageRegion damage = window_.take_damage();
        if (damage.empty() || !(*framebuffer_).begin(window_.image_resources(), damage.bounds())) return;
        const std::optional<PaintReceipt> receipt = window_.paint((*framebuffer_).painter(), damage.rectangles());
        (*framebuffer_).end();
        if (receipt) static_cast<void>(window_.notify_presented(*receipt));
    }
    // One present tick phased to `vblank`: the presentations it showed.
    std::size_t present(FrameTime vblank) {
        const std::vector<LiveSurfacePresentation> shown = window_.take_live_surface_presentations(true);
        LiveDisplayTiming timing;
        timing.refresh_period = refresh;
        timing.last_vblank = vblank;
        timing.source = LiveTimingSource::display_phased;
        for (const LiveSurfacePresentation& presentation : shown) {
            if (!presentation.surface) continue;
            const LiveSurfaceFrame frame = (*presentation.surface).acquire_latest();
            if (frame) (*presentation.surface).notify_presented(frame.generation(), timing);
        }
        return shown.size();
    }

private:
    Window& window_;
    std::unique_ptr<PaintFramebuffer> framebuffer_;
};

// M1, one thread: the cells as a UI control, painted by the window.
[[nodiscard]] Spread ui_thread_cells(std::size_t steps) {
    render::SkiaRaster raster;
    std::shared_ptr<DataGridViewCells> cells = std::make_shared<DataGridViewCells>(StableId("bench.grid"));
    Window window(cells, Size{region_width, region_height});
    window.set_framebuffer_painter(&raster);
    window.flush();
    PresentPath path(window, raster);
    path.paint();
    std::vector<double> samples;
    samples.reserve(steps);
    for (std::size_t step = 0U; step < steps; ++step) {
        const Clock::time_point start = Clock::now();
        require((*cells).set_snapshot(snapshot_from(step * 3U, every_cell)), "a valid snapshot");
        window.flush();
        path.paint();
        samples.push_back(milliseconds(Clock::now() - start));
    }
    Spread result = spread_of(samples);
    return result;
}

// The real grid: 100,000 rows of eight formatted text cells.
[[nodiscard]] std::shared_ptr<DataGridView> make_grid() {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("bench.data-grid"));
    std::vector<DataGridViewColumn> columns;
    columns.reserve(column_total);
    for (std::size_t column = 0U; column < column_total; ++column) {
        DataGridViewColumn added{};
        added.name = "c" + std::to_string(column);
        added.header_text = "Column " + std::to_string(column);
        added.width = column_width;
        columns.push_back(std::move(added));
    }
    (*grid).set_columns(std::move(columns));
    std::vector<DataGridViewRow> rows;
    rows.reserve(row_total);
    for (std::size_t row = 0U; row < row_total; ++row) {
        DataGridViewRow added{};
        added.stable_id = DataGridViewRowId{"r" + std::to_string(row)};
        added.cells.reserve(column_total);
        for (std::size_t column = 0U; column < column_total; ++column) {
            added.cells.push_back(BindingValue{"R" + std::to_string(row) + "C" + std::to_string(column)});
        }
        rows.push_back(std::move(added));
    }
    (*grid).rows = std::move(rows);
    (*grid).set_allow_user_to_add_rows(false);
    return grid;
}

// M1 and M4 for the real grid: scrolling three rows a step at 60 Hz, the
// cells on the UI thread or on the grid's region.
void grid_scrolling(bool on_region, std::size_t steps, Spread& ui_cost, std::uint64_t& waits,
                    std::uint64_t& unpaired, std::uint64_t& behind) {
    render::SkiaRaster raster;
    std::shared_ptr<DataGridView> grid = make_grid();
    (*grid).cells_on_sub_surface = on_region;
    Window window(grid, Size{region_width + 40.0, region_height + 60.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    PresentPath path(window, raster);
    path.paint();
    const MetricsSnapshot before = window.metrics_snapshot();
    std::vector<double> samples;
    samples.reserve(steps);
    Clock::time_point tick = Clock::now() + refresh;
    for (std::size_t step = 0U; step < steps; ++step) {
        const Clock::time_point start = Clock::now();
        (*grid).set_first_displayed_row(DataGridViewRowPosition{(step * 3U) % (row_total - 40U)});
        window.flush();
        path.paint();
        samples.push_back(milliseconds(Clock::now() - start));
        std::this_thread::sleep_until(tick);
        // Whether the region's newest frame is older than the epoch the
        // UI frame just painted needs, as the present tick finds it.
        if (on_region) {
            const std::shared_ptr<SubSurfaceHost> host =
                std::dynamic_pointer_cast<SubSurfaceHost>((*grid).children()[0]);
            require(static_cast<bool>(host), "the grid's region is its first child");
            const LiveSurfaceSnapshot shown = (*(*host).live_surface()).snapshot();
            if (shown.content_tag < (*host).content_epoch()) ++behind;
        }
        static_cast<void>(path.present(tick));
        tick += refresh;
    }
    const MetricsSnapshot after = window.metrics_snapshot();
    ui_cost = spread_of(samples);
    waits = after.live_pairing_waits - before.live_pairing_waits;
    unpaired = after.live_unpaired_presentations - before.live_unpaired_presentations;
}

// A window whose region is a sub-surface showing the cells.
struct Region final {
    render::SkiaRaster raster;
    std::shared_ptr<Panel> root = std::make_shared<Panel>(StableId("bench.window"));
    std::shared_ptr<SubSurfaceHost> host = std::make_shared<SubSurfaceHost>(StableId("bench.cells"));
    std::unique_ptr<Window> window;
    std::shared_ptr<LiveSurface> surface;

    Region() {
        (*host).set_requested_bounds(Rect{0.0, 0.0, region_width, region_height});
        (*host).set_content(std::make_unique<DataGridViewCellsContent>(StableId("bench.grid")), true);
        static_cast<void>((*host).redraw_all(update_from(0U, every_cell)));
        (*root).add_child(host);
        window = std::make_unique<Window>(root, Size{region_width, region_height});
        (*window).set_framebuffer_painter(&raster);
        (*window).flush();
        surface = (*host).live_surface();
        require(static_cast<bool>(surface), "the host made its surface");
        static_cast<void>(wait_for_tag(*surface, (*host).content_epoch()));
    }
    ~Region() {
        if (window) static_cast<void>((*root).remove_child((*host).runtime_id()));
    }
    Region(const Region&) = delete;
    Region& operator=(const Region&) = delete;
};

// M1 (sub-surface), M4 and M5: scrolling at 60 Hz with a present tick per
// refresh.
void scrolling_on_sub_surface(std::size_t steps, Spread& ui_cost, Spread& publish_intervals,
                              std::uint64_t& waits, std::uint64_t& unpaired, std::uint64_t& presented) {
    Region region;
    Window& window = *region.window;
    PresentPath path(window, region.raster);
    path.paint();
    const MetricsSnapshot before = window.metrics_snapshot();
    std::vector<double> ui_samples;
    ui_samples.reserve(steps);
    std::vector<double> interval_samples;
    interval_samples.reserve(steps);
    std::uint64_t last_publishes = (*region.surface).snapshot().publishes;
    Clock::time_point last_publish_seen = Clock::now();
    Clock::time_point tick = Clock::now() + refresh;
    presented = 0U;
    for (std::size_t step = 0U; step < steps; ++step) {
        const Clock::time_point start = Clock::now();
        static_cast<void>((*region.host).redraw_all(update_from(step * 3U, every_cell)));
        window.flush();
        path.paint();
        ui_samples.push_back(milliseconds(Clock::now() - start));
        std::this_thread::sleep_until(tick);
        presented += path.present(tick);
        tick += refresh;
        const LiveSurfaceSnapshot snapshot = (*region.surface).snapshot();
        if (snapshot.publishes != last_publishes) {
            const Clock::time_point now = Clock::now();
            interval_samples.push_back(milliseconds(now - last_publish_seen));
            last_publish_seen = now;
            last_publishes = snapshot.publishes;
        }
    }
    ui_cost = spread_of(ui_samples);
    publish_intervals = spread_of(interval_samples);
    const MetricsSnapshot after = window.metrics_snapshot();
    waits = after.live_pairing_waits - before.live_pairing_waits;
    unpaired = after.live_unpaired_presentations - before.live_unpaired_presentations;
}

// M2: redraw_all to the publication of its epoch, one at a time.
[[nodiscard]] Spread update_to_publish(std::size_t steps) {
    Region region;
    PresentPath path(*region.window, region.raster);
    std::vector<double> samples;
    samples.reserve(steps);
    for (std::size_t step = 0U; step < steps; ++step) {
        // The previous frame is shown first, so the pacer is not waiting.
        static_cast<void>(path.present(FrameClock::now()));
        const Clock::time_point start = Clock::now();
        const std::uint64_t epoch = (*region.host).redraw_all(update_from(step * 7U, every_cell));
        (*region.window).flush();
        static_cast<void>(wait_for_tag(*region.surface, epoch));
        samples.push_back(milliseconds(Clock::now() - start));
    }
    Spread result = spread_of(samples);
    return result;
}

// M2's input path: which input, and what it measured.
enum class GridInput : std::uint8_t { click, key };

struct InputPath final {
    Spread to_publish{};
    Spread to_present{};
    // Inputs whose frame was presented within one refresh of the input.
    std::size_t within_frame{};
};

// The point of the data cell in display column 2, `row` rows below the
// first displayed one: past the row headers and clear of the column
// headers (the 40-unit offset leaves room for any header height in use).
[[nodiscard]] Point cell_point(std::size_t row) noexcept {
    const Point point{column_width * 2.5, 40.0 + row_height * (static_cast<double>(row) + 0.5)};
    return point;
}

void click_at(Window& window, Point point) {
    PointerEvent down{PointerAction::down, PointerButton::primary, point};
    down.click_count = 1U;
    static_cast<void>(window.dispatch_pointer(down));
    PointerEvent up{PointerAction::up, PointerButton::primary, point};
    static_cast<void>(window.dispatch_pointer(up));
}

// M2 on the real grid: each step presents the previous frame, waits until a
// phase of the refresh, dispatches the input (a click on another row's
// cell, or Down and Up in turn), flushes and paints the window as a host
// would, then waits for the region's epoch and presents at the next tick.
[[nodiscard]] InputPath input_to_photon(GridInput input, std::size_t steps) {
    render::SkiaRaster raster;
    std::shared_ptr<DataGridView> grid = make_grid();
    (*grid).cells_on_sub_surface = true;
    Window window(grid, Size{region_width + 40.0, region_height + 60.0});
    window.set_framebuffer_painter(&raster);
    window.flush();
    PresentPath path(window, raster);
    path.paint();
    const std::shared_ptr<SubSurfaceHost> host = std::dynamic_pointer_cast<SubSurfaceHost>((*grid).children()[0]);
    require(static_cast<bool>(host), "the grid's region is its first child");
    const std::shared_ptr<LiveSurface> surface = (*host).live_surface();
    static_cast<void>(wait_for_tag(*surface, (*host).content_epoch()));
    // The first click gives the grid focus and a current cell for the keys.
    click_at(window, cell_point(0U));
    window.flush();
    path.paint();
    static_cast<void>(wait_for_tag(*surface, (*host).content_epoch()));
    std::vector<double> publish_samples;
    std::vector<double> present_samples;
    publish_samples.reserve(steps);
    present_samples.reserve(steps);
    std::size_t within_frame = 0U;
    Clock::time_point tick = Clock::now() + refresh;
    for (std::size_t step = 0U; step < steps; ++step) {
        std::this_thread::sleep_until(tick);
        static_cast<void>(path.present(tick));
        // Sixteen phases across the refresh, in a fixed stride.
        const std::chrono::nanoseconds phase = refresh * static_cast<std::int64_t>((step * 7U) % 16U) / 16;
        std::this_thread::sleep_until(tick + phase);
        const std::uint64_t before = (*host).content_epoch();
        const Clock::time_point start = Clock::now();
        if (input == GridInput::click) {
            // Rows 1 to 20 in turn: never the row the last click chose.
            click_at(window, cell_point(1U + step % 20U));
        } else {
            KeyEvent event{KeyAction::down, (step % 2U) == 0U ? PhysicalKey::down : PhysicalKey::up};
            static_cast<void>(window.dispatch_key(event));
        }
        window.flush();
        path.paint();
        const std::uint64_t epoch = (*host).content_epoch();
        require(epoch > before, "the input's paint sent the region a new epoch");
        static_cast<void>(wait_for_tag(*surface, epoch));
        const Clock::time_point published = Clock::now();
        // The next present tick on the 60 Hz grid shows it paired.
        while (tick <= published) tick += refresh;
        std::this_thread::sleep_until(tick);
        static_cast<void>(path.present(tick));
        require((*surface).snapshot().content_tag >= epoch, "the present shows the input's epoch");
        const Clock::duration to_present = tick - start;
        publish_samples.push_back(milliseconds(published - start));
        present_samples.push_back(milliseconds(to_present));
        if (to_present <= refresh) ++within_frame;
        tick += refresh;
    }
    InputPath result{};
    result.to_publish = spread_of(publish_samples);
    result.to_present = spread_of(present_samples);
    result.within_frame = within_frame;
    return result;
}

// Posted to the sub-surface: raises a flag the UI thread spins on.
class RaiseFlag final {
public:
    explicit RaiseFlag(std::shared_ptr<std::atomic<bool>> flag) : flag_(std::move(flag)) {}
    void operator()() const { (*flag_).store(true, std::memory_order_release); }

private:
    std::shared_ptr<std::atomic<bool>> flag_;
};

// Posted to the sub-surface: keeps its thread busy for a while.
class StayBusy final {
public:
    explicit StayBusy(std::chrono::milliseconds duration) : duration_(duration) {}
    void operator()() const { std::this_thread::sleep_for(duration_); }

private:
    std::chrono::milliseconds duration_{};
};

// M7: a posted call's round trip, and a 1,000-cell snapshot from build to
// publish.
void message_costs(std::size_t steps, Spread& round_trip, Spread& thousand_cells) {
    Region region;
    std::vector<double> trips;
    trips.reserve(steps);
    for (std::size_t step = 0U; step < steps; ++step) {
        std::shared_ptr<std::atomic<bool>> flag = std::make_shared<std::atomic<bool>>(false);
        const Clock::time_point start = Clock::now();
        static_cast<void>((*region.host).dispatcher().begin_invoke(RaiseFlag(flag)));
        while (!(*flag).load(std::memory_order_acquire)) std::this_thread::yield();
        trips.push_back(milliseconds(Clock::now() - start));
    }
    round_trip = spread_of(trips);
    PresentPath path(*region.window, region.raster);
    std::vector<double> batches;
    batches.reserve(steps);
    for (std::size_t step = 0U; step < steps; ++step) {
        static_cast<void>(path.present(FrameClock::now()));
        const Clock::time_point start = Clock::now();
        const std::uint64_t epoch = (*region.host).redraw_all(update_from(step, 1000U));
        (*region.window).flush();
        static_cast<void>(wait_for_tag(*region.surface, epoch));
        batches.push_back(milliseconds(Clock::now() - start));
    }
    thousand_cells = spread_of(batches);
}

// M9: removing the host from its window stops and joins its thread.
[[nodiscard]] double stop_time(bool pending_work) {
    Region region;
    if (pending_work) {
        static_cast<void>((*region.host).dispatcher().begin_invoke(StayBusy(std::chrono::milliseconds(20))));
        static_cast<void>((*region.host).redraw_all(update_from(1234U, every_cell)));
    }
    const Clock::time_point start = Clock::now();
    static_cast<void>((*region.root).remove_child((*region.host).runtime_id()));
    const double taken = milliseconds(Clock::now() - start);
    require(!(*region.host).started(), "the host's thread stopped");
    region.window.reset();
    return taken;
}

} // namespace

int main(int argc, char** argv) {
    std::size_t steps = 600U;
    if (argc > 1) {
        const long requested = std::strtol(argv[1], nullptr, 10);
        if (requested > 0) steps = static_cast<std::size_t>(requested);
    }
    std::printf("sub-surface cells benchmark: %zu rows x %zu columns, region %.0fx%.0f, %zu steps\n", row_total,
                column_total, region_width, region_height, steps);

    const Spread single = ui_thread_cells(steps);
    report("M1 UI cost per step, cells on the UI thread", single);
    Spread ui_cost;
    Spread intervals;
    std::uint64_t waits = 0U;
    std::uint64_t unpaired = 0U;
    std::uint64_t presented = 0U;
    scrolling_on_sub_surface(steps, ui_cost, intervals, waits, unpaired, presented);
    report("M1 UI cost per step, cells on a sub-surface", ui_cost);
    Spread grid_plain;
    Spread grid_region;
    std::uint64_t grid_waits = 0U;
    std::uint64_t grid_unpaired = 0U;
    std::uint64_t grid_behind = 0U;
    grid_scrolling(false, steps, grid_plain, grid_waits, grid_unpaired, grid_behind);
    report("M1 UI cost per step, grid, cells on UI", grid_plain);
    grid_scrolling(true, steps, grid_region, grid_waits, grid_unpaired, grid_behind);
    report("M1 UI cost per step, grid, cells on region", grid_region);
    std::printf("M4 grid pairing at %zu ticks: %llu waits, %llu unpaired\n", steps,
                static_cast<unsigned long long>(grid_waits), static_cast<unsigned long long>(grid_unpaired));
    std::printf("M4 grid region behind its epoch at the present tick: %llu of %zu\n",
                static_cast<unsigned long long>(grid_behind), steps);
    std::printf("M4 pairing at %zu ticks: %llu presented, %llu waits, %llu unpaired\n", steps,
                static_cast<unsigned long long>(presented), static_cast<unsigned long long>(waits),
                static_cast<unsigned long long>(unpaired));
    report("M5 publish interval at 60 Hz", intervals);
    const Spread to_publish = update_to_publish(steps / 4U + 1U);
    report("M2 redraw_all to publish (full window)", to_publish);
    const std::size_t input_steps = steps / 4U + 1U;
    const InputPath clicks = input_to_photon(GridInput::click, input_steps);
    report("M2 grid click to publish", clicks.to_publish);
    report("M2 grid click to present (60 Hz ticks)", clicks.to_present);
    std::printf("M2 grid clicks presented within one frame: %zu of %zu\n", clicks.within_frame, input_steps);
    const InputPath keys = input_to_photon(GridInput::key, input_steps);
    report("M2 grid key to publish", keys.to_publish);
    report("M2 grid key to present (60 Hz ticks)", keys.to_present);
    std::printf("M2 grid keys presented within one frame: %zu of %zu\n", keys.within_frame, input_steps);

    const DataGridViewCellsSnapshot sample = snapshot_from(0U, every_cell);
    const std::size_t snapshot_bytes = sizeof(DataGridViewCellsSnapshot) +
                                       sample.cells.capacity() * sizeof(DataGridViewCellsCell) +
                                       sample.text.capacity() +
                                       sample.lines.capacity() * sizeof(DataGridViewCellsTextRange);
    const std::size_t buffer_bytes =
        static_cast<std::size_t>(region_width) * static_cast<std::size_t>(region_height) * 4U *
        default_live_surface_buffer_count;
    std::printf("M6 memory: LiveSurface %zu bytes (%zu buffers), snapshot %zu bytes (%zu cells), "
                "framebuffer %zu bytes, thread stack: the platform's default\n",
                buffer_bytes, default_live_surface_buffer_count, snapshot_bytes, sample.cells.size(),
                static_cast<std::size_t>(region_width) * static_cast<std::size_t>(region_height) * 4U);

    Spread round_trip;
    Spread thousand;
    message_costs(steps / 4U + 1U, round_trip, thousand);
    report("M7 posted call round trip", round_trip);
    report("M7 1,000-cell snapshot build to publish", thousand);
    std::printf("M8 not applicable: the cell region is not exposed (the grid publishes its cells)\n");

    std::vector<double> idle_stops;
    std::vector<double> busy_stops;
    for (int trial = 0; trial < 9; ++trial) {
        idle_stops.push_back(stop_time(false));
        busy_stops.push_back(stop_time(true));
    }
    report("M9 stop and join, idle", spread_of(idle_stops));
    report("M9 stop and join, busy call + redraw pending", spread_of(busy_stops));
    return 0;
}
