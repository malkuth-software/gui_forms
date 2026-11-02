// Stage C5: the cost of shadow frames. Measures one push and pop of each
// frame shape alone, then a renderer-neutral window workload (pointer moves,
// a layout flush and a paint per turn over a 200-control tree) and the
// frames it opens per turn, and prints the frames' estimated share of the
// workload. Shares come from separate measurements, so they are estimates.
//
//   gui_forms_shadow_frames_benchmark [pairs] [turns]

#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/gui_forms.hpp"
#include "../tests/support/clip_tracking_painter.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;
using Clock = std::chrono::steady_clock;

constexpr std::size_t tree_columns = 20U;
constexpr std::size_t tree_rows = 10U;

// Keeps the optimizer from removing the measured scopes.
volatile std::uint64_t sink = 0U;

std::uint64_t pushed_so_far() {
    std::array<ShadowFrame, 1> unused{};
    ShadowStackState state{};
    static_cast<void>(copy_shadow_frames(std::span<ShadowFrame>(unused), state));
    return state.pushed;
}

double nanoseconds_since(Clock::time_point start, std::uint64_t count) {
    const std::chrono::duration<double, std::nano> elapsed = Clock::now() - start;
    return elapsed.count() / static_cast<double>(count);
}

double library_call_pair(std::uint64_t pairs) {
    const Clock::time_point start = Clock::now();
    for (std::uint64_t index = 0U; index < pairs; ++index) {
        const ShadowFrameScope frame(2U, static_cast<std::uint32_t>(index & 15U));
        sink = sink + 1U;
    }
    return nanoseconds_since(start, pairs);
}

double text_frame_pair(std::uint64_t pairs) {
    const std::string subject = "gallery.page.button.ok";
    const Clock::time_point start = Clock::now();
    for (std::uint64_t index = 0U; index < pairs; ++index) {
        const ShadowFrameScope frame(ShadowFrameKind::event_callback, subject, "click", index);
        sink = sink + 1U;
    }
    return nanoseconds_since(start, pairs);
}

struct Workload final {
    double turn_nanoseconds = 0.0;
    double frames_per_turn = 0.0;
};

Workload window_workload(std::uint64_t turns) {
    const Control::Ptr root = make_control<Control>(StableId("bench.root"));
    (*root).set_requested_bounds({0.0, 0.0, 800.0, 400.0});
    std::vector<Control::Ptr> cells;
    cells.reserve(tree_columns * tree_rows);
    for (std::size_t row = 0U; row < tree_rows; ++row) {
        for (std::size_t column = 0U; column < tree_columns; ++column) {
            const std::string id = "bench.cell." + std::to_string(row) + "." + std::to_string(column);
            const Control::Ptr cell = make_control<Control>(StableId(id));
            (*cell).set_requested_bounds({static_cast<double>(column) * 40.0, static_cast<double>(row) * 40.0, 36.0,
                                          36.0});
            (*root).add_child(cell);
            cells.push_back(cell);
        }
    }
    Window window(root, {800.0, 400.0});
    window.perform_layout();
    test_support::ClipTrackingPainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, 800.0, 400.0}));

    const std::uint64_t pushed_before = pushed_so_far();
    const Clock::time_point start = Clock::now();
    for (std::uint64_t turn = 0U; turn < turns; ++turn) {
        const double x = static_cast<double>(turn % 800U);
        PointerEvent move{};
        move.action = PointerAction::move;
        move.position = Point{x, 200.0};
        static_cast<void>(window.dispatch_pointer(move));
        (*cells[turn % cells.size()]).invalidate(Dirty::layout);
        (*cells[(turn + 7U) % cells.size()]).invalidate(Dirty::paint);
        window.flush();
        static_cast<void>(window.paint(painter));
        painter.texts.clear();
    }
    Workload result;
    result.turn_nanoseconds = nanoseconds_since(start, turns);
    result.frames_per_turn =
        static_cast<double>(pushed_so_far() - pushed_before) / static_cast<double>(turns);
    return result;
}

std::uint64_t argument(int argc, char** argv, int index, std::uint64_t fallback) {
    if (argc <= index) {
        return fallback;
    }
    const std::uint64_t value = std::strtoull(argv[index], nullptr, 10);
    return value == 0U ? fallback : value;
}

} // namespace

int main(int argc, char** argv) {
    const std::uint64_t pairs = argument(argc, argv, 1, 20000000U);
    const std::uint64_t turns = argument(argc, argv, 2, 20000U);
    const double library_call = library_call_pair(pairs);
    const double text_frame = text_frame_pair(pairs);
    const Workload workload = window_workload(turns);
    // The window's frames carry text; the costlier shape bounds them.
    const double frames_cost = workload.frames_per_turn * text_frame;
    std::printf("shadow frames: %llu pairs, %llu turns\n", static_cast<unsigned long long>(pairs),
                static_cast<unsigned long long>(turns));
    std::printf("  library_call push+pop: %.2f ns\n", library_call);
    std::printf("  text frame push+pop:   %.2f ns\n", text_frame);
    std::printf("  window turn: %.0f ns, %.2f frames per turn\n", workload.turn_nanoseconds,
                workload.frames_per_turn);
    std::printf("  estimated frame share of a turn: %.4f%%\n", 100.0 * frames_cost / workload.turn_nanoseconds);
    return 0;
}
