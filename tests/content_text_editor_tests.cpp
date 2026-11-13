// The content editing mode (owner decisions 2026-11-13, "Content editing
// mode and the integrity boundary"): GUI.Forms' editing logic driving text a
// consumer lays out and renders. The editor keeps the text, the caret and
// selection, the keys, input methods, undo and accessibility; the consumer
// supplies a validated layout (lines, clusters, optional caret stops) and
// frames. These tests play the consumer with a fixed-advance layout.

#include "gui_forms/content/editing/content_text_layout.hpp"
#include "gui_forms/content/frame_sink/content_frame_sink.hpp"
#include "gui_forms/content/types/content_types.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/content_text_editor.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/window/window.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace gui_forms;

// Throws on failure, so the check runs in every build type.
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// The play consumer's metrics: every byte is a cluster `advance` wide, lines
// are `line_height` tall, from (`origin_x`, `origin_y`), split at '\n'.
// ASCII text only, except where a test builds its own layout.
constexpr double origin_x = 5.0;
constexpr double origin_y = 2.0;
constexpr double advance = 10.0;
constexpr double line_height = 20.0;

ContentTextLayout fixed_layout(const std::string& text, std::uint64_t revision) {
    ContentTextLayout layout;
    layout.text_revision = revision;
    std::size_t line_start = 0U;
    std::uint32_t line = 0U;
    const std::size_t size = text.size();
    for (std::size_t index = 0U; index <= size; ++index) {
        const bool ends = index == size || text[index] == '\n';
        if (!ends) continue;
        const double top = origin_y + static_cast<double>(line) * line_height;
        const double width = static_cast<double>(index - line_start) * advance;
        layout.lines.push_back({.range = {Utf8Offset(line_start), Utf8Offset(index)},
                                .bounds = Rect{origin_x, top, width, line_height}});
        for (std::size_t byte = line_start; byte < index; ++byte) {
            const double left = origin_x + static_cast<double>(byte - line_start) * advance;
            layout.clusters.push_back({.range = {Utf8Offset(byte), Utf8Offset(byte + 1U)},
                                       .line = line,
                                       .bounds = Rect{left, top, advance, line_height},
                                       .right_to_left = false});
        }
        line_start = index + 1U;
        line += 1U;
    }
    return layout;
}

ContentLayoutResult submit_fixed(ContentTextEditor& editor) {
    const ContentTextLayout layout = fixed_layout(editor.text(), editor.text_revision());
    const ContentLayoutResult result = editor.submit_layout(layout);
    return result;
}

// An editor in a window at (10, 10), 200 by 100.
struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<ContentTextEditor> editor;
    std::unique_ptr<Window> window;
};

Fixture make_fixture(const char* id, const char* text) {
    Fixture fixture;
    fixture.root = make_control<Panel>(StableId(std::string(id) + ".root"));
    fixture.editor = make_control<ContentTextEditor>(StableId(std::string(id) + ".editor"), std::string(text));
    (*fixture.editor).set_requested_bounds({10.0, 10.0, 200.0, 100.0});
    (*fixture.root).add_child(fixture.editor);
    fixture.window = std::make_unique<Window>(fixture.root, Size{320.0, 240.0});
    (*fixture.window).perform_layout();
    return fixture;
}

bool press(Window& window, std::uint32_t key, Modifier modifiers = Modifier::none) {
    const KeyEvent event{.action = KeyAction::down, .physical_key = key, .modifiers = modifiers};
    const bool handled = window.dispatch_key(event);
    return handled;
}

bool type(Window& window, const char* text, bool composing = false) {
    TextInputEvent input{};
    input.text_utf8 = text;
    input.composing = composing;
    const bool handled = window.dispatch_text(input);
    return handled;
}

void place_caret(ContentTextEditor& editor, std::size_t offset) {
    editor.select(Utf8Offset(offset), Utf8Offset(offset));
}

std::size_t caret_of(const ContentTextEditor& editor) {
    const std::size_t caret = editor.selection().caret.value();
    return caret;
}

bool near(double left, double right) {
    const bool result = std::abs(left - right) < 1e-9;
    return result;
}

struct RecordRevisions final {
    std::vector<std::uint64_t>* seen{};
    void operator()(std::uint64_t revision) const { (*seen).push_back(revision); }
};

struct RecordTexts final {
    std::vector<std::string>* seen{};
    void operator()(const std::string& text) const { (*seen).push_back(text); }
};

void layout_validation_refusals() {
    std::shared_ptr<ContentTextEditor> editor =
        make_control<ContentTextEditor>(StableId("content_editor.validation"), std::string("ab\ncd"));
    ContentTextEditor& field = *editor;
    const std::uint64_t revision = field.text_revision();
    require(!field.layout_current(), "an editor starts without a layout");
    require(submit_fixed(field) == ContentLayoutResult::accepted && field.layout_current(),
            "a layout that matches the text is accepted");

    ContentTextLayout stale = fixed_layout(field.text(), revision + 1U);
    require(field.submit_layout(stale) == ContentLayoutResult::stale_revision,
            "a layout for another revision is refused as stale");
    require(field.layout_current(), "a refused layout leaves the accepted one in force");

    ContentTextLayout past = fixed_layout(field.text(), revision);
    past.lines[1].range.end = Utf8Offset(9U);
    require(field.submit_layout(past) == ContentLayoutResult::refused_range, "a line past the text is refused");

    ContentTextLayout reversed = fixed_layout(field.text(), revision);
    reversed.lines[0].range = {Utf8Offset(2U), Utf8Offset(1U)};
    require(field.submit_layout(reversed) == ContentLayoutResult::refused_range, "a reversed line is refused");

    ContentTextLayout overlapping = fixed_layout(field.text(), revision);
    overlapping.lines[1].range.start = Utf8Offset(1U);
    require(field.submit_layout(overlapping) == ContentLayoutResult::refused_range,
            "a line overlapping the one before is refused");

    ContentTextLayout gap = fixed_layout(field.text(), revision);
    gap.clusters.erase(gap.clusters.begin() + 1);
    require(field.submit_layout(gap) == ContentLayoutResult::refused_cluster,
            "a line its clusters do not cover is refused");

    ContentTextLayout wrong_line = fixed_layout(field.text(), revision);
    wrong_line.clusters[0].line = 7U;
    require(field.submit_layout(wrong_line) == ContentLayoutResult::refused_cluster,
            "a cluster naming no line is refused");

    ContentTextLayout empty_cluster = fixed_layout(field.text(), revision);
    empty_cluster.clusters[0].range.end = Utf8Offset(0U);
    require(field.submit_layout(empty_cluster) == ContentLayoutResult::refused_cluster,
            "an empty cluster is refused");

    ContentTextLayout bad_box = fixed_layout(field.text(), revision);
    bad_box.clusters[2].bounds.x = std::numeric_limits<double>::quiet_NaN();
    require(field.submit_layout(bad_box) == ContentLayoutResult::refused_geometry,
            "a box that is not finite is refused");

    ContentTextLayout negative = fixed_layout(field.text(), revision);
    negative.lines[0].bounds.width = -1.0;
    require(field.submit_layout(negative) == ContentLayoutResult::refused_geometry,
            "a box of negative size is refused");

    ContentTextLayout carets = fixed_layout(field.text(), revision);
    carets.carets.push_back({.offset = Utf8Offset(2U), .caret = Rect{0.0, 0.0, 1.0, 10.0}});
    carets.carets.push_back({.offset = Utf8Offset(1U), .caret = Rect{0.0, 0.0, 1.0, 10.0}});
    require(field.submit_layout(carets) == ContentLayoutResult::refused_caret,
            "caret stops out of order are refused");

    ContentTextLayout far_caret = fixed_layout(field.text(), revision);
    far_caret.carets.push_back({.offset = Utf8Offset(6U), .caret = Rect{0.0, 0.0, 1.0, 10.0}});
    require(field.submit_layout(far_caret) == ContentLayoutResult::refused_range,
            "a caret stop past the text is refused");

    ContentTextLayout too_large = fixed_layout(field.text(), revision);
    too_large.carets.resize(maximum_content_layout_entries + 1U);
    require(field.submit_layout(too_large) == ContentLayoutResult::refused_too_large,
            "a layout over the entry limit is refused before it is read");

    // A grapheme boundary check: "é" is two bytes; the layout may not split it.
    std::shared_ptr<ContentTextEditor> accented =
        make_control<ContentTextEditor>(StableId("content_editor.boundary"), std::string("\xC3\xA9x"));
    ContentTextLayout split;
    split.text_revision = (*accented).text_revision();
    split.lines.push_back({.range = {Utf8Offset(0U), Utf8Offset(3U)}, .bounds = Rect{0.0, 0.0, 20.0, 20.0}});
    split.clusters.push_back({.range = {Utf8Offset(0U), Utf8Offset(1U)}, .line = 0U, .bounds = Rect{0.0, 0.0, 5.0, 20.0}});
    split.clusters.push_back({.range = {Utf8Offset(1U), Utf8Offset(3U)}, .line = 0U, .bounds = Rect{5.0, 0.0, 15.0, 20.0}});
    require((*accented).submit_layout(split) == ContentLayoutResult::refused_boundary,
            "a cluster splitting a grapheme is refused");
    require(!(*accented).layout_current(), "nothing refused is adopted");
    require(content_layout_result_name(ContentLayoutResult::refused_cluster) == "refused_cluster",
            "results have names");
}

void keyboard_editing_requests_layouts() {
    Fixture fixture = make_fixture("content_editor.keys", "ab");
    Window& window = *fixture.window;
    ContentTextEditor& editor = *fixture.editor;
    std::vector<std::uint64_t> requested;
    std::vector<std::string> texts;
    SubscriptionToken requests = editor.layout_requested().subscribe(RecordRevisions{&requested});
    SubscriptionToken changes = editor.text_changed().subscribe(RecordTexts{&texts});
    require(submit_fixed(editor) == ContentLayoutResult::accepted, "the first layout is accepted");
    require(window.request_focus(fixture.editor), "the editor takes focus");
    const std::uint64_t before = editor.text_revision();
    require(type(window, "x") && editor.text() == "abx", "typed text is inserted at the caret");
    require(editor.text_revision() > before && !editor.layout_current(), "an edit makes the layout stale");
    require(!requested.empty() && requested.back() == editor.text_revision(),
            "an edit requests a layout for the new revision");
    require(texts.size() == 1U && texts.back() == "abx", "the edit raises text_changed");
    require(!editor.caret_rectangle(), "no caret is drawn from a stale layout");
    require(submit_fixed(editor) == ContentLayoutResult::accepted && editor.caret_rectangle(),
            "the consumer's new layout brings the caret back");
    require(press(window, PhysicalKey::left) && caret_of(editor) == 2U, "Left moves back a grapheme");
    require(press(window, PhysicalKey::backspace) && editor.text() == "ax" && caret_of(editor) == 1U,
            "Backspace deletes before the caret");
    require(editor.undo() && editor.text() == "abx", "undo restores the deleted text");
    require(editor.redo() && editor.text() == "ax", "redo deletes it again");
    editor.set_read_only(true);
    require(!type(window, "q") || editor.text() == "ax", "a read-only editor refuses typing");
    require(editor.text() == "ax", "the read-only text is unchanged");
    editor.set_read_only(false);
    bool refused = false;
    try {
        editor.set_text("\xFF");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && editor.text() == "ax", "text that is not UTF-8 is refused and changes nothing");
}

void caret_and_line_movement_follow_the_layout() {
    Fixture fixture = make_fixture("content_editor.lines", "ab\ncd");
    Window& window = *fixture.window;
    ContentTextEditor& editor = *fixture.editor;
    editor.set_multiline(true);
    require(submit_fixed(editor) == ContentLayoutResult::accepted, "the layout is accepted");
    require(window.request_focus(fixture.editor), "the editor takes focus");

    place_caret(editor, 0U);
    std::optional<Rect> caret = editor.caret_rectangle();
    require(caret && near((*caret).x, origin_x - 0.5) && near((*caret).y, origin_y) &&
                near((*caret).height, line_height),
            "the caret at 0 stands at the first cluster's leading edge");
    place_caret(editor, 2U);
    caret = editor.caret_rectangle();
    require(caret && near((*caret).x, origin_x + 2.0 * advance - 0.5) && near((*caret).y, origin_y),
            "the caret at a line's end stands after its last cluster");
    place_caret(editor, 3U);
    caret = editor.caret_rectangle();
    require(caret && near((*caret).x, origin_x - 0.5) && near((*caret).y, origin_y + line_height),
            "the caret after the break starts the next line");
    const std::optional<Rect> ime = editor.text_input_caret();
    require(ime && *ime == *caret, "the input method's caret is the drawn caret");

    place_caret(editor, 4U);
    require(press(window, PhysicalKey::up) && caret_of(editor) == 1U, "Up keeps the caret's x on the line above");
    require(press(window, PhysicalKey::down) && caret_of(editor) == 4U, "Down returns to the same x");
    require(press(window, PhysicalKey::end) && caret_of(editor) == 5U, "End goes to the line's end");
    require(press(window, PhysicalKey::home) && caret_of(editor) == 3U, "Home goes to the line's start");
    require(press(window, PhysicalKey::up, Modifier::shift) && editor.selection().anchor.value() == 3U &&
                caret_of(editor) == 0U,
            "Shift+Up extends the selection");

    require(press(window, PhysicalKey::enter) && editor.text() == "\ncd",
            "Enter in a multiline editor replaces the selection with a line break");
}

void selection_boxes_and_pointer_placement() {
    Fixture fixture = make_fixture("content_editor.pointer", "ab\ncd");
    Window& window = *fixture.window;
    ContentTextEditor& editor = *fixture.editor;
    editor.set_multiline(true);
    require(submit_fixed(editor) == ContentLayoutResult::accepted, "the layout is accepted");
    editor.select(Utf8Offset(1U), Utf8Offset(4U));
    const std::vector<Rect> boxes = editor.selection_rectangles();
    require(boxes.size() == 2U, "the selection covers one cluster on each line");
    require(near(boxes[0].x, origin_x + advance) && near(boxes[0].width, advance) && near(boxes[0].y, origin_y),
            "the first box is the 'b' cluster");
    require(near(boxes[1].x, origin_x) && near(boxes[1].y, origin_y + line_height),
            "the second box is the 'c' cluster");

    // A press at local (21, 27): the second line, nearest the boundary at 25.
    PointerEvent press_event{};
    press_event.action = PointerAction::down;
    press_event.button = PointerButton::primary;
    press_event.position = Point{10.0 + 21.0, 10.0 + 27.0};
    require(window.dispatch_pointer(press_event) && caret_of(editor) == 5U && editor.selection().empty(),
            "a press places the caret at the nearest boundary of the line under it");
    require(window.focused_control() == fixture.editor, "a press focuses the editor");
    PointerEvent drag = press_event;
    drag.action = PointerAction::move;
    drag.button = PointerButton::none;
    drag.position = Point{10.0 + 6.0, 10.0 + 3.0};
    require(window.dispatch_pointer(drag) && editor.selection().anchor.value() == 5U && caret_of(editor) == 0U,
            "dragging extends the selection to the first line");
    PointerEvent release = drag;
    release.action = PointerAction::up;
    release.button = PointerButton::primary;
    require(window.dispatch_pointer(release), "the release ends the drag");
    require(editor.position_at(Point{1000.0, 1000.0}).value() == 5U, "a point past the text maps to its end");
}

void clusters_ligatures_and_caret_stops() {
    std::shared_ptr<ContentTextEditor> ligature =
        make_control<ContentTextEditor>(StableId("content_editor.ligature"), std::string("fix"));
    ContentTextLayout fi;
    fi.text_revision = (*ligature).text_revision();
    fi.lines.push_back({.range = {Utf8Offset(0U), Utf8Offset(3U)}, .bounds = Rect{0.0, 0.0, 30.0, 20.0}});
    fi.clusters.push_back({.range = {Utf8Offset(0U), Utf8Offset(2U)}, .line = 0U, .bounds = Rect{0.0, 0.0, 20.0, 20.0}});
    fi.clusters.push_back({.range = {Utf8Offset(2U), Utf8Offset(3U)}, .line = 0U, .bounds = Rect{20.0, 0.0, 10.0, 20.0}});
    require((*ligature).submit_layout(fi) == ContentLayoutResult::accepted, "a ligature layout is accepted");
    place_caret(*ligature, 1U);
    std::optional<Rect> caret = (*ligature).caret_rectangle();
    require(caret && near((*caret).x, 9.5), "a caret inside a ligature divides it by its bytes");

    fi.carets.push_back({.offset = Utf8Offset(1U), .caret = Rect{12.0, 0.0, 2.0, 18.0}});
    require((*ligature).submit_layout(fi) == ContentLayoutResult::accepted, "caret stops are accepted");
    caret = (*ligature).caret_rectangle();
    require(caret && *caret == (Rect{12.0, 0.0, 2.0, 18.0}), "a caret stop overrides the derived caret");

    std::shared_ptr<ContentTextEditor> rtl =
        make_control<ContentTextEditor>(StableId("content_editor.rtl"), std::string("ab"));
    ContentTextLayout mirrored;
    mirrored.text_revision = (*rtl).text_revision();
    mirrored.lines.push_back({.range = {Utf8Offset(0U), Utf8Offset(2U)}, .bounds = Rect{5.0, 0.0, 20.0, 20.0}});
    mirrored.clusters.push_back(
        {.range = {Utf8Offset(0U), Utf8Offset(1U)}, .line = 0U, .bounds = Rect{15.0, 0.0, 10.0, 20.0}, .right_to_left = true});
    mirrored.clusters.push_back(
        {.range = {Utf8Offset(1U), Utf8Offset(2U)}, .line = 0U, .bounds = Rect{5.0, 0.0, 10.0, 20.0}, .right_to_left = true});
    require((*rtl).submit_layout(mirrored) == ContentLayoutResult::accepted, "a right-to-left layout is accepted");
    place_caret(*rtl, 0U);
    caret = (*rtl).caret_rectangle();
    require(caret && near((*caret).x, 24.5), "a right-to-left cluster's leading edge is its right");
    place_caret(*rtl, 2U);
    caret = (*rtl).caret_rectangle();
    require(caret && near((*caret).x, 4.5), "after the last right-to-left cluster the caret is at its left");
}

void input_method_composition() {
    Fixture fixture = make_fixture("content_editor.ime", "");
    Window& window = *fixture.window;
    ContentTextEditor& editor = *fixture.editor;
    editor.set_multiline(true);
    require(window.request_focus(fixture.editor), "the editor takes focus");
    require(type(window, "k", true) && editor.text() == "k", "a composition's text is shown in the text");
    require(press(window, PhysicalKey::enter) && editor.text() == "k",
            "Enter during a composition is the input method's, not a line break");
    require(type(window, "\xE3\x81\x8B") && editor.text() == "\xE3\x81\x8B",
            "the committed text replaces the composition");
    require(editor.undo() && editor.text() != "\xE3\x81\x8B", "undo takes the committed text back");
}

void accessibility_publication() {
    Fixture fixture = make_fixture("content_editor.semantics", "ab\ncd");
    Window& window = *fixture.window;
    ContentTextEditor& editor = *fixture.editor;
    editor.set_multiline(true);
    editor.accessible_name = "Notes";
    place_caret(editor, 4U);
    const SemanticDescriptor descriptor = editor.semantic_descriptor();
    require(descriptor.role == SemanticRole::text_box && descriptor.name == "Notes" && descriptor.value == "ab\ncd",
            "the editor is an editable text box with its text as the value");
    require(descriptor.text_selection && (*descriptor.text_selection).caret.value() == 4U && descriptor.multiline,
            "the selection and caret are published");

    SemanticTextLayout unlaid{};
    require(window.semantic_text_layout(editor.runtime_id(), unlaid) == SemanticTextResult::done,
            "the text is published before any layout");
    require(unlaid.lines.size() == 1U && unlaid.graphemes.size() == 5U && unlaid.graphemes[0].bounds.width == 0.0,
            "without a layout the graphemes carry empty boxes");

    require(submit_fixed(editor) == ContentLayoutResult::accepted, "the layout is accepted");
    SemanticTextLayout laid{};
    require(window.semantic_text_layout(editor.runtime_id(), laid) == SemanticTextResult::done,
            "the laid-out text is published");
    require(laid.lines.size() == 2U && laid.graphemes.size() == 4U, "the consumer's lines and clusters are published");
    require(near(laid.graphemes[0].bounds.x, 10.0 + origin_x) && near(laid.graphemes[2].bounds.y, 10.0 + origin_y + line_height),
            "grapheme boxes are in window coordinates");
    require(laid.graphemes[2].line.value() == 1U, "graphemes name their lines");
    require(!laid.word_starts.empty(), "the word rule's stops are published");

    require(window.set_semantic_text_selection(editor.runtime_id(), SemanticTextSelection{Utf8Offset(1U), Utf8Offset(4U)}) ==
                    SemanticTextResult::done &&
                editor.selection().anchor.value() == 1U && caret_of(editor) == 4U,
            "assistive technology may move the selection");

    std::shared_ptr<ContentTextEditor> accented =
        make_control<ContentTextEditor>(StableId("content_editor.semantics_boundary"), std::string("\xC3\xA9"));
    require((*accented).semantic_descriptor().value == "\xC3\xA9", "the value is the text");
}

void frames_through_the_sink() {
    Fixture fixture = make_fixture("content_editor.frames", "ab");
    ContentTextEditor& editor = *fixture.editor;
    ContentFrameSink sink = editor.open_frames(true);
    require(!sink.revoked() && editor.live_surface(), "open_frames gives a live sink");
    const ContentRequest request = sink.request();
    require(request.size.width >= 200U && request.size.height >= 100U, "the sink asks for the region's device size");
    std::vector<std::byte> pixels(static_cast<std::size_t>(4U * 4U * 4U), std::byte{0x40});
    ContentFrame frame;
    frame.width = 4U;
    frame.height = 4U;
    frame.row_bytes = 16U;
    frame.opaque = true;
    frame.pixels = std::span<const std::byte>(pixels.data(), pixels.size());
    require(sink.publish(frame) == ContentPublishResult::published, "a valid frame is published");
    ContentFrame short_rows = frame;
    short_rows.row_bytes = 8U;
    require(sink.publish(short_rows) == ContentPublishResult::refused_row_bytes, "a frame with short rows is refused");
    ContentFrameSink fresh = editor.open_frames(true);
    require(sink.revoked() && sink.publish(frame) == ContentPublishResult::revoked,
            "reopening revokes the old sink");
    require(fresh.publish(frame) == ContentPublishResult::published, "the new sink publishes");
}

} // namespace

int main() {
    try {
        layout_validation_refusals();
        keyboard_editing_requests_layouts();
        caret_and_line_movement_follow_the_layout();
        selection_boxes_and_pointer_placement();
        clusters_ligatures_and_caret_stops();
        input_method_composition();
        accessibility_publication();
        frames_through_the_sink();
    } catch (const std::exception& error) {
        std::cerr << "content text editor tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "content text editor tests passed\n";
    return 0;
}
