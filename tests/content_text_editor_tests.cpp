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
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/delegate.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/live_surface/frame/live_surface_frame.hpp"
#include "gui_forms/live_surface/surface/live_surface.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/typography/types/typography_types.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
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

struct RecordSelections final {
    std::vector<TextSelection>* seen{};
    void operator()(const TextSelection& selection) const { (*seen).push_back(selection); }
};

struct RecordUserEdits final {
    std::vector<ContentUserEdit>* seen{};
    void operator()(const ContentUserEdit& edit) const { (*seen).push_back(edit); }
};

// A consumer's edit filter: records each proposal with the text the editor
// held when it was offered, and refuses or rewrites as configured.
struct ProposalLog final {
    ContentTextEditor* editor{};
    std::vector<TextEdit> proposals{};
    std::vector<std::string> texts_at_proposal{};
    bool refuse_digits{false};
    bool upper_case{false};

    void filter(TextEdit& edit) {
        texts_at_proposal.push_back((*editor).text());
        TextEdit copy{};
        copy.kind = edit.kind;
        copy.start = edit.start;
        copy.end = edit.end;
        proposals.push_back(copy);
        const bool digit = !edit.replacement.empty() && edit.replacement.front() >= '0' &&
                           edit.replacement.front() <= '9';
        if (refuse_digits && digit) {
            edit.verdict = TextEditVerdict::refuse;
            return;
        }
        if (!upper_case) return;
        const std::string current = (*editor).text();
        std::string rewritten = current.substr(0U, edit.start.value());
        for (const char byte : edit.replacement) {
            const bool lower = byte >= 'a' && byte <= 'z';
            rewritten.push_back(lower ? static_cast<char>(byte - 'a' + 'A') : byte);
        }
        const std::size_t caret = rewritten.size();
        rewritten.append(current.substr(edit.end.value()));
        edit.verdict = TextEditVerdict::rewrite;
        edit.rewritten_text = rewritten;
        edit.rewritten_caret = Utf8Offset(caret);
    }
};

// Counts every drawing command (fills, strokes, lines, text, images, live
// surfaces, materials); state changes (save, restore, translate, clips) are
// not drawing and are not counted.
class CountingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect, Color) override { ++draws; }
    void fill_rounded_rect(Rect, double, Color) override { ++draws; }
    void stroke_rect(Rect, Color, double) override { ++draws; }
    void stroke_rounded_rect(Rect, double, Color, double) override { ++draws; }
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override { ++draws; }
    void draw_line(Point, Point, Color, double) override { ++draws; }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override { ++draws; }
    void draw_image(ImageId, Rect, double) override { ++draws; }
    void draw_live_surface(std::shared_ptr<LiveSurface>, Rect, double) override {
        ++draws;
        ++live_surfaces;
    }
    void draw_surface_material(Rect, const SurfaceMaterial&) override { ++draws; }

    std::size_t draws{0U};
    std::size_t live_surfaces{0U};
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

void edit_filter_sees_edits_before_they_apply() {
    Fixture fixture = make_fixture("content_editor.filter", "ab");
    Window& window = *fixture.window;
    ContentTextEditor& editor = *fixture.editor;
    std::vector<ContentUserEdit> edits;
    std::vector<std::string> texts;
    SubscriptionToken edit_token = editor.user_edit().subscribe(RecordUserEdits{&edits});
    SubscriptionToken text_token = editor.text_changed().subscribe(RecordTexts{&texts});
    ProposalLog log{};
    log.editor = &editor;
    editor.set_edit_filter(Delegate<TextEdit&>::bind<ProposalLog, &ProposalLog::filter>(log));
    require(window.request_focus(fixture.editor), "the editor takes focus");

    place_caret(editor, 2U);
    require(type(window, "c") && editor.text() == "abc", "an accepted proposal is applied");
    require(log.proposals.size() == 1U && log.proposals[0].kind == TextEditKind::typing &&
                log.proposals[0].start.value() == 2U && log.proposals[0].end.value() == 2U,
            "the filter sees the typed edit's range");
    require(log.texts_at_proposal[0] == "ab", "the filter sees the edit before it is applied");
    require(edits.size() == 1U && edits[0].kind == TextEditKind::typing && !edits[0].composing,
            "user_edit reports the accepted typing");
    require(texts.size() == 1U, "the accepted edit raised text_changed once");

    editor.select(Utf8Offset(0U), Utf8Offset(1U));
    require(press(window, PhysicalKey::backspace) && editor.text() == "bc", "a deletion is offered and applied");
    require(log.proposals.size() == 2U && log.proposals[1].kind == TextEditKind::deletion &&
                log.proposals[1].start.value() == 0U && log.proposals[1].end.value() == 1U,
            "the filter sees the deletion's range");
    require(edits.size() == 2U && edits[1].kind == TextEditKind::deletion, "user_edit reports the deletion");

    log.refuse_digits = true;
    const std::uint64_t before_refusal = editor.text_revision();
    static_cast<void>(type(window, "7"));
    require(editor.text() == "bc" && editor.text_revision() == before_refusal, "a refused edit changes nothing");
    require(log.proposals.size() == 3U && edits.size() == 2U && texts.size() == 2U,
            "a refused edit is offered but raises neither user_edit nor text_changed");
    log.refuse_digits = false;

    editor.select(Utf8Offset(1U), Utf8Offset(2U));
    require(type(window, "c") && editor.text() == "bc", "a replacement by the same bytes is accepted");
    require(log.proposals.size() == 4U && log.proposals[3].start.value() == 1U && log.proposals[3].end.value() == 2U,
            "a replacement by the same bytes still reaches the filter");
    require(texts.size() == 2U && edits.size() == 3U,
            "a replacement by the same bytes raises user_edit and no text_changed");

    log.upper_case = true;
    place_caret(editor, 2U);
    require(type(window, "d") && editor.text() == "bcD" && caret_of(editor) == 3U, "a filter may rewrite the edit");
    log.upper_case = false;

    editor.set_edit_filter(Delegate<TextEdit&>{});
    const std::size_t offered = log.proposals.size();
    require(type(window, "e") && editor.text() == "bcDe" && log.proposals.size() == offered,
            "clearing the filter restores plain editing");
    editor.set_text("plain");
    require(edits.back().kind == TextEditKind::typing && edits.size() == 5U, "set_text raises no user_edit");
}

void no_frames_paint_nothing_over_the_consumer() {
    Fixture fixture = make_fixture("content_editor.no_frames", "ab");
    Window& window = *fixture.window;
    ContentTextEditor& editor = *fixture.editor;
    require(submit_fixed(editor) == ContentLayoutResult::accepted, "the layout is accepted");
    const Rect local{0.0, 0.0, 200.0, 100.0};
    const Rect region{10.0, 10.0, 200.0, 100.0};
    require(!editor.live_surface(), "an editor without frames has no live surface");

    CountingPainter unfocused{};
    editor.on_paint(unfocused, local);
    require(unfocused.draws == 0U, "an unfocused editor without frames draws nothing");

    // The surface registered with the window's presentation has no frame
    // for a host to show.
    std::vector<LiveSurfacePresentation> presentations = window.take_live_surface_presentations(true);
    for (const LiveSurfacePresentation& presentation : presentations) {
        if (presentation.control != editor.runtime_id() || !presentation.surface) continue;
        const LiveSurfaceFrame frame = (*presentation.surface).acquire_latest();
        require(!frame, "the editor's placeholder surface carries no frame");
    }

    // The whole window paints the same commands over the editor's region
    // with the editor as without it: no fill, border or surface of its own.
    CountingPainter with_editor{};
    static_cast<void>(window.paint(with_editor, region));

    require(window.request_focus(fixture.editor), "the editor takes focus");
    editor.set_draws_selection(false);
    editor.select(Utf8Offset(0U), Utf8Offset(1U));
    CountingPainter consumer_draws{};
    editor.on_paint(consumer_draws, local);
    require(consumer_draws.draws == 0U,
            "a focused editor leaving the selection to the consumer draws nothing");
    editor.set_draws_selection(true);
    CountingPainter highlighted{};
    editor.on_paint(highlighted, local);
    require(highlighted.draws > 0U && highlighted.live_surfaces == 0U,
            "with draws_selection only the highlight is drawn, never a surface");

    const Control::Ptr removed = (*fixture.root).remove_child(editor.runtime_id());
    require(removed != nullptr, "the editor leaves the window");
    window.perform_layout();
    CountingPainter without_editor{};
    static_cast<void>(window.paint(without_editor, region));
    require(with_editor.draws == without_editor.draws,
            "an unfocused editor without frames adds no drawing over the consumer's");
}

struct MeasureAndSubmit final {
    ContentTextEditor* editor{};
    Window* window{};
    std::size_t* submitted{};
    void operator()(std::uint64_t revision) const {
        const std::string text = (*editor).text();
        // The window's text engine, outside paint: the same metrics a
        // paint's measure_text_utf8 reads.
        const TextExtent extent = (*window).measure_text_extent_utf8(text, FontSpec{});
        if (!(extent.logical_size.width >= 0.0)) return;
        ContentTextLayout layout = fixed_layout(text, revision);
        if ((*editor).submit_layout(layout) == ContentLayoutResult::accepted) *submitted += 1U;
    }
};

void layouts_measured_outside_paint_or_after_it() {
    Fixture fixture = make_fixture("content_editor.measure", "ab");
    Window& window = *fixture.window;
    ContentTextEditor& editor = *fixture.editor;
    std::size_t submitted = 0U;
    SubscriptionToken token =
        editor.layout_requested().subscribe(MeasureAndSubmit{&editor, &window, &submitted});
    require(window.request_focus(fixture.editor), "the editor takes focus");
    require(type(window, "c") && submitted == 1U && editor.layout_current(),
            "a layout measured in layout_requested, outside paint, is accepted at once");
    token = SubscriptionToken{};

    require(type(window, "d") && !editor.layout_current(), "the next edit waits for a layout");
    CountingPainter before_layout{};
    static_cast<void>(window.paint(before_layout, Rect{0.0, 0.0, 320.0, 240.0}));
    require(!editor.caret_rectangle() && editor.selection_rectangles().empty(),
            "until the layout comes the editor shows no caret and keeps working");
    require(press(window, PhysicalKey::left) && caret_of(editor) == 3U, "keys still move the caret without a layout");
    require(submit_fixed(editor) == ContentLayoutResult::accepted && editor.caret_rectangle(),
            "a layout submitted after the consumer's paint is accepted");
}

void resubmitting_the_same_revision_is_silent() {
    Fixture fixture = make_fixture("content_editor.resubmit", "abc");
    Window& window = *fixture.window;
    ContentTextEditor& editor = *fixture.editor;
    require(submit_fixed(editor) == ContentLayoutResult::accepted, "the first layout is accepted");
    place_caret(editor, 1U);
    window.perform_layout();
    const std::optional<Rect> first = editor.caret_rectangle();
    std::vector<std::uint64_t> requested;
    std::vector<std::string> texts;
    std::vector<TextSelection> selections;
    std::vector<ContentUserEdit> edits;
    SubscriptionToken request_token = editor.layout_requested().subscribe(RecordRevisions{&requested});
    SubscriptionToken text_token = editor.text_changed().subscribe(RecordTexts{&texts});
    SubscriptionToken selection_token = editor.selection_changed().subscribe(RecordSelections{&selections});
    SubscriptionToken edit_token = editor.user_edit().subscribe(RecordUserEdits{&edits});
    const std::uint64_t revision = editor.text_revision();
    for (int scroll = 1; scroll <= 2; ++scroll) {
        ContentTextLayout scrolled = fixed_layout(editor.text(), revision);
        const double shift = -15.0 * static_cast<double>(scroll);
        for (ContentTextLine& line : scrolled.lines) line.bounds.y += shift;
        for (ContentTextCluster& cluster : scrolled.clusters) cluster.bounds.y += shift;
        require(editor.submit_layout(scrolled) == ContentLayoutResult::accepted,
                "a layout for the same revision is accepted again");
        window.perform_layout();
        const std::optional<Rect> moved = editor.caret_rectangle();
        require(first && moved && near((*moved).y, (*first).y + shift) && near((*moved).x, (*first).x),
                "the resubmitted layout moves the caret with the consumer's scroll");
    }
    require(editor.text_revision() == revision, "resubmitting does not change the revision");
    require(requested.empty() && texts.empty() && selections.empty() && edits.empty(),
            "resubmitting raises no event back to the consumer");
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
        edit_filter_sees_edits_before_they_apply();
        no_frames_paint_nothing_over_the_consumer();
        layouts_measured_outside_paint_or_after_it();
        resubmitting_the_same_revision_is_silent();
    } catch (const std::exception& error) {
        std::cerr << "content text editor tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "content text editor tests passed\n";
    return 0;
}
