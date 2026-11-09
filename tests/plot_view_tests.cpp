// PlotView, its items and Sparkline (IMGUI_IMPLOT_STUDY B.4; owner rulings
// 1a to 14a): the items' properties, bounds and data sources; the plot read
// from its items in a window (layout, the view taken from the axes and
// auto-fitted to the data); its gestures and keys raising view_changed,
// selection_changed, point_picked and legend_toggled once each; the point
// cursor; the figure node, its summary and the table view; the declared
// interactions; `live` making and removing the sub-surface host; signal
// programs run on the window's motion clock; and Sparkline's ring, bounds
// and paint. Time comes from the test, never the wall clock.

#include "gui_forms/instrument_controls.hpp"
#include "gui_forms/plot_controls.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface_host.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

    int saves{};
    int restores{};
    int polylines{};
    std::size_t polyline_points{};
    int bands{};
    std::size_t markers{};
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
}

void test_ranges_and_legend() {
    Fixture fixture = make_plot("plot.ranges", false);
    Recorder recorder{};
    std::vector<SubscriptionToken> tokens;
    subscribe(*fixture.plot, recorder, tokens);
    Window window(fixture.plot, {400.0, 300.0});
    window.perform_layout();
    require((*fixture.plot).set_x_range(0U, plots::PlotRange{2.0, 4.0}) && recorder.views == 1 &&
                recorder.last_view.x == (plots::PlotRange{2.0, 4.0}),
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
    require(!(*line).hit_test_local(Point{1.0, 1.0}), "a sparkline takes no input");
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
    test_signal_runs_on_motion_clock();
    test_sparkline();
    std::cout << "gui_forms_plot_view_tests passed\n";
    return 0;
}
