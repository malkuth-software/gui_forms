// Ribbon state (ribbon phase 4, owner decision RB-12): the stable text form
// round trip, defaults and diagnostics for malformed entries, unknown keys
// and command IDs preserved, and capture from and application to a
// QuickAccessToolbar.

#include "gui_forms/gui_forms.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

bool has_issue(const RibbonStateParseResult& result, std::size_t line, RibbonStateIssue issue) {
    for (const RibbonStateDiagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.line == line && diagnostic.issue == issue) return true;
    }
    return false;
}

void round_trip() {
    RibbonState state{};
    state.quick_access_command_ids = {"save", "undo", "redo"};
    state.quick_access_placement = QuickAccessToolbarPlacement::below_ribbon;
    state.minimized = true;
    const std::string text = serialize_ribbon_state(state);
    require(text ==
                "gui_forms.ribbon_state 1\n"
                "quick_access.placement=below_ribbon\n"
                "ribbon.minimized=true\n"
                "quick_access.command=save\n"
                "quick_access.command=undo\n"
                "quick_access.command=redo\n",
            "the text form is stable and deterministic");
    const RibbonStateParseResult parsed = parse_ribbon_state(text);
    require(parsed.diagnostics.empty() && parsed.state == state, "the text form round-trips");

    const RibbonStateParseResult windows = parse_ribbon_state(
        "gui_forms.ribbon_state 1\r\nribbon.minimized=false\r\nquick_access.command=save\r\n");
    require(windows.diagnostics.empty() && windows.state.quick_access_command_ids.size() == 1U &&
                windows.state.quick_access_command_ids[0] == "save",
            "CR LF line ends are read");
}

void defaults_and_diagnostics() {
    const RibbonStateParseResult empty = parse_ribbon_state("");
    require(empty.state == RibbonState{} && has_issue(empty, 1U, RibbonStateIssue::missing_header),
            "empty text is the default state with a missing header");
    const RibbonStateParseResult later = parse_ribbon_state("gui_forms.ribbon_state 2\nribbon.minimized=true\n");
    require(later.state == RibbonState{} && has_issue(later, 1U, RibbonStateIssue::unsupported_version),
            "another version is not read");
    const RibbonStateParseResult bad = parse_ribbon_state(
        "gui_forms.ribbon_state 1\n"
        "quick_access.placement=sideways\n"
        "ribbon.minimized=true\n"
        "ribbon.minimized=false\n"
        "no equals sign\n"
        "quick_access.command=save\n"
        "quick_access.command=save\n"
        "quick_access.command=\n"
        "future.setting=on\n");
    require(bad.state.quick_access_placement == QuickAccessToolbarPlacement::above_ribbon &&
                has_issue(bad, 2U, RibbonStateIssue::invalid_value),
            "an invalid placement keeps the default and is reported");
    require(bad.state.minimized && has_issue(bad, 4U, RibbonStateIssue::duplicate_key),
            "a repeated key keeps the first value and is reported");
    require(has_issue(bad, 5U, RibbonStateIssue::malformed_line), "a line without a key is reported");
    require(bad.state.quick_access_command_ids.size() == 1U && has_issue(bad, 7U, RibbonStateIssue::duplicate_key) &&
                has_issue(bad, 8U, RibbonStateIssue::invalid_value),
            "a repeated or empty command ID is reported and skipped");
    require(bad.state.unrecognized_entries.size() == 1U && bad.state.unrecognized_entries[0] == "future.setting=on",
            "an unknown key is preserved, not activated");
    const std::string written = serialize_ribbon_state(bad.state);
    require(written.find("future.setting=on\n") != std::string::npos, "an unknown key is written back");

    std::string many = "gui_forms.ribbon_state 1\n";
    for (int index = 0; index < 21; ++index) many += "quick_access.command=c" + std::to_string(index) + "\n";
    const RibbonStateParseResult capped = parse_ribbon_state(many);
    require(capped.state.quick_access_command_ids.size() == QuickAccessToolbar::maximum_commands &&
                has_issue(capped, 22U, RibbonStateIssue::too_many_commands),
            "more than 20 command IDs are reported and dropped");

    RibbonState invalid{};
    invalid.quick_access_command_ids = {"line\nbreak"};
    bool threw = false;
    try {
        static_cast<void>(serialize_ribbon_state(invalid));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "an ID with a line break cannot be written");
}

void toolbar_capture_and_apply() {
    const std::shared_ptr<Command> save = std::make_shared<Command>("save", "Save");
    const std::shared_ptr<Command> undo = std::make_shared<Command>("undo", "Undo");
    const std::shared_ptr<Command> print = std::make_shared<Command>("print", "Print");
    std::shared_ptr<QuickAccessToolbar> toolbar = make_control<QuickAccessToolbar>(StableId("qat"));
    (*toolbar).set_commands({{.command = save}, {.command = undo}, {.command = print, .shown = false}});
    Window window(toolbar, {320.0, 25.0});
    window.perform_layout();

    (*toolbar).set_command_shown("print", true);
    (*toolbar).set_command_shown("save", false);
    (*toolbar).placement = QuickAccessToolbarPlacement::below_ribbon;
    const RibbonState captured = capture_ribbon_state(*toolbar, true);
    const std::string saved = serialize_ribbon_state(captured);

    // A new session: the application declares its defaults, then loads.
    std::shared_ptr<QuickAccessToolbar> restored = make_control<QuickAccessToolbar>(StableId("qat"));
    (*restored).set_commands({{.command = save}, {.command = undo}, {.command = print, .shown = false}});
    Window next(restored, {320.0, 25.0});
    next.perform_layout();
    const RibbonStateParseResult loaded = parse_ribbon_state(saved);
    require(loaded.diagnostics.empty() && loaded.state.minimized, "the saved state loads");
    apply_ribbon_state(*restored, loaded.state);
    require((*restored).shown_command_ids() == (*toolbar).shown_command_ids() &&
                (*restored).placement.read() == QuickAccessToolbarPlacement::below_ribbon,
            "applying the state restores the toolbar's commands, order and placement");
    const std::vector<ToolStrip::Item> items = (*restored).realized_items();
    require(items.size() == 2U && items[0].stable_id == "undo" && items[1].stable_id == "print",
            "the restored toolbar shows the saved order");

    // A state from a later version names a command this one lacks.
    RibbonState future = loaded.state;
    future.quick_access_command_ids.insert(future.quick_access_command_ids.begin(), "insert.chart");
    apply_ribbon_state(*restored, future);
    require((*restored).realized_items().size() == 2U &&
                capture_ribbon_state(*restored, true).quick_access_command_ids == future.quick_access_command_ids,
            "an unknown command ID shows nothing but survives capture");
}

} // namespace

int main() {
    round_trip();
    defaults_and_diagnostics();
    toolbar_capture_and_apply();
    std::cout << "Ribbon state: text round trip, diagnostics, preserved entries and toolbar capture passed\n";
    return 0;
}
