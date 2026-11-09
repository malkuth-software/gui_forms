#import <AppKit/AppKit.h>

#include "gui_forms/application.hpp"
#include "gui_forms/basic_controls.hpp"
#include "gui_forms/control.hpp"
#include "gui_forms/input_controls.hpp"
#include "gui_forms/window/window.hpp"
#include "support/macos_handoff_probe.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Focus in composite fields on real AppKit (Mac handoff 2026-11-09, item 2c;
// Control::focus_target, commits 70026017 and 65e4f4d2). For each composite
// (ComboBox with an edit field, NumericUpDown, DomainUpDown, MaskedTextBox,
// TokenBox, SearchField): focusing it puts focus in its edit part, the window
// names the composite as its active control, the NSWindow stays key with its
// raster view (the NSTextInputClient) first responder, the input method's
// caret rectangle (firstRectForCharacterRange:) lies in the edit part on
// screen, and a key typed through NSApp reaches the edit part. One PNG per
// composite of the host's raster (AppKit's cached display) shows the focus
// ring; they are written to handoff_focus_captures/ beside the bundle. Last,
// Tab and Shift+Tab sent through NSApp walk the composites in order.

namespace {
using namespace gui_forms;

constexpr const char* window_title = "GUI.Forms composite focus probe";
constexpr unsigned short key_code_tab = 48U;
constexpr unsigned short key_code_seven = 26U;

struct CompositeEntry final {
    std::string name{};
    Control::Ptr composite{};
    std::shared_ptr<TextBox> editor{};
};

struct FocusProbe final {
    handoff_probe::Report report{};
    handoff_probe::Watchdog* watchdog{};
    Window* window{};
    ApplicationWindowHandle handle{};
    std::shared_ptr<TextBox> start{};
    std::vector<CompositeEntry> entries{};
    std::size_t index{};
    std::string capture_directory{};
};

std::string id_text(const Control::Ptr& control) {
    if (control == nullptr) return std::string("none");
    const std::string text((*control).stable_id().value());
    return text;
}

// Adds `composite` at row `row` of the window's column, with its editor.
void add_entry(FocusProbe& probe, Panel& root, const char* const name, const Control::Ptr& composite,
               const std::shared_ptr<TextBox>& editor, const std::size_t row) {
    const double top = 20.0 + 52.0 * static_cast<double>(row);
    (*composite).set_requested_bounds({20.0, top, 280.0, 30.0});
    root.add_child(composite);
    probe.entries.push_back(CompositeEntry{name, composite, editor});
}

std::unique_ptr<Window> make_window(FocusProbe& probe) {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("focus.probe.root"));
    (*root).set_requested_bounds({0.0, 0.0, 340.0, 400.0});
    probe.start = make_control<TextBox>(StableId("focus.probe.start"));
    (*probe.start).set_requested_bounds({20.0, 20.0, 280.0, 30.0});
    (*root).add_child(probe.start);
    const std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("focus.probe.combo"));
    (*combo).drop_down_style = ComboBoxStyle::drop_down;
    add_entry(probe, *root, "combo_box", combo, (*combo).editor(), 1U);
    const std::shared_ptr<NumericUpDown> numeric = make_control<NumericUpDown>(StableId("focus.probe.numeric"));
    add_entry(probe, *root, "numeric_up_down", numeric, (*numeric).editor(), 2U);
    const std::shared_ptr<DomainUpDown> domain = make_control<DomainUpDown>(StableId("focus.probe.domain"));
    add_entry(probe, *root, "domain_up_down", domain, (*domain).editor(), 3U);
    const std::shared_ptr<MaskedTextBox> masked =
        make_control<MaskedTextBox>(StableId("focus.probe.masked"), std::string("000"));
    add_entry(probe, *root, "masked_text_box", masked, (*masked).editor(), 4U);
    const std::shared_ptr<TokenBox> token = make_control<TokenBox>(StableId("focus.probe.token"));
    add_entry(probe, *root, "token_box", token, (*token).editor(), 5U);
    const std::shared_ptr<SearchField> search = make_control<SearchField>(StableId("focus.probe.search"));
    add_entry(probe, *root, "search_field", search, (*search).editor(), 6U);
    std::unique_ptr<Window> window = std::make_unique<Window>(root, gui_forms::Size{340.0, 400.0});
    return window;
}

void focus_one(void* context);
void finish_probe(void* context);

// Tab from the start field through every composite and back, then Shift+Tab
// once, each key sent through NSApp; the active control is read after each.
void walk_tab_order(void* const context) {
    FocusProbe& probe = *static_cast<FocusProbe*>(context);
    handoff_probe::Report& report = probe.report;
    (*probe.watchdog).reached(100);
    Window& window = *probe.window;
    NSWindow* const native = handoff_probe::find_window(window_title);
    report.check("start field takes focus", window.request_focus(probe.start), "");
    std::string expected;
    for (const CompositeEntry& entry : probe.entries) expected += id_text(entry.composite) + " ";
    expected += id_text(probe.start);
    std::string walked;
    std::string focused_parts;
    const std::size_t presses = probe.entries.size() + 1U;
    for (std::size_t press = 0U; press < presses; ++press) {
        handoff_probe::send_key(native, @"\t", key_code_tab, 0);
        if (press != 0U) walked += " ";
        walked += id_text(window.active_control());
        if (press != 0U) focused_parts += " ";
        focused_parts += id_text(window.focused_control());
    }
    report.check("Tab walks the composites in order", walked == expected,
                 "walked: " + walked + " | expected: " + expected);
    report.note("focused parts during the Tab walk: " + focused_parts);
    handoff_probe::send_key(native, @"\t", key_code_tab, NSEventModifierFlagShift);
    const std::string back = id_text(window.active_control());
    const std::string last = id_text(probe.entries.back().composite);
    report.check("Shift+Tab from the start field reaches the last composite", back == last,
                 "active=" + back + " expected=" + last);
    handoff_probe::after(0.2, &probe, &finish_probe);
}

// Captures the raster with the current composite focused, then types a
// digit through NSApp and checks that it reached the edit part.
void capture_and_type(void* const context) {
    FocusProbe& probe = *static_cast<FocusProbe*>(context);
    handoff_probe::Report& report = probe.report;
    const CompositeEntry& entry = probe.entries[probe.index];
    NSWindow* const native = handoff_probe::find_window(window_title);
    NSView* const raster = native == nil ? nil : handoff_probe::find_raster_view(native.contentView);
    const std::string path = probe.capture_directory + "/" + entry.name + ".png";
    const bool written = raster != nil && handoff_probe::write_view_png(raster, path);
    report.check((entry.name + " focus capture written").c_str(), written, path);

    const std::string before = (*entry.editor).text.read();
    handoff_probe::send_key(native, @"7", key_code_seven, 0);
    const std::string after = (*entry.editor).text.read();
    report.check((entry.name + " native key reaches the edit part").c_str(), after != before,
                 "editor text '" + before + "' -> '" + after + "'");
    report.check((entry.name + " still focused after typing").c_str(),
                 (*probe.window).control_focused(entry.composite),
                 "focused=" + id_text((*probe.window).focused_control()));

    ++probe.index;
    if (probe.index < probe.entries.size()) {
        handoff_probe::after(0.1, &probe, &focus_one);
    } else {
        handoff_probe::after(0.2, &probe, &walk_tab_order);
    }
}

void focus_one(void* const context) {
    FocusProbe& probe = *static_cast<FocusProbe*>(context);
    handoff_probe::Report& report = probe.report;
    (*probe.watchdog).reached(10 + static_cast<int>(probe.index));
    Window& window = *probe.window;
    const CompositeEntry& entry = probe.entries[probe.index];
    const std::string& name = entry.name;

    const bool requested = window.request_focus(entry.composite);
    const Control::Ptr focused = window.focused_control();
    const Control::Ptr editor_part = entry.editor;
    report.check((name + " focus puts focus in its edit part").c_str(),
                 requested && entry.editor != nullptr && focused == editor_part,
                 "focused=" + id_text(focused) + " edit part=" + id_text(editor_part));
    report.check((name + " is the active control").c_str(), window.active_control() == entry.composite,
                 "active=" + id_text(window.active_control()));
    report.check((name + " reports Focused").c_str(), window.control_focused(entry.composite), "");

    NSWindow* const native = handoff_probe::find_window(window_title);
    NSView* const raster = native == nil ? nil : handoff_probe::find_raster_view(native.contentView);
    report.check((name + " NSWindow is key").c_str(), native != nil && [NSApp keyWindow] == native,
                 "keyWindow=" + handoff_probe::describe_object([NSApp keyWindow]));
    const bool client = raster != nil && native.firstResponder == raster &&
                        [raster conformsToProtocol:@protocol(NSTextInputClient)];
    report.check((name + " first responder is the NSTextInputClient raster view").c_str(), client,
                 "firstResponder=" + handoff_probe::describe_object(native == nil ? nil : native.firstResponder));
    if (client && entry.editor != nullptr) {
        // conformsToProtocol: was checked above; id converts without a cast.
        id const object = raster;
        id<NSTextInputClient> const input = object;
        const NSRect caret = [input firstRectForCharacterRange:NSMakeRange(0, 0) actualRange:nullptr];
        const gui_forms::Rect local = (*entry.editor).client_rectangle();
        const gui_forms::Rect in_model = (*entry.editor).rectangle_to_window(local);
        const NSRect in_view = NSMakeRect(in_model.x, in_model.y, in_model.width, in_model.height);
        const NSRect in_window = [raster convertRect:in_view toView:nil];
        const NSRect on_screen = NSInsetRect([native convertRectToScreen:in_window], -2.0, -2.0);
        const NSPoint middle = NSMakePoint(NSMidX(caret), NSMidY(caret));
        report.check((name + " input method caret lies in the edit part").c_str(),
                     NSPointInRect(middle, on_screen) == YES,
                     "caret=" + handoff_probe::describe(caret) + " edit part=" + handoff_probe::describe(on_screen));
    }
    [raster setNeedsDisplay:YES];
    handoff_probe::after(0.4, &probe, &capture_and_type);
}

void finish_probe(void* const context) {
    FocusProbe& probe = *static_cast<FocusProbe*>(context);
    const HostServiceStatus closed = probe.handle.request_close();
    probe.report.check("close request accepted", closed.accepted(), "");
}

void begin_focus(void* const context) {
    FocusProbe& probe = *static_cast<FocusProbe*>(context);
    (*probe.watchdog).reached(2);
    probe.index = 0U;
    focus_one(context);
}

struct FocusReady final {
    FocusProbe* probe{};
    void operator()(Window& window, ApplicationWindowHandle handle) const {
        (*probe).window = &window;
        (*probe).handle = handle;
        (*(*probe).watchdog).reached(1);
        handoff_probe::after(0.6, probe, &begin_focus);
    }
};

} // namespace

int main() {
    handoff_probe::Watchdog watchdog(60);
    FocusProbe probe{};
    probe.watchdog = &watchdog;
    probe.capture_directory = handoff_probe::beside_bundle("handoff_focus_captures");
    ApplicationCreateResult created = Application::create(ApplicationConfig{});
    probe.report.check("application created", static_cast<bool>(created), "");
    if (!created) {
        watchdog.finish();
        probe.report.finish();
        return 1;
    }
    ApplicationWindowOptions options{};
    options.title = window_title;
    options.initial_size = {340.0, 400.0};
    options.ready = FocusReady{&probe};
    const ApplicationResult result = (*created.application).run(make_window(probe), std::move(options));
    probe.report.check("run accepted", result.accepted(),
                       "error=" + std::to_string(static_cast<int>(result.error)));
    probe.report.check("no callback exception", !result.callback_exception, "");
    probe.entries.clear();
    probe.start.reset();
    watchdog.finish();
    probe.report.finish();
    const int status = probe.report.failures() == 0U ? 0 : 1;
    return status;
}
