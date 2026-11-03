// Paint groups in the retained recording (Painter::begin_group; T2 E7, owner
// decision E7-1): a control's groups are recorded and replayed in order,
// nest to maximum_paint_group_depth and are refused by name past it (drawn
// without isolation and counted in the window's metrics), must end in the
// chunk that begins them with their saves balanced, and are inspectable.
// A minimal painter without isolation clips a group to its bounds and draws
// nothing at opacity 0. The pixels groups make are checked on every host by
// part_composite_identity_tests.

#include "gui_forms/control.hpp"
#include "gui_forms/visual_inspection.hpp"
#include "gui_forms/window.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_paint_group_tests: " << message << '\n';
    std::exit(1);
}

PaintGroup group_of(Rect bounds, double opacity) {
    PaintGroup group{};
    group.bounds = bounds;
    group.opacity = opacity;
    return group;
}

// Records what a replay hands it, one word per call, and the clips.
class ListingPainter final : public Painter {
public:
    void save() override { calls.emplace_back("save"); }
    void restore() override { calls.emplace_back("restore"); }
    void translate(Point) override { calls.emplace_back("translate"); }
    void clip_rect(Rect rect) override {
        calls.emplace_back("clip");
        clips.push_back(rect);
    }
    void fill_rect(Rect, Color) override { calls.emplace_back("fill"); }
    void stroke_rect(Rect, Color, double) override { calls.emplace_back("stroke"); }
    void draw_line(Point, Point, Color, double) override { calls.emplace_back("line"); }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { calls.emplace_back("text"); }
    void draw_image(ImageId, Rect, double) override { calls.emplace_back("image"); }
    void begin_group(const PaintGroup& group) override {
        calls.emplace_back("begin");
        opacities.push_back(group.opacity);
    }
    void end_group() override { calls.emplace_back("end"); }

    std::vector<std::string> calls{};
    std::vector<Rect> clips{};
    std::vector<double> opacities{};
};

// A painter keeping the base fallback for groups.
class PlainPainter final : public Painter {
public:
    void save() override { ++saves; }
    void restore() override { --saves; }
    void translate(Point) override {}
    void clip_rect(Rect rect) override { clips.push_back(rect); }
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}

    int saves{};
    std::vector<Rect> clips{};
};

enum class Script : std::uint8_t { nested, too_deep, left_open, ended_twice, saves_cross };

// A control drawing one of the scripts.
class Grouper final : public Control {
public:
    explicit Grouper(StableId stable_id) : Control(std::move(stable_id)) {}

    void on_paint(Painter& painter, Rect) override {
        switch (script) {
        case Script::nested:
            painter.begin_group(group_of({0.0, 0.0, 50.0, 40.0}, 0.5));
            painter.fill_rect({2.0, 2.0, 10.0, 10.0}, Color::rgba(1, 2, 3, 255));
            painter.begin_group(group_of({4.0, 4.0, 20.0, 20.0}, 0.25));
            painter.fill_rect({5.0, 5.0, 4.0, 4.0}, Color::rgba(4, 5, 6, 255));
            painter.end_group();
            painter.end_group();
            return;
        case Script::too_deep:
            for (std::size_t level = 0U; level < maximum_paint_group_depth + 2U; ++level) {
                painter.begin_group(group_of({0.0, 0.0, 50.0, 40.0}, 0.5));
            }
            painter.fill_rect({2.0, 2.0, 10.0, 10.0}, Color::rgba(1, 2, 3, 255));
            for (std::size_t level = 0U; level < maximum_paint_group_depth + 2U; ++level) painter.end_group();
            return;
        case Script::left_open:
            painter.begin_group(group_of({0.0, 0.0, 50.0, 40.0}, 0.5));
            return;
        case Script::ended_twice:
            painter.begin_group(group_of({0.0, 0.0, 50.0, 40.0}, 0.5));
            painter.end_group();
            painter.end_group();
            return;
        case Script::saves_cross:
            painter.begin_group(group_of({0.0, 0.0, 50.0, 40.0}, 0.5));
            painter.save();
            painter.end_group();
            painter.restore();
            return;
        }
        // A script outside the enumeration draws nothing.
        return;
    }

    Script script{Script::nested};
};

struct Fixture final {
    std::shared_ptr<Grouper> root;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(Script script) {
    Fixture fixture{};
    fixture.root = make_control<Grouper>(StableId("grouper"));
    (*fixture.root).script = script;
    fixture.window = std::make_unique<Window>(fixture.root, Size{100.0, 80.0});
    (*fixture.window).perform_layout();
    return fixture;
}

// Paints a script's window, true when the paint threw std::logic_error.
bool paint_refuses(Script script) {
    Fixture fixture = make_fixture(script);
    ListingPainter painter{};
    try {
        static_cast<void>((*fixture.window).paint(painter));
    } catch (const std::logic_error&) {
        return true;
    }
    return false;
}

void recorded_and_replayed() {
    Fixture fixture = make_fixture(Script::nested);
    ListingPainter painter{};
    static_cast<void>((*fixture.window).paint(painter));
    // From the first group on: the window's own backplane is filled first.
    std::vector<std::string> groups{};
    bool begun = false;
    for (const std::string& call : painter.calls) {
        if (call == "begin") begun = true;
        if (!begun) continue;
        if (call == "begin" || call == "end" || call == "fill") groups.push_back(call);
    }
    const std::vector<std::string> expected{"begin", "fill", "begin", "fill", "end", "end"};
    require(groups == expected, "nested groups replay in order around their commands");
    require(painter.opacities == std::vector<double>({0.5, 0.25}), "each group keeps its opacity");

    VisualInspectionOptions options{};
    const VisualInspectionSnapshot snapshot = (*fixture.window).visual_inspection_snapshot(options);
    const VisualControlInspection* const control = snapshot.find("grouper");
    require(control != nullptr, "the control is inspected");
    std::size_t begins = 0U;
    std::size_t ends = 0U;
    for (const VisualPaintOperationSnapshot& operation : (*control).display_operations) {
        if (operation.operation == VisualPaintOperation::begin_group) {
            ++begins;
            require(operation.scalar == 0.5 || operation.scalar == 0.25, "an inspected group shows its opacity");
        }
        if (operation.operation == VisualPaintOperation::end_group) ++ends;
    }
    require(begins == 2U && ends == 2U, "inspection lists the groups");
    require(std::string_view(visual_paint_operation_name(VisualPaintOperation::begin_group)) == "begin_group",
            "and names them");
}

void refused_past_the_depth() {
    Fixture fixture = make_fixture(Script::too_deep);
    ListingPainter painter{};
    static_cast<void>((*fixture.window).paint(painter));
    std::size_t begins = 0U;
    for (const std::string& call : painter.calls) {
        if (call == "begin") ++begins;
    }
    require(begins == maximum_paint_group_depth, "groups nest to the depth limit");
    require((*fixture.window).metrics_snapshot().paint_group_refusals == 2U,
            "the two groups past it are refused by name and counted");
}

void unbalanced_chunks_are_refused() {
    require(paint_refuses(Script::left_open), "a chunk leaving a group open is refused");
    require(paint_refuses(Script::ended_twice), "a group ended without one begun is refused");
    require(paint_refuses(Script::saves_cross), "a group whose saves are unbalanced is refused");
    require(!paint_refuses(Script::nested), "a balanced chunk records");
}

void minimal_painter_fallback() {
    PlainPainter painter{};
    painter.begin_group(group_of({1.0, 2.0, 3.0, 4.0}, 0.5));
    require(painter.saves == 1 && painter.clips.size() == 1U && painter.clips[0] == Rect{1.0, 2.0, 3.0, 4.0},
            "without isolation a group is a save and its bounds' clip");
    painter.end_group();
    require(painter.saves == 0, "ended by the matching restore");
    painter.begin_group(group_of({1.0, 2.0, 3.0, 4.0}, 0.0));
    require(painter.clips.size() == 3U && painter.clips[2].empty(), "at opacity 0 it clips everything away");
    painter.end_group();
}

} // namespace

int main() {
    recorded_and_replayed();
    refused_past_the_depth();
    unbalanced_chunks_are_refused();
    minimal_painter_fallback();
    std::cout << "paint groups: recording, nesting, refusals, balance, inspection and the fallback passed\n";
    return EXIT_SUCCESS;
}
