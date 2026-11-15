// PlotView, its items and Sparkline (IMGUI_IMPLOT_STUDY B.4; owner rulings
// 1a to 14a): the items' properties, bounds and data sources; the plot read
// from its items in a window (layout, the view taken from the axes and
// auto-fitted to the data); its gestures and keys raising view_changed,
// selection_changed, point_picked and legend_toggled once each; the point
// cursor; the figure node, its summary and the table view; the declared
// interactions; `live` making and removing the sub-surface host, and a live
// plot's repaints sending unchanged copied series without copying them
// (counted with the allocation probe below); signal programs run on the
// window's motion clock; and Sparkline's ring, bounds and paint. Time comes
// from the test, never the wall clock.

#include "gui_forms/instrument_controls.hpp"
#include "gui_forms/plot_controls.hpp"
#include "gui_forms/plots/colormap/colormap.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#include <malloc.h>
#endif

// Counts allocation attempts and bytes on this thread while enabled (the
// PageStrip probe's pattern, tests/page_strip_tests.cpp). Replacement
// allocation is confined to this executable; a live plot's sub-surface
// thread is not counted.
namespace plot_view_allocation_probe {
thread_local bool enabled{};
thread_local std::size_t attempts{};
thread_local std::size_t bytes_requested{};

void count(const std::size_t bytes) {
    if (!enabled) return;
    ++attempts;
    bytes_requested = bytes_requested + bytes;
}

void* allocate(const std::size_t bytes) {
    count(bytes);
    const std::size_t extent = bytes == 0U ? 1U : bytes;
    void* memory = std::malloc(extent);
    if (!memory) throw std::bad_alloc();
    return memory;
}

void* allocate_aligned(const std::size_t bytes, const std::size_t alignment) {
    count(bytes);
    const std::size_t extent = bytes == 0U ? 1U : bytes;
    void* memory = nullptr;
#ifdef _WIN32
    memory = _aligned_malloc(extent, alignment);
#else
    const int status = posix_memalign(&memory, alignment, extent);
    if (status != 0) throw std::bad_alloc();
#endif
    if (!memory) throw std::bad_alloc();
    return memory;
}

void release_aligned(void* const memory) noexcept {
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}

class CountScope final {
public:
    CountScope() {
        attempts = 0U;
        bytes_requested = 0U;
        enabled = true;
    }
    ~CountScope() { enabled = false; }
    CountScope(const CountScope&) = delete;
    CountScope& operator=(const CountScope&) = delete;
};
} // namespace plot_view_allocation_probe

void* operator new(const std::size_t bytes) {
    void* const memory = plot_view_allocation_probe::allocate(bytes);
    return memory;
}
void* operator new[](const std::size_t bytes) {
    void* const memory = plot_view_allocation_probe::allocate(bytes);
    return memory;
}
void operator delete(void* const memory) noexcept { std::free(memory); }
void operator delete[](void* const memory) noexcept { std::free(memory); }
void operator delete(void* const memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* const memory, std::size_t) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment) {
    void* const memory = plot_view_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment) {
    void* const memory = plot_view_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
    return memory;
}
void operator delete(void* const memory, const std::align_val_t) noexcept {
    plot_view_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t) noexcept {
    plot_view_allocation_probe::release_aligned(memory);
}
void operator delete(void* const memory, std::size_t, const std::align_val_t) noexcept {
    plot_view_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, std::size_t, const std::align_val_t) noexcept {
    plot_view_allocation_probe::release_aligned(memory);
}
// The nothrow forms too: every form must come from the same allocator as its
// matching delete, or a sanitizer reports an allocation/deallocation mismatch.
void* operator new(const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = plot_view_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        void* const memory = plot_view_allocation_probe::allocate(bytes);
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* const memory, const std::nothrow_t&) noexcept { std::free(memory); }
void* operator new(const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = plot_view_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void* operator new[](const std::size_t bytes, const std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try {
        void* const memory = plot_view_allocation_probe::allocate_aligned(bytes, static_cast<std::size_t>(alignment));
        return memory;
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}
void operator delete(void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    plot_view_allocation_probe::release_aligned(memory);
}
void operator delete[](void* const memory, const std::align_val_t, const std::nothrow_t&) noexcept {
    plot_view_allocation_probe::release_aligned(memory);
}

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_plot_view_tests: " << message << '\n';
    std::exit(1);
}

bool near(double left, double right) {
    return std::fabs(left - right) <= 1e-9 * std::fmax(1.0, std::fabs(right));
}

// Counts the batched marks drawn and checks saves balance.
class RecordingPainter final : public Painter {
public:
    void save() override { saves = saves + 1; }
    void restore() override { restores = restores + 1; }
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect, double, Color, double) override {}
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_linear_gradient_spread(Rect, Point, Point, std::span<const GradientStop>, GradientSpreadMode) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void stroke_polyline(std::span<const Point> points, const StrokeStyle&) override {
        polylines = polylines + 1;
        polyline_points = polyline_points + points.size();
    }
    void fill_band(std::span<const Point>, std::span<const Point>, Color) override { bands = bands + 1; }
    void draw_markers(std::span<const Point> centers, const MarkerStyle&) override {
        markers = markers + centers.size();
    }
    void fill_rects(std::span<const Rect> rects, Color) override {
        rect_batches = rect_batches + 1;
        rects_drawn = rects_drawn + rects.size();
    }
    void fill_rects_each(std::span<const Rect> rects, std::span<const Color>) override {
        rect_batches = rect_batches + 1;
        rects_drawn = rects_drawn + rects.size();
    }

    int saves{};
    int restores{};
    int polylines{};
    std::size_t polyline_points{};
    int bands{};
    std::size_t markers{};
    int rect_batches{};
    std::size_t rects_drawn{};
};

// Records the plot's events.
struct Recorder final {
    int views{};
    int selections{};
    int picks{};
    int toggles{};
    PlotViewRanges last_view{};
    PlotQuery last_query{};
    PlotPointPick last_pick{};
    std::size_t last_toggle{};

    void on_view(const PlotViewRanges& ranges) {
        views = views + 1;
        last_view = ranges;
    }
    void on_selection(const PlotQuery& query) {
        selections = selections + 1;
        last_query = query;
    }
    void on_pick(const PlotPointPick& pick) {
        picks = picks + 1;
        last_pick = pick;
    }
    void on_toggle(std::size_t position) {
        toggles = toggles + 1;
        last_toggle = position;
    }
};

void subscribe(PlotView& plot, Recorder& recorder, std::vector<SubscriptionToken>& tokens) {
    tokens.push_back(plot.view_changed().subscribe(
        Delegate<const PlotViewRanges&>::bind<Recorder, &Recorder::on_view>(recorder)));
    tokens.push_back(plot.selection_changed().subscribe(
        Delegate<const PlotQuery&>::bind<Recorder, &Recorder::on_selection>(recorder)));
    tokens.push_back(plot.point_picked().subscribe(
        Delegate<const PlotPointPick&>::bind<Recorder, &Recorder::on_pick>(recorder)));
    tokens.push_back(plot.legend_toggled().subscribe(
        Delegate<std::size_t>::bind<Recorder, &Recorder::on_toggle>(recorder)));
}

const FrameTime start{std::chrono::seconds(1000)};

struct Fixture final {
    std::shared_ptr<PlotView> plot{};
    std::shared_ptr<PlotAxisItem> x_axis{};
    std::shared_ptr<PlotAxisItem> y_axis{};
    std::shared_ptr<PlotSeriesItem> series{};
};

// A plot over x [0, 10] and y [-1, 1] with one labelled line of eleven
// points, y = x / 10.
Fixture make_plot(const char* id, bool auto_fit) {
    Fixture fixture{};
    const std::string prefix(id);
    fixture.plot = make_control<PlotView>(StableId(prefix));
    fixture.x_axis = make_control<PlotAxisItem>(StableId(prefix + ".x"));
    (*fixture.x_axis).minimum = 0.0;
    (*fixture.x_axis).maximum = 10.0;
    (*fixture.x_axis).auto_fit = auto_fit;
    fixture.y_axis = make_control<PlotAxisItem>(StableId(prefix + ".y"));
    (*fixture.y_axis).direction = PlotAxisDirection::vertical;
    (*fixture.y_axis).minimum = -1.0;
    (*fixture.y_axis).maximum = 1.0;
    (*fixture.y_axis).auto_fit = auto_fit;
    fixture.series = make_control<PlotSeriesItem>(StableId(prefix + ".load"));
    (*fixture.series).label = std::string("Load");
    std::array<double, 11> xs{};
    std::array<double, 11> ys{};
    for (std::size_t index = 0U; index < xs.size(); ++index) {
        xs[index] = static_cast<double>(index);
        ys[index] = static_cast<double>(index) / 10.0;
    }
    (*fixture.series).set_points(xs, ys);
    (*fixture.plot).add_child(fixture.x_axis);
    (*fixture.plot).add_child(fixture.y_axis);
    (*fixture.plot).add_child(fixture.series);
    return fixture;
}

void test_axis_item() {
    std::shared_ptr<PlotAxisItem> axis = make_control<PlotAxisItem>(StableId("axis"));
    const std::uint64_t before = (*axis).revision();
    (*axis).scale = PlotAxisScale::log10;
    (*axis).follow_span = 5.0;
    require((*axis).revision() > before, "an axis property write bumps the revision");
    const plots::Axis described = (*axis).axis_description(plots::AxisSide::top);
    require(described.kind == plots::ScaleKind::log10 && described.side == plots::AxisSide::top,
            "the description carries the scale and the side");
    require(described.follow.tail && near(described.follow.span, 5.0), "a follow span turns following on");
    require(described.fit == plots::AxisFit::once, "auto_fit fits once");
    bool threw = false;
    try {
        (*axis).minimum = std::nan("");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a non-finite minimum is refused");
    threw = false;
    try {
        (*axis).follow_span = -1.0;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a negative follow span is refused");
    require(!(*axis).semantic_descriptor().exposed && !(*axis).hit_test_local(Point{0.0, 0.0}),
            "an axis item publishes no node and takes no input");
}

void test_series_item() {
    std::shared_ptr<PlotSeriesItem> series = make_control<PlotSeriesItem>(StableId("series"));
    const std::array<double, 3> xs{0.0, 1.0, 2.0};
    const std::array<double, 3> ys{5.0, 6.0, 7.0};
    (*series).set_points(xs, ys);
    require((*series).column_samples().size() == 3U, "set_points copies the points");
    const std::array<double, 2> back_xs{1.5, 3.0};
    const std::array<double, 2> more_ys{8.0, 9.0};
    bool threw = false;
    try {
        (*series).append_points(back_xs, more_ys);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*series).column_samples().size() == 3U, "an append going back in x is refused whole");
    const std::array<double, 2> later_xs{3.0, 4.0};
    (*series).append_points(later_xs, more_ys);
    require((*series).column_samples().size() == 5U && near((*series).column_samples().y(4U), 9.0),
            "an ordered append extends the columns");
    const std::array<double, 4> values{1.0, 2.0, 3.0, 4.0};
    (*series).set_values(values);
    require(near((*series).column_samples().x(3U), 3.0), "set_values implies x = 0, 1, 2, ...");
    (*series).use_ring(3U);
    const std::array<double, 4> ring_xs{0.0, 1.0, 2.0, 3.0};
    (*series).push_points(ring_xs, values);
    require((*series).column_samples().size() == 3U && near((*series).column_samples().x(0U), 1.0),
            "a ring keeps the newest points");
    threw = false;
    try {
        (*series).source = std::string("signal(nonsense)");
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && !(*series).signal().has_value(), "a source that does not parse is refused");
    (*series).source = std::string("signal(sine, frequency 2Hz)");
    require((*series).signal().has_value(), "a parsed source sets the signal program");
    const std::array<double, 2> unequal{1.0, 2.0};
    threw = false;
    try {
        (*series).set_points(unequal, values);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "unequal lengths are refused");
}

void test_layout_and_auto_fit() {
    Fixture fixed = make_plot("plot.fixed", false);
    Window window(fixed.plot, {400.0, 300.0});
    window.perform_layout();
    const Rect plot = (*fixed.plot).plot_bounds();
    require(plot.width > 100.0 && plot.height > 100.0, "the plot rectangle is laid out inside the bounds");
    require((*fixed.plot).x_range(0U) == (plots::PlotRange{0.0, 10.0}) &&
                (*fixed.plot).y_range(0U) == (plots::PlotRange{-1.0, 1.0}),
            "the view is the axis items' ranges");
    // A-gf-controls-10-F7: an axis item replaced by a new one with as many writes is still a
    // change, though the new item may be allocated where the old one was.
    const RuntimeId old_x = (*fixed.x_axis).runtime_id();
    static_cast<void>((*fixed.plot).remove_child(old_x));
    fixed.x_axis.reset();
    std::shared_ptr<PlotAxisItem> replacement = make_control<PlotAxisItem>(StableId("plot.fixed.x2"));
    (*replacement).minimum = 0.0;
    (*replacement).maximum = 20.0;
    (*replacement).auto_fit = false;
    (*fixed.plot).insert_child(replacement, 2U);
    window.perform_layout();
    require((*fixed.plot).x_range(0U) == (plots::PlotRange{0.0, 20.0}), "a replaced axis item's range is taken");

    Fixture fitted = make_plot("plot.fitted", true);
    Window fitted_window(fitted.plot, {400.0, 300.0});
    fitted_window.perform_layout();
    const plots::PlotRange x = (*fitted.plot).x_range(0U);
    const plots::PlotRange y = (*fitted.plot).y_range(0U);
    require(near(x.minimum, -0.2) && near(x.maximum, 10.2), "auto fit pads the x extent by 2 percent");
    require(near(y.minimum, -0.02) && near(y.maximum, 1.02), "auto fit pads the y extent by 2 percent");
}

void test_gestures_raise_events() {
    Fixture fixture = make_plot("plot.gestures", false);
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    Recorder recorder{};
    std::vector<SubscriptionToken> tokens;
    subscribe(*fixture.plot, recorder, tokens);
    const Rect plot = (*fixture.plot).plot_bounds();
    const Point centre{plot.x + plot.width / 2.0, plot.y + plot.height / 2.0};

    PointerEvent wheel{};
    wheel.action = PointerAction::wheel;
    wheel.position = centre;
    wheel.wheel_delta = Point{0.0, 1.0};
    require(window.dispatch_pointer(wheel), "a wheel over the plot is handled");
    require(recorder.views == 1 && (*fixture.plot).x_range(0U).extent() < 10.0,
            "a wheel notch zooms in and raises view_changed once");

    // Ctrl + right drag keeps a query rectangle.
    PointerEvent down{};
    down.action = PointerAction::down;
    down.button = PointerButton::secondary;
    down.modifiers = Modifier::control;
    down.position = Point{plot.x + 20.0, plot.y + 20.0};
    static_cast<void>(window.dispatch_pointer(down));
    PointerEvent move = down;
    move.action = PointerAction::move;
    move.button = PointerButton::none;
    move.position = Point{plot.x + 120.0, plot.y + 90.0};
    static_cast<void>(window.dispatch_pointer(move));
    PointerEvent up = move;
    up.action = PointerAction::up;
    up.button = PointerButton::secondary;
    static_cast<void>(window.dispatch_pointer(up));
    require(recorder.selections == 1 && recorder.last_query.present, "a query drag raises selection_changed once");
    require((*fixture.plot).query().present, "the query is kept");
    (*fixture.plot).clear_query();
    require(recorder.selections == 2 && !recorder.last_query.present, "clearing the query raises it again");

    // A primary click on a point picks it.
    (*fixture.plot).reset_view();
    window.perform_layout();
    const plots::PlotRange x = (*fixture.plot).x_range(0U);
    const plots::PlotRange y = (*fixture.plot).y_range(0U);
    const double point_x = plot.x + (5.0 - x.minimum) / x.extent() * plot.width;
    const double point_y = plot.y + (y.maximum - 0.5) / y.extent() * plot.height;
    PointerEvent click{};
    click.action = PointerAction::down;
    click.button = PointerButton::primary;
    click.position = Point{point_x + 1.0, point_y};
    static_cast<void>(window.dispatch_pointer(click));
    click.action = PointerAction::up;
    static_cast<void>(window.dispatch_pointer(click));
    require(recorder.picks == 1 && recorder.last_pick.element == 5U && near(recorder.last_pick.y, 0.5),
            "a click on a point raises point_picked with that point");
}

void test_keyboard_cursor() {
    Fixture fixture = make_plot("plot.keys", false);
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    require(window.request_focus(fixture.plot), "the plot takes focus");
    Recorder recorder{};
    std::vector<SubscriptionToken> tokens;
    subscribe(*fixture.plot, recorder, tokens);
    KeyEvent right{};
    right.physical_key = PhysicalKey::right;
    require(window.dispatch_key(right), "an arrow is handled");
    require(recorder.picks == 1 && recorder.last_pick.series == 0U && recorder.last_pick.element == 0U,
            "the first arrow places the cursor on the first point");
    static_cast<void>(window.dispatch_key(right));
    require(recorder.picks == 2 && recorder.last_pick.element == 1U, "right moves the cursor one point");
    KeyEvent shifted = right;
    shifted.modifiers = Modifier::shift;
    static_cast<void>(window.dispatch_key(shifted));
    require(recorder.last_pick.element == 10U, "Shift moves ten points, clamped to the last");
    require(recorder.views == 0, "moving the cursor does not move the view");
    KeyEvent zoom{};
    zoom.physical_key = PhysicalKey::equal;
    static_cast<void>(window.dispatch_key(zoom));
    require(recorder.views == 1 && (*fixture.plot).x_range(0U).extent() < 10.0, "+ zooms in");
    KeyEvent home{};
    home.physical_key = PhysicalKey::home;
    static_cast<void>(window.dispatch_key(home));
    require(recorder.views == 2, "Home fits and raises view_changed");
    const std::optional<PlotPointPick> at = (*fixture.plot).cursor();
    require(at.has_value() && (*at).element == 10U && near((*at).y, 1.0), "the cursor names its point");
    require((*fixture.plot).semantic_descriptor().value == "10, 1", "the figure's value is the cursor's point");

    // A-gf-controls-10-F2: a cursor on a series that was removed is dropped
    // at the next arrow, which places it afresh on the first point.
    std::shared_ptr<PlotSeriesItem> second = make_control<PlotSeriesItem>(StableId("plot.keys.second"));
    std::array<double, 3> second_xs{1.0, 2.0, 3.0};
    std::array<double, 3> second_ys{0.1, 0.2, 0.3};
    (*second).set_points(second_xs, second_ys);
    (*fixture.plot).add_child(second);
    window.perform_layout();
    KeyEvent down{};
    down.physical_key = PhysicalKey::down;
    static_cast<void>(window.dispatch_key(down));
    require(recorder.last_pick.series == 1U, "Down moves the cursor to the second series");
    const Control::Ptr removed = (*fixture.plot).remove_child((*second).runtime_id());
    window.perform_layout();
    const int picks_before = recorder.picks;
    require(window.dispatch_key(right) && recorder.picks == picks_before + 1 && recorder.last_pick.series == 0U &&
                recorder.last_pick.element == 0U,
            "after its series is removed, an arrow places the cursor afresh on the first point");
    static_cast<void>(removed);
}

void test_ranges_and_legend() {
    Fixture fixture = make_plot("plot.ranges", false);
    Recorder recorder{};
    std::vector<SubscriptionToken> tokens;
    subscribe(*fixture.plot, recorder, tokens);
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    require((*fixture.plot).set_x_range(0U, plots::PlotRange{2.0, 4.0}) && recorder.views == 1 &&
                near(recorder.last_view.x_minimum, 2.0) && near(recorder.last_view.x_maximum, 4.0),
            "set_x_range moves the view and raises view_changed");
    require(!(*fixture.plot).set_x_range(0U, plots::PlotRange{4.0, 2.0}) && recorder.views == 1,
            "an inverted range is refused, raising nothing");
    require(!(*fixture.plot).set_y_range(3U, plots::PlotRange{0.0, 1.0}), "a missing axis is refused");
    // An axis item's write after the view moved keeps the moved range.
    (*fixture.x_axis).label = std::string("Time");
    window.perform_layout();
    require((*fixture.plot).x_range(0U) == (plots::PlotRange{2.0, 4.0}), "a moved view survives an item change");
    (*fixture.plot).reset_view();
    window.perform_layout();
    require((*fixture.plot).x_range(0U) == (plots::PlotRange{0.0, 10.0}), "reset_view takes the items' ranges");
    (*fixture.plot).toggle_series(0U);
    require(recorder.toggles == 1 && recorder.last_toggle == 0U && (*fixture.series).hidden.read(),
            "toggling a series hides it and raises legend_toggled");
    (*fixture.plot).toggle_series(7U);
    require(recorder.toggles == 1, "toggling a missing series does nothing");
}

void test_semantics_and_table() {
    Fixture fixture = make_plot("plot.semantics", false);
    (*fixture.plot).title = std::string("Load over time");
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    const SemanticDescriptor descriptor = (*fixture.plot).semantic_descriptor();
    require(descriptor.role == SemanticRole::figure && descriptor.exposed, "the plot is one figure node");
    require(descriptor.name == "Load over time", "the title names the figure");
    require(descriptor.description.find("Plot with 1 series: Load (line, 11 points)") == 0U,
            "the summary names the series, its kind and its size");
    require(descriptor.description.find("x from 0 to 10, y from -1 to 1.") != std::string::npos,
            "the summary gives the visible ranges");
    require((*fixture.plot).table_row_count() == 11U, "the table holds a row per point");
    const std::string table = (*fixture.plot).table_text();
    require(table.find("Load\t0\t0\nLoad\t1\t0.1\n") == 0U, "the table's rows are series, x and y");
    std::vector<InteractionDeclaration> interactions;
    (*fixture.plot).append_interactions(interactions);
    int found = 0;
    for (std::size_t index = 0U; index < interactions.size(); ++index) {
        const std::string_view name = interactions[index].name;
        if (name == "view_changed" || name == "selection_changed" || name == "point_picked" ||
            name == "legend_toggled") {
            found = found + 1;
        }
    }
    require(found == 4, "the plot declares its four events for rules");
    require((*fixture.plot).parts().plot.present && (*fixture.plot).parts().title.present,
            "the anatomy publishes the plot and the title");
}

void test_paint_draws_series() {
    Fixture fixture = make_plot("plot.paint", false);
    std::shared_ptr<PlotSeriesItem> dots = make_control<PlotSeriesItem>(StableId("plot.paint.dots"));
    (*dots).kind = PlotSeriesKind::scatter;
    const std::array<double, 3> xs{1.0, 2.0, 3.0};
    const std::array<double, 3> ys{0.1, 0.2, 0.3};
    (*dots).set_points(xs, ys);
    (*fixture.plot).add_child(dots);
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    RecordingPainter painter;
    (*fixture.plot).on_paint(painter, Rect{0.0, 0.0, 400.0, 300.0});
    require(painter.saves == painter.restores, "saves and restores balance");
    require(painter.polylines >= 1 && painter.polyline_points >= 2U, "the line is drawn as a polyline");
    require(painter.markers >= 3U, "the scatter series draws its markers");
    (*fixture.series).hidden = true;
    RecordingPainter hidden;
    (*fixture.plot).on_paint(hidden, Rect{0.0, 0.0, 400.0, 300.0});
    require(hidden.polylines == 0, "a hidden series is not drawn");
}

void test_live_host() {
    Fixture fixture = make_plot("plot.live", false);
    require((*fixture.plot).live_host() == nullptr, "a plot starts on the window's thread");
    (*fixture.plot).live = true;
    SubSurfaceHost* const host = (*fixture.plot).live_host();
    require(host != nullptr && (*host).parent().get() == fixture.plot.get(), "live makes the sub-surface host a child");
    require(!(*host).publishes_semantics(), "the host leaves the figure node to the plot");
    require((*fixture.plot).semantic_descriptor().role == SemanticRole::figure, "a live plot is still a figure");
    (*fixture.plot).live = false;
    require((*fixture.plot).live_host() == nullptr, "live off removes the host");
}

// What one repaint of a plot allocated on this thread.
struct RepaintAllocations final {
    std::size_t attempts{};
    std::size_t bytes{};
};

RepaintAllocations count_repaint(PlotView& plot) {
    RecordingPainter painter;
    RepaintAllocations counted{};
    {
        plot_view_allocation_probe::CountScope probe{};
        plot.on_paint(painter, Rect{0.0, 0.0, 400.0, 300.0});
        counted.attempts = plot_view_allocation_probe::attempts;
        counted.bytes = plot_view_allocation_probe::bytes_requested;
    }
    return counted;
}

// A live plot sends its state with every repaint; a copied-in series
// travels as the shared columns made when its points last changed, so a
// repaint that leaves the points costs no copy: neither a copy count nor
// bytes in proportion to the series. A change of the points makes exactly
// one copy, and the repaints after it none again.
void test_live_repaint_copies_only_changed_columns() {
    Fixture fixture = make_plot("plot.live-copies", false);
    constexpr std::size_t point_count = 100000U;
    std::vector<double> xs(point_count);
    std::vector<double> ys(point_count);
    for (std::size_t index = 0U; index < point_count; ++index) {
        xs[index] = static_cast<double>(index) * 1e-4;
        ys[index] = std::sin(xs[index]);
    }
    (*fixture.series).set_points(xs, ys);
    (*fixture.plot).live = true;
    require((*fixture.plot).live_host() != nullptr, "the plot is live");
    const std::uint64_t made = (*fixture.plot).live_column_copies();
    require(made == 1U, "going live copies the copied-in series once");
    const std::size_t series_bytes = point_count * 2U * sizeof(double);

    // One repaint first, so storage the plot reuses has its size.
    static_cast<void>(count_repaint(*fixture.plot));
    const RepaintAllocations first = count_repaint(*fixture.plot);
    const RepaintAllocations second = count_repaint(*fixture.plot);
    require((*fixture.plot).live_column_copies() == made, "an unchanged repaint copies no series");
    require(first.bytes < series_bytes / 16U && second.bytes < series_bytes / 16U,
            "an unchanged repaint allocates nothing in proportion to the series");
    require(first.attempts == second.attempts && first.bytes == second.bytes,
            "unchanged repaints allocate the same, repaint after repaint");

    // A label is not data: still no copy.
    (*fixture.series).label = std::string("renamed");
    static_cast<void>(count_repaint(*fixture.plot));
    require((*fixture.plot).live_column_copies() == made, "a label change copies no series");

    for (std::size_t index = 0U; index < point_count; ++index) ys[index] = std::cos(xs[index]);
    (*fixture.series).set_points(xs, ys);
    const RepaintAllocations changed = count_repaint(*fixture.plot);
    require((*fixture.plot).live_column_copies() == made + 1U, "changed points are copied once");
    require(changed.bytes >= series_bytes, "the probe sees the one copy of the changed points");
    const RepaintAllocations after = count_repaint(*fixture.plot);
    require((*fixture.plot).live_column_copies() == made + 1U && after.bytes < series_bytes / 16U,
            "the repaints after a change copy nothing again");
    (*fixture.plot).live = false;
}

void test_signal_runs_on_motion_clock() {
    Fixture fixture = make_plot("plot.signal", false);
    (*fixture.series).source = std::string("signal(sine, frequency 2Hz)");
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    MotionClock& clock = window.motion_clock();
    RecordingPainter first;
    (*fixture.plot).on_paint(first, Rect{0.0, 0.0, 400.0, 300.0});
    require(clock.control_ambient(*fixture.plot) && window.next_wake().has_value(),
            "a plot with a signal is enlisted on the motion clock");
    const std::uint64_t before = (*fixture.series).revision();
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    RecordingPainter second;
    (*fixture.plot).on_paint(second, Rect{0.0, 0.0, 400.0, 300.0});
    require(window.next_wake().has_value(), "the plot stays enlisted while drawn");
    static_cast<void>(window.poll_frame_schedule(*window.next_wake() + std::chrono::milliseconds(50)));
    require((*fixture.plot).frames_run() >= 1U, "the plot runs frames");
    require((*fixture.series).revision() > before, "the signal writes samples into the series");
    (*fixture.series).source = std::string();
    RecordingPainter third;
    (*fixture.plot).on_paint(third, Rect{0.0, 0.0, 400.0, 300.0});
    require(!clock.control_ambient(*fixture.plot), "without a producer the plot leaves the clock");
}

// A plot that is not painted between two frames leaves the motion clock;
// a paint enlists it again (hidden tabs stop the clock).
void test_unpainted_plot_leaves_motion_clock() {
    Fixture fixture = make_plot("plot.hidden", false);
    (*fixture.series).source = std::string("signal(sine, frequency 2Hz)");
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    MotionClock& clock = window.motion_clock();
    RecordingPainter first;
    (*fixture.plot).on_paint(first, Rect{0.0, 0.0, 400.0, 300.0});
    require(clock.control_ambient(*fixture.plot), "a painted plot with a signal is enlisted");
    // No paint from here: after at most one frame the clock finds the plot
    // unshown and drops it (a few wakes allow for other scheduled work).
    for (std::size_t wake_index = 0U; wake_index < 4U && clock.control_ambient(*fixture.plot); ++wake_index) {
        const std::optional<FrameTime> wake = window.next_wake();
        if (!wake.has_value()) break;
        static_cast<void>(window.poll_frame_schedule(*wake + std::chrono::milliseconds(50)));
    }
    const std::uint64_t frames = (*fixture.plot).frames_run();
    require(frames <= 1U, "an unpainted plot runs at most the frame after its paint");
    require(!clock.control_ambient(*fixture.plot), "an unpainted plot leaves the clock");
    RecordingPainter again;
    (*fixture.plot).on_paint(again, Rect{0.0, 0.0, 400.0, 300.0});
    require(clock.control_ambient(*fixture.plot), "a paint enlists the plot again");
}

// A PlotLinkGroup shares first-axis ranges: a member's change reaches the
// others once, and the echoes are ignored.
void test_link_group() {
    Fixture first = make_plot("plot.link-a", false);
    Fixture second = make_plot("plot.link-b", false);
    const std::shared_ptr<Control> root = make_control<Control>(StableId("plot.links"));
    (*first.plot).set_requested_bounds(Rect{0.0, 0.0, 400.0, 150.0});
    (*second.plot).set_requested_bounds(Rect{0.0, 150.0, 400.0, 150.0});
    (*root).add_child(first.plot);
    (*root).add_child(second.plot);
    Window window(root, {400.0, 300.0});
    window.perform_layout();
    Recorder recorder{};
    std::vector<SubscriptionToken> tokens;
    subscribe(*second.plot, recorder, tokens);
    PlotLinkGroup group(PlotLinkAxes::x, plots::PlotRange{0.0, 10.0}, plots::PlotRange{-1.0, 1.0});
    group.add(first.plot);
    group.add(second.plot);
    require(group.member_count() == 2U, "both plots joined");
    bool threw = false;
    try {
        group.add(first.plot);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && group.member_count() == 2U, "a plot joins once");
    const std::uint64_t views_before = static_cast<std::uint64_t>(recorder.views);
    require((*first.plot).set_x_range(0U, plots::PlotRange{2.0, 5.0}), "the first plot moves");
    require((*second.plot).x_range(0U) == (plots::PlotRange{2.0, 5.0}), "the linked x range reaches the other plot");
    require(group.x_link().range() == (plots::PlotRange{2.0, 5.0}), "the link holds the range");
    require(static_cast<std::uint64_t>(recorder.views) == views_before + 1U && group.echoes() >= 1U,
            "the other plot raises view_changed once, which the group ignores");
    require((*second.plot).y_range(0U) == (plots::PlotRange{-1.0, 1.0}), "y is not linked");
    require((*second.plot).set_y_range(0U, plots::PlotRange{0.0, 0.5}) &&
                (*first.plot).y_range(0U) == (plots::PlotRange{-1.0, 1.0}),
            "an unlinked axis stays apart");
    group.set_axes(PlotLinkAxes::both);
    require((*first.plot).y_range(0U) == group.y_link().range() &&
                (*second.plot).y_range(0U) == group.y_link().range(),
            "linking y gives every member the y link's range");

    // A-gf-controls-10-F5: a member disposed while still held is skipped, so
    // a pan on another member neither throws nor leaves the group stuck in
    // its propagating state.
    Fixture third = make_plot("plot.link-c", false);
    group.add(third.plot);
    (*third.plot).dispose();
    bool propagation_threw = false;
    try {
        static_cast<void>((*first.plot).set_x_range(0U, plots::PlotRange{1.0, 4.0}));
    } catch (const std::exception&) {
        propagation_threw = true;
    }
    require(!propagation_threw && (*second.plot).x_range(0U) == (plots::PlotRange{1.0, 4.0}),
            "a disposed member is skipped and the others still follow");
    require((*second.plot).set_x_range(0U, plots::PlotRange{3.0, 6.0}) &&
                (*first.plot).x_range(0U) == (plots::PlotRange{3.0, 6.0}),
            "the group keeps linking after skipping a disposed member");
    Fixture fourth = make_plot("plot.link-d", false);
    (*fourth.plot).dispose();
    bool disposed_refused = false;
    try {
        group.add(fourth.plot);
    } catch (const std::invalid_argument&) {
        disposed_refused = true;
    }
    require(disposed_refused && group.member_count() == 3U, "a disposed plot cannot join a group");
}

// resolve_plot_palette and plot_series_color give a custom kind PlotView's
// colours.
void test_plot_palette() {
    Fixture fixture = make_plot("plot.palette", false);
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    const PlotPalette palette = resolve_plot_palette(*fixture.plot);
    require(palette.grid_major.alpha == 96U && palette.grid_minor.alpha == 40U, "the grid takes reduced alphas");
    require(palette.grid_major.red == palette.axis.red && palette.grid_major.green == palette.axis.green &&
                palette.grid_major.blue == palette.axis.blue,
            "grid and axis share the rule colour");
    require(palette.selection_edge.red == palette.selection_fill.red && palette.selection_fill.alpha == 48U,
            "the selection fill is its edge at low alpha");
    const Color first_series = plot_series_color(*fixture.plot, 0U);
    require(first_series.alpha == 255U && !(first_series == plot_series_color(*fixture.plot, 1U)),
            "series colours are opaque and differ by position");
    require(resolve_plot_palette(*fixture.plot) == palette, "resolving reads only");
    // The flame graph demonstrator's F7: a category's accent is the series
    // colour at its position, the same for the same name.
    const std::uint64_t key = plots::category_key("parse_header");
    require(plot_category_color(*fixture.plot, key) ==
                plot_series_color(*fixture.plot, plots::category_position(key, 256U)),
            "a category takes the series colour at its position");
    require(plot_category_color(*fixture.plot, key) == plot_category_color(*fixture.plot, plots::category_key("parse_header")),
            "equal names share a colour");
}

// A custom kind gets PlotView's frame: it fits, paints through the plot's
// mark painter and picks; the stock heatmap and histogram are custom kinds.
void test_custom_series() {
    Fixture fixture = make_plot("plot.custom", true);
    const std::shared_ptr<PlotHistogramSeries> histogram = std::make_shared<PlotHistogramSeries>();
    const std::array<double, 7> samples{0.0, 1.0, 1.0, 2.0, 2.0, 2.0, 4.0};
    (*histogram).set_samples(samples, 4U);
    require((*histogram).counts().size() == 4U && (*histogram).counts()[0] == 1U && (*histogram).counts()[1] == 2U &&
                (*histogram).counts()[2] == 3U && (*histogram).counts()[3] == 1U,
            "the histogram bins its samples over their extent");
    (*fixture.series).set_custom(histogram);
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    (*fixture.plot).fit_view();
    require((*fixture.plot).x_range(0U).minimum <= 0.0 && (*fixture.plot).x_range(0U).maximum >= 4.0,
            "the plot fits to the custom extent");
    RecordingPainter painter;
    (*fixture.plot).on_paint(painter, Rect{0.0, 0.0, 400.0, 300.0});
    require(painter.rect_batches >= 1 && painter.polylines == 0, "the histogram draws bars, not the item's line");
    require(painter.saves == painter.restores, "saves balance");

    const std::shared_ptr<PlotHeatmapSeries> heatmap = std::make_shared<PlotHeatmapSeries>();
    const std::array<double, 6> cells{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    (*heatmap).set_grid(cells, 2U, 3U, plots::PlotRange{0.0, 3.0}, plots::PlotRange{0.0, 2.0});
    require((*heatmap).domain() == (plots::PlotRange{1.0, 6.0}), "the domain follows the values");
    bool threw = false;
    try {
        (*heatmap).set_grid(cells, 4U, 2U, plots::PlotRange{0.0, 3.0}, plots::PlotRange{0.0, 2.0});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*heatmap).rows() == 2U, "a mismatched grid is refused, changing nothing");
    (*fixture.series).set_custom(heatmap);
    (*fixture.plot).reset_view();
    window.perform_layout();
    (*fixture.plot).fit_view();
    RecordingPainter heat;
    (*fixture.plot).on_paint(heat, Rect{0.0, 0.0, 400.0, 300.0});
    require(heat.rects_drawn >= 6U, "the heatmap draws a rectangle per cell");
    // A pick names the cell under the pointer: row 0 is at the top.
    plots::Axis x_axis{};
    x_axis.range = plots::PlotRange{0.0, 3.0};
    plots::Axis y_axis{};
    y_axis.range = plots::PlotRange{0.0, 2.0};
    y_axis.side = plots::AxisSide::left;
    const plots::PlotTransformResult made = plots::PlotTransform::make(x_axis, y_axis, Rect{0.0, 0.0, 30.0, 20.0}, 1.0);
    require(made.check == plots::PlotTransformCheck::valid, "the test transform is valid");
    const plots::PickResult top_left = (*heatmap).pick(Point{5.0, 5.0}, made.transform);
    require(top_left.found && top_left.element == plots::ElementIndex{0U} && near(top_left.y, 1.0),
            "the top-left cell is element 0");
    const plots::PickResult bottom_right = (*heatmap).pick(Point{25.0, 15.0}, made.transform);
    require(bottom_right.found && bottom_right.element == plots::ElementIndex{5U} && near(bottom_right.y, 6.0),
            "the bottom-right cell is the last element");
    (*fixture.series).set_custom(nullptr);
    RecordingPainter plain;
    (*fixture.plot).on_paint(plain, Rect{0.0, 0.0, 400.0, 300.0});
    require(plain.polylines >= 1, "without a custom kind the item's line draws again");
}

void test_sparkline() {
    std::shared_ptr<Sparkline> line = make_control<Sparkline>(StableId("spark"));
    require((*line).capacity.read() == 120U && !(*line).latest().has_value(), "a sparkline starts empty");
    const std::array<double, 5> values{3.0, 1.0, std::nan(""), 4.0, 2.0};
    (*line).push_values(values);
    require((*line).latest().has_value() && near(*(*line).latest(), 2.0), "latest is the newest finite value");
    require((*line).extent() == (plots::PlotRange{1.0, 4.0}), "the extent ignores gaps");
    bool threw = false;
    try {
        (*line).push(std::numeric_limits<double>::infinity());
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*line).values().size() == 5U, "an infinity is refused, changing nothing");
    (*line).capacity = 2U;
    require((*line).values().size() == 2U && near((*line).values().at(plots::ElementIndex{0U}), 4.0),
            "a smaller capacity keeps the newest values");
    threw = false;
    try {
        (*line).capacity = 1U;
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*line).capacity.read() == 2U, "a capacity under 2 is refused");
    (*line).fill = true;
    Window window(line, {80.0, 20.0});
    window.perform_layout();
    RecordingPainter painter;
    (*line).on_paint(painter, Rect{0.0, 0.0, 80.0, 20.0});
    require(painter.polylines == 1 && painter.bands == 1 && painter.markers == 1U,
            "the sparkline draws its band, its line and the newest dot");
    const SemanticDescriptor descriptor = (*line).semantic_descriptor();
    require(descriptor.role == SemanticRole::figure && descriptor.value == "2", "the value is the newest one");
    // A-gf-controls-05-F5: the description is written in the C locale's "%.6g" form.
    require(descriptor.description == "Trend of 2 values from 2 to 4", "the description names the count and range");
    require(!(*line).hit_test_local(Point{1.0, 1.0}), "a sparkline takes no input");
}

// Records the handle events.
struct HandleRecorder final {
    int moves{};
    int commits{};
    PlotHandleMove last_move{};
    PlotHandleMove last_commit{};
    void moved(const PlotHandleMove& move) {
        moves = moves + 1;
        last_move = move;
    }
    void committed(const PlotHandleMove& move) {
        commits = commits + 1;
        last_commit = move;
    }
};

void drag_pointer(Window& window, Point from, Point to) {
    PointerEvent down{};
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = from;
    static_cast<void>(window.dispatch_pointer(down));
    PointerEvent move = down;
    move.action = PointerAction::move;
    move.button = PointerButton::none;
    move.position = Point{(from.x + to.x) * 0.5, (from.y + to.y) * 0.5};
    static_cast<void>(window.dispatch_pointer(move));
    move.position = to;
    static_cast<void>(window.dispatch_pointer(move));
    PointerEvent up = move;
    up.action = PointerAction::up;
    up.button = PointerButton::primary;
    static_cast<void>(window.dispatch_pointer(up));
}

void test_handles_drag_on_the_drag_engine() {
    Fixture fixture = make_plot("plot.handles", false);
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    const plots::PlotRange x = (*fixture.plot).x_range(0U);
    const plots::PlotRange y = (*fixture.plot).y_range(0U);
    const double middle_x = x.minimum + x.extent() * 0.5;
    const double middle_y = y.minimum + y.extent() * 0.5;
    std::vector<PlotHandle> handles;
    handles.push_back(PlotHandle{1U, PlotHandleKind::point, middle_x, middle_y, true});
    handles.push_back(PlotHandle{2U, PlotHandleKind::vertical_line, x.minimum + x.extent() * 0.25, 0.0, true});
    std::vector<PlotHandle> repeated = handles;
    repeated.push_back(PlotHandle{1U, PlotHandleKind::point, 0.0, 0.0, true});
    bool refused = false;
    try {
        (*fixture.plot).set_handles(repeated);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*fixture.plot).handles().empty(), "a repeated handle id is refused, changing nothing");
    (*fixture.plot).set_handles(handles);
    HandleRecorder recorder{};
    SubscriptionToken moved = (*fixture.plot).handle_moved().subscribe(
        Delegate<const PlotHandleMove&>::bind<HandleRecorder, &HandleRecorder::moved>(recorder));
    SubscriptionToken committed = (*fixture.plot).handle_committed().subscribe(
        Delegate<const PlotHandleMove&>::bind<HandleRecorder, &HandleRecorder::committed>(recorder));
    const std::optional<Point> point = (*fixture.plot).handle_point(1U);
    require(point.has_value(), "a laid-out plot places its handles");
    const plots::PlotRange before = (*fixture.plot).x_range(0U);
    drag_pointer(window, *point, Point{(*point).x + 40.0, (*point).y - 30.0});
    require(recorder.moves >= 1 && recorder.commits == 1 && recorder.last_commit.id == 1U,
            "dragging a point raises handle_moved, then handle_committed once");
    require((*fixture.plot).handles()[0].x > middle_x && (*fixture.plot).handles()[0].y > middle_y,
            "the point follows the pointer right and up");
    require((*fixture.plot).x_range(0U) == before, "a handle drag does not pan the plot");
    const std::optional<Point> line = (*fixture.plot).handle_point(2U);
    require(line.has_value(), "the line has a grab point");
    const double line_y = (*fixture.plot).handles()[1].y;
    drag_pointer(window, *line, Point{(*line).x + 30.0, (*line).y + 50.0});
    require(recorder.commits == 2 && (*fixture.plot).handles()[1].x > x.minimum + x.extent() * 0.25 &&
                (*fixture.plot).handles()[1].y == line_y,
            "a vertical line moves across x only");
    require(!(*fixture.plot).dragged_handle().has_value(), "no handle is dragged after the release");
    moved.disconnect();
    committed.disconnect();
}

void test_paint_sparkline_without_control() {
    const std::array<double, 4> values{1.0, std::nan(""), 3.0, 2.0};
    plots::SeriesSpans<double> spans{};
    spans.first = values;
    SparklineStyle style{};
    style.line = Color{10U, 20U, 30U, 255U};
    SparklineWorkspace workspace{};
    RecordingPainter painter;
    paint_sparkline(painter, Rect{0.0, 0.0, 60.0, 16.0}, spans, 8U, 1.0, style, workspace);
    require(painter.polylines >= 1 && painter.bands == 0 && painter.markers == 1U,
            "paint_sparkline draws the line and the newest dot, no band unless asked");
    require(painter.saves == painter.restores && workspace.reserved_columns > 0U,
            "paint_sparkline restores the painter and reserves its workspace");
    const std::size_t reserved = workspace.reserved_columns;
    style.fill = true;
    style.newest_marker_size = 0.0;
    RecordingPainter filled;
    paint_sparkline(filled, Rect{0.0, 0.0, 60.0, 16.0}, spans, 8U, 1.0, style, workspace);
    require(filled.bands == 1 && filled.markers == 0U && workspace.reserved_columns == reserved,
            "fill shades the band, a zero dot size draws no dot, and the workspace is reused");
    RecordingPainter refused;
    paint_sparkline(refused, Rect{0.0, 0.0, 60.0, 16.0}, spans, 3U, 1.0, style, workspace);
    const std::array<double, 2> gaps{std::nan(""), std::nan("")};
    plots::SeriesSpans<double> empty{};
    empty.first = gaps;
    paint_sparkline(refused, Rect{0.0, 0.0, 60.0, 16.0}, empty, 8U, 1.0, style, workspace);
    paint_sparkline(refused, Rect{0.0, 0.0, 0.0, 16.0}, spans, 8U, 1.0, style, workspace);
    require(refused.polylines == 0 && refused.bands == 0 && refused.markers == 0U && refused.saves == 0,
            "too few slots, no finite value or an empty plot draws nothing");
}

} // namespace

int main() {
    test_axis_item();
    test_series_item();
    test_layout_and_auto_fit();
    test_gestures_raise_events();
    test_keyboard_cursor();
    test_ranges_and_legend();
    test_semantics_and_table();
    test_paint_draws_series();
    test_live_host();
    test_live_repaint_copies_only_changed_columns();
    test_signal_runs_on_motion_clock();
    test_unpainted_plot_leaves_motion_clock();
    test_link_group();
    test_plot_palette();
    test_custom_series();
    test_sparkline();
    test_paint_sparkline_without_control();
    test_handles_drag_on_the_drag_engine();
    std::cout << "gui_forms_plot_view_tests passed\n";
    return 0;
}
