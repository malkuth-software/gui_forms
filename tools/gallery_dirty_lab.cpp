// Gallery dirty lab (property-capabilities study, M0 measurement): what the
// shared dirty structure (M0.1 to M0.4) costs on the real Gallery. For each
// page the Gallery shows, with the page selected and settled:
//
//   paint sweep    every attached control takes invalidate(Dirty::paint),
//                  then one flush and take_damage settle it;
//   measure sweep  the same with Dirty::measure (the layout pass runs);
//   one write      a single paint mark on the page's deepest control, then
//                  the settle;
//   resize         the window is widened, made taller, and both, from its
//                  default size, each step followed by a flush; the
//                  arranged-control and resize_relayouts counts are the
//                  metrics' differences over the three steps.
//
// Not a test; a measurement program. Each timing is the fastest of several
// rounds; one line per page, then the totals.

#include "demo/gallery.hpp"
#include "demo/gallery_page_specs.hpp"
#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {

using gui_forms::Control;
using gui_forms::Dirty;
using gui_forms::MetricsSnapshot;
using gui_forms::Size;
using gui_forms::Window;
using Clock = std::chrono::steady_clock;

constexpr std::size_t rounds = 5U;

double microseconds_since(Clock::time_point start) {
    const std::chrono::duration<double, std::micro> elapsed = Clock::now() - start;
    return elapsed.count();
}

// Appends `control` and its descendants, depth first, with their depths.
// `controls` and `depths` are the caller's storage and grow together.
void collect(const Control::Ptr& control, std::size_t depth, std::vector<Control::Ptr>& controls,
             std::vector<std::size_t>& depths) {
    controls.push_back(control);
    depths.push_back(depth);
    for (const Control::Ptr& child : (*control).children()) {
        if (child) collect(child, depth + 1U, controls, depths);
    }
}

void settle(Window& window) {
    window.flush();
    static_cast<void>(window.take_damage());
}

// One sweep: every control takes `dirty`, then the settle. Returns the
// fastest round's whole time in microseconds.
double sweep(Window& window, const std::vector<Control::Ptr>& controls, Dirty dirty) {
    double best = 1.0e300;
    for (std::size_t round = 0U; round < rounds; ++round) {
        const Clock::time_point start = Clock::now();
        for (const Control::Ptr& control : controls) (*control).invalidate(dirty);
        settle(window);
        best = std::min(best, microseconds_since(start));
    }
    return best;
}

double one_write(Window& window, Control& control) {
    double best = 1.0e300;
    for (std::size_t round = 0U; round < rounds; ++round) {
        const Clock::time_point start = Clock::now();
        control.invalidate(Dirty::paint);
        settle(window);
        best = std::min(best, microseconds_since(start));
    }
    return best;
}

struct ResizeResult final {
    double microseconds{};
    std::uint64_t arranged{};
    std::uint64_t relayouts{};
};

// Three steps (wider, taller, both back) from the default size; one round,
// since each step changes the state the next measures.
ResizeResult resize_steps(Window& window) {
    const Size base = gui_forms::gallery::gallery_default_size;
    const std::array<Size, 3> steps{{{base.width + 40.0, base.height},
                                     {base.width + 40.0, base.height + 30.0},
                                     {base.width, base.height}}};
    const MetricsSnapshot before = window.metrics_snapshot();
    const Clock::time_point start = Clock::now();
    for (const Size step : steps) {
        window.resize(step);
        settle(window);
    }
    const double elapsed = microseconds_since(start);
    const MetricsSnapshot after = window.metrics_snapshot();
    ResizeResult result{};
    result.microseconds = elapsed;
    result.arranged = after.controls_arranged - before.controls_arranged;
    result.relayouts = after.resize_relayouts - before.resize_relayouts;
    return result;
}

// --repeat: the paint sweep on the first page, again and again, one line
// per sweep (a sweep that grows slower over repeats is owed work piling up).
int repeat_paint_sweeps(Window& window) {
    std::vector<Control::Ptr> controls{};
    std::vector<std::size_t> depths{};
    collect(window.root(), 0U, controls, depths);
    for (std::size_t repeat = 0U; repeat < 40U; ++repeat) {
        const double paint = sweep(window, controls, Dirty::paint);
        std::cout << "repeat " << repeat << ": paint sweep " << paint << " us\n";
    }
    return 0;
}

} // namespace

int main(int argument_count, char** arguments) {
    std::unique_ptr<Window> owned = gui_forms::gallery::make_gallery();
    Window& window = *owned;
    settle(window);
    std::cout << std::fixed << std::setprecision(1);
    if (argument_count > 1 && std::string_view(arguments[1]) == "--repeat") return repeat_paint_sweeps(window);
    std::cout << "page controls depth | paint-sweep us | measure-sweep us | one-write us | "
                 "resize us arranged relayouts\n";
    std::size_t total_controls = 0U;
    double total_paint = 0.0;
    double total_measure = 0.0;
    double total_resize = 0.0;
    std::uint64_t total_arranged = 0U;
    std::uint64_t total_relayouts = 0U;
    std::vector<Control::Ptr> controls{};
    std::vector<std::size_t> depths{};
    for (const gui_forms::gallery::GalleryPageSpec& page : gui_forms::gallery::gallery_pages) {
        if (!gui_forms::gallery::select_gallery_page(window, page.id)) continue;
        settle(window);
        controls.clear();
        depths.clear();
        collect(window.root(), 0U, controls, depths);
        const std::vector<std::size_t>::const_iterator deepest = std::max_element(depths.begin(), depths.end());
        const std::size_t deepest_index = static_cast<std::size_t>(deepest - depths.begin());
        const double paint = sweep(window, controls, Dirty::paint);
        const double measure = sweep(window, controls, Dirty::measure);
        const double single = one_write(window, *controls[deepest_index]);
        const ResizeResult resized = resize_steps(window);
        std::cout << page.id << ' ' << controls.size() << ' ' << *deepest << " | " << paint << " | " << measure
                  << " | " << single << " | " << resized.microseconds << ' ' << resized.arranged << ' '
                  << resized.relayouts << '\n';
        total_controls += controls.size();
        total_paint += paint;
        total_measure += measure;
        total_resize += resized.microseconds;
        total_arranged += resized.arranged;
        total_relayouts += resized.relayouts;
    }
    std::cout << "total " << total_controls << " | " << total_paint << " | " << total_measure << " | - | "
              << total_resize << ' ' << total_arranged << ' ' << total_relayouts << '\n';
    return 0;
}
