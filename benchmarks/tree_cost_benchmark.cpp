// Audit addendum batch d (tree cost): the per-operation cost of a window of
// several thousand controls. The tree is 20 sections of 25 groups of 10
// focusable cells (5,522 controls with the root and an empty panel); the
// panel takes 2,000 children only in the bulk-clear case. Each case runs its
// operation many times and prints the mean in nanoseconds:
//
//   attach_detach   add a 10-control subtree to a group, then remove it
//   pointer_move    one pointer move across the cells
//   press_release   a press and a release on a cell
//   focus           request_focus on alternating cells far apart
//   layout_one      one cell's layout invalidated, then a flush
//   paint_one       one cell's paint invalidated, a flush and a paint
//   inherited_reads effectively_visible, effective_cursor and
//                   effective_layout_direction on every cell
//   clear_children  clear the flat panel's 2,000 children (each round
//                   re-adds them untimed)
//
//   gui_forms_tree_cost_benchmark [rounds]
//
// Renderer-neutral (gui_forms_core only); run in Release, sequential with
// builds and other measurements.

#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/plots/series_column/series_column.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"
#include "../tests/support/clip_tracking_painter.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;
using Clock = std::chrono::steady_clock;

constexpr std::size_t section_count = 20U;
constexpr std::size_t groups_per_section = 25U;
constexpr std::size_t cells_per_group = 10U;
constexpr std::size_t flat_children = 2000U;
constexpr double cell_extent = 8.0;
constexpr double surface_width = 2000.0;
constexpr double surface_height = 1200.0;

// Keeps the optimizer from removing the measured reads.
volatile std::uint64_t sink = 0U;

double nanoseconds_since(Clock::time_point start, std::uint64_t count) {
    const std::chrono::duration<double, std::nano> elapsed = Clock::now() - start;
    return elapsed.count() / static_cast<double>(count);
}

struct Tree final {
    Control::Ptr root;
    std::vector<Control::Ptr> groups;
    std::vector<Control::Ptr> cells;
    Control::Ptr flat_panel;
    std::vector<Control::Ptr> flat;
};

Control::Ptr make_box(const std::string& id, Rect bounds) {
    const Control::Ptr control = make_control<Control>(StableId(id));
    (*control).set_requested_bounds(bounds);
    return control;
}

// Sections stack in rows of the surface; each group is a row of ten cells.
Tree make_tree() {
    Tree tree;
    tree.root = make_box("tree.root", {0.0, 0.0, surface_width, surface_height});
    tree.groups.reserve(section_count * groups_per_section);
    tree.cells.reserve(section_count * groups_per_section * cells_per_group);
    const double group_width = cell_extent * static_cast<double>(cells_per_group);
    for (std::size_t section = 0U; section < section_count; ++section) {
        const std::string section_id = "tree.s" + std::to_string(section);
        const Control::Ptr section_box =
            make_box(section_id, {0.0, static_cast<double>(section) * 48.0, surface_width, 48.0});
        for (std::size_t group = 0U; group < groups_per_section; ++group) {
            const std::string group_id = section_id + ".g" + std::to_string(group);
            const Control::Ptr group_box =
                make_box(group_id, {static_cast<double>(group) * group_width, 0.0, group_width, cell_extent});
            for (std::size_t cell = 0U; cell < cells_per_group; ++cell) {
                const std::string cell_id = group_id + ".c" + std::to_string(cell);
                const Control::Ptr cell_box = make_box(
                    cell_id, {static_cast<double>(cell) * cell_extent, 0.0, cell_extent - 1.0, cell_extent - 1.0});
                (*cell_box).focusable = true;
                (*group_box).add_child(cell_box);
                tree.cells.push_back(cell_box);
            }
            (*section_box).add_child(group_box);
            tree.groups.push_back(group_box);
        }
        (*tree.root).add_child(section_box);
    }
    tree.flat_panel = make_box("tree.flat", {0.0, 1000.0, surface_width, 200.0});
    tree.flat.reserve(flat_children);
    for (std::size_t index = 0U; index < flat_children; ++index) {
        tree.flat.push_back(make_box("tree.flat." + std::to_string(index), {0.0, 0.0, 4.0, 4.0}));
    }
    (*tree.root).add_child(tree.flat_panel);
    return tree;
}

double attach_detach(Tree& tree, std::uint64_t rounds) {
    std::vector<Control::Ptr> subtrees;
    subtrees.reserve(rounds);
    for (std::uint64_t round = 0U; round < rounds; ++round) {
        const std::string id = "tree.extra" + std::to_string(round);
        const Control::Ptr extra = make_box(id, {0.0, 0.0, 8.0, 8.0});
        for (std::size_t child = 0U; child < 9U; ++child) {
            (*extra).add_child(make_box(id + "." + std::to_string(child), {0.0, 0.0, 1.0, 1.0}));
        }
        subtrees.push_back(extra);
    }
    const Clock::time_point start = Clock::now();
    for (std::uint64_t round = 0U; round < rounds; ++round) {
        const Control::Ptr& group = tree.groups[round % tree.groups.size()];
        (*group).add_child(subtrees[round]);
        static_cast<void>((*group).remove_child((*subtrees[round]).runtime_id()));
    }
    return nanoseconds_since(start, rounds);
}

double pointer_moves(Window& window, std::uint64_t rounds) {
    const Clock::time_point start = Clock::now();
    for (std::uint64_t round = 0U; round < rounds; ++round) {
        PointerEvent move{};
        move.action = PointerAction::move;
        move.position = Point{static_cast<double>(round % 1990U) + 3.0,
                              static_cast<double>((round / 7U) % 20U) * 48.0 + 3.0};
        static_cast<void>(window.dispatch_pointer(move));
    }
    return nanoseconds_since(start, rounds);
}

double press_releases(Window& window, std::uint64_t rounds) {
    const Clock::time_point start = Clock::now();
    for (std::uint64_t round = 0U; round < rounds; ++round) {
        PointerEvent press{};
        press.action = PointerAction::down;
        press.button = PointerButton::primary;
        press.position = Point{static_cast<double>(round % 1990U) + 3.0, 51.0};
        static_cast<void>(window.dispatch_pointer(press));
        PointerEvent release = press;
        release.action = PointerAction::up;
        static_cast<void>(window.dispatch_pointer(release));
    }
    return nanoseconds_since(start, rounds);
}

double focus_changes(Window& window, const Tree& tree, std::uint64_t rounds) {
    const std::size_t half = tree.cells.size() / 2U;
    const Clock::time_point start = Clock::now();
    for (std::uint64_t round = 0U; round < rounds; ++round) {
        const std::size_t index = (round % 2U == 0U) ? (round % half) : (half + round % half);
        static_cast<void>(window.request_focus(tree.cells[index]));
    }
    return nanoseconds_since(start, rounds);
}

double layout_one(Window& window, const Tree& tree, std::uint64_t rounds) {
    const Clock::time_point start = Clock::now();
    for (std::uint64_t round = 0U; round < rounds; ++round) {
        (*tree.cells[(round * 37U) % tree.cells.size()]).invalidate(Dirty::layout);
        window.flush();
    }
    return nanoseconds_since(start, rounds);
}

double paint_one(Window& window, const Tree& tree, std::uint64_t rounds) {
    test_support::ClipTrackingPainter painter;
    const Clock::time_point start = Clock::now();
    for (std::uint64_t round = 0U; round < rounds; ++round) {
        (*tree.cells[(round * 53U) % tree.cells.size()]).invalidate(Dirty::paint);
        window.flush();
        static_cast<void>(window.paint(painter));
        painter.texts.clear();
    }
    return nanoseconds_since(start, rounds);
}

double inherited_reads(const Tree& tree, std::uint64_t rounds) {
    std::uint64_t counted = 0U;
    const Clock::time_point start = Clock::now();
    for (std::uint64_t round = 0U; round < rounds; ++round) {
        for (const Control::Ptr& cell : tree.cells) {
            if ((*cell).effectively_visible()) ++counted;
            counted += static_cast<std::uint64_t>((*cell).effective_cursor());
            counted += static_cast<std::uint64_t>((*cell).effective_layout_direction());
        }
    }
    sink = sink + counted;
    return nanoseconds_since(start, rounds * tree.cells.size());
}

double clear_children(Tree& tree, std::uint64_t rounds) {
    std::chrono::duration<double, std::nano> total{0.0};
    for (std::uint64_t round = 0U; round < rounds; ++round) {
        for (const Control::Ptr& child : tree.flat) {
            (*tree.flat_panel).add_child(child);
        }
        const Clock::time_point start = Clock::now();
        (*tree.flat_panel).clear_children();
        total += Clock::now() - start;
    }
    return total.count() / static_cast<double>(rounds);
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
    const std::uint64_t rounds = argument(argc, argv, 1, 2000U);
    Tree tree = make_tree();
    Window window(tree.root, {surface_width, surface_height});
    window.perform_layout();
    test_support::ClipTrackingPainter painter;
    static_cast<void>(window.paint(painter, Rect{0.0, 0.0, surface_width, surface_height}));

    const double attach = attach_detach(tree, rounds);
    const double move = pointer_moves(window, rounds);
    const double press = press_releases(window, rounds);
    const double focus = focus_changes(window, tree, rounds);
    const double layout = layout_one(window, tree, rounds);
    const double paint = paint_one(window, tree, rounds);
    const double reads = inherited_reads(tree, 20U);
    const double clear = clear_children(tree, 20U);
    std::printf("tree cost: %zu controls, %llu rounds\n",
                1U + section_count * (1U + groups_per_section * (1U + cells_per_group)) + 1U,
                static_cast<unsigned long long>(rounds));
    std::printf("  attach_detach   %12.0f ns\n", attach);
    std::printf("  pointer_move    %12.0f ns\n", move);
    std::printf("  press_release   %12.0f ns\n", press);
    std::printf("  focus           %12.0f ns\n", focus);
    std::printf("  layout_one      %12.0f ns\n", layout);
    std::printf("  paint_one       %12.0f ns\n", paint);
    std::printf("  inherited_reads %12.1f ns per cell\n", reads);
    std::printf("  clear_children  %12.0f ns per 2000\n", clear);
    return 0;
}
