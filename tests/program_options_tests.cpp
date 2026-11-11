// Headless checks of the --smoke-test verdict in demo/program_options.hpp: a
// smoke passes only when a frame had been presented before its scheduled
// close, and fails when the deadline closed a window that never presented.

#include "../demo/program_options.hpp"

#include "gui_forms/gui_forms.hpp"

#include <array>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "program options failure: " << message << '\n';
        std::exit(1);
    }
}

class NullPainter final : public Painter {
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

struct CountClose final {
    std::shared_ptr<int> count;

    void operator()() const { ++(*count); }
};

[[nodiscard]] std::unique_ptr<Window> make_window(std::string_view id) {
    const std::shared_ptr<Control> root = make_control<Control>(StableId(std::string(id)));
    (*root).set_requested_bounds({0.0, 0.0, 160.0, 90.0});
    return std::make_unique<Window>(root, Size{160.0, 90.0});
}

[[nodiscard]] std::shared_ptr<demo::SmokeTest> arm(Window& window, FrameTime started,
                                                   const std::shared_ptr<int>& closes) {
    std::shared_ptr<demo::SmokeTest> state = std::make_shared<demo::SmokeTest>();
    (*state).window = &window;
    (*state).started = started;
    (*state).request_close = CountClose{closes};
    return state;
}

void test_deadline_without_a_presented_frame_fails() {
    std::unique_ptr<Window> window = make_window("smoke.occluded.root");
    (*window).set_occluded(true, FrameTime{});
    const FrameTime started = FrameTime{} + 1s;
    const std::shared_ptr<int> closes = std::make_shared<int>(0);
    const std::shared_ptr<demo::SmokeTest> state = arm(*window, started, closes);

    demo::SmokeTestTick{state}(started + 9900ms);
    require(!(*state).completed && *closes == 0, "the smoke closed before its deadline");
    demo::SmokeTestTick{state}(started + 10s);
    require((*state).completed && *closes == 1, "the deadline did not request the close");
    require(!(*state).presented && (*state).occluded,
            "the deadline close must record no presented frame and the occlusion");
    demo::SmokeTestTick{state}(started + 11s);
    require(*closes == 1, "a completed smoke requested a second close");
    require(demo::smoke_test_result(state, 0) == 1,
            "a smoke that presented no frame must fail although the host returned 0");
    require(demo::smoke_test_result(state, 4) == 4, "the host's failure code was replaced");
}

void test_presented_frame_passes() {
    std::unique_ptr<Window> window = make_window("smoke.presented.root");
    NullPainter painter;
    const std::optional<PaintReceipt> receipt =
        (*window).paint(painter, (*window).take_damage().bounds());
    require(receipt.has_value() && (*window).notify_presented(*receipt) &&
                (*window).metrics_snapshot().frames_presented == 1U,
            "the headless window did not present its first frame");
    const FrameTime started = FrameTime{} + 1s;
    const std::shared_ptr<int> closes = std::make_shared<int>(0);
    const std::shared_ptr<demo::SmokeTest> state = arm(*window, started, closes);

    demo::SmokeTestTick{state}(started + 500ms);
    require(!(*state).completed, "a presented smoke closed before one second");
    demo::SmokeTestTick{state}(started + 1s);
    require((*state).completed && *closes == 1 && (*state).presented && !(*state).occluded,
            "a presented smoke must close after one second and record the frame");
    require(demo::smoke_test_result(state, 0) == 0, "a presented smoke failed");
    require(demo::smoke_test_result(state, 3) == 3, "the host's failure code was replaced");
}

void test_unscheduled_close_and_absent_smoke() {
    std::unique_ptr<Window> window = make_window("smoke.unscheduled.root");
    const std::shared_ptr<int> closes = std::make_shared<int>(0);
    const std::shared_ptr<demo::SmokeTest> state = arm(*window, FrameTime{}, closes);
    require(demo::smoke_test_result(state, 0) == 1,
            "a smoke that never reached its scheduled close must fail");
    require(demo::smoke_test_result(nullptr, 7) == 7,
            "without --smoke-test the host's code must pass through");
}

// The Gallery's capture-staging and runtime-sheet options parse into their
// fields; a missing value is refused.
void test_theme_sheet_and_staging_options_parse() {
    std::array<char*, 9> arguments{const_cast<char*>("gallery"), const_cast<char*>("--theme-sheet"),
                                   const_cast<char*>("candidate.theme.css"), const_cast<char*>("--reveal"),
                                   const_cast<char*>("gallery.commands.ribbon"), const_cast<char*>("--open-menu"),
                                   const_cast<char*>("gallery.commands.menu/0"), const_cast<char*>("--scheme"),
                                   const_cast<char*>("dark")};
    const std::optional<demo::ProgramOptions> parsed =
        demo::parse_program_options(static_cast<int>(arguments.size()), arguments.data());
    require(parsed.has_value() && (*parsed).theme_sheet == "candidate.theme.css" &&
                (*parsed).reveal == "gallery.commands.ribbon" && (*parsed).open_menu == "gallery.commands.menu/0" &&
                (*parsed).scheme == "dark",
            "--theme-sheet, --reveal, --open-menu and --scheme parse into their fields");
    std::array<char*, 2> missing{const_cast<char*>("gallery"), const_cast<char*>("--theme-sheet")};
    require(!demo::parse_program_options(static_cast<int>(missing.size()), missing.data()).has_value(),
            "--theme-sheet without a path is refused");
}

} // namespace

int main() {
    test_deadline_without_a_presented_frame_fails();
    test_presented_frame_passes();
    test_unscheduled_close_and_absent_smoke();
    test_theme_sheet_and_staging_options_parse();
    std::cout << "GUI.Forms program options tests passed\n";
    return 0;
}
