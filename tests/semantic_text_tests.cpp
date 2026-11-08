// The two on-demand text queries of owner decision A-T1 (design language
// 12.13): Window::semantic_text_layout and Window::set_semantic_text_selection,
// answered by TextBox (single-line and multiline) and forwarded by
// MaskedTextBox, with their refusals; and the unit boundaries hosts move
// by (fill_semantic_text_boundaries).

#include "gui_forms/gui_forms.hpp"
#include "headless_host.hpp"
#include "support/typography_painter.hpp"

#include <cstddef>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

// Every grapheme ten units wide and every line twenty high, so boxes can be
// checked exactly.
class Metrics final : public TextMetricsProvider {
public:
    ResolvedTextLayout resolve_text_layout_utf8(std::string_view text, FontSpec font) override {
        ResolvedTextLayout result = estimate_text_layout_utf8(text, font);
        const TextStore store(text);
        result.logical_size = {static_cast<double>(store.grapheme_count().value()) * 10.0, 20.0};
        result.ascent = 15.0;
        result.descent = 5.0;
        result.line_gap = 0.0;
        return result;
    }
};

// The node with `stable_id` at or below `nodes`, depth first, or null.
const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        const SemanticNode* found = find_node(node.children, stable_id);
        if (found != nullptr) return found;
    }
    return nullptr;
}
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

// The selection the window's snapshot publishes for `stable_id`, or none.
std::optional<SemanticTextSelection> published_selection(Window& window, std::string_view stable_id) {
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* node = find_node(snapshot.roots, stable_id);
    if (node == nullptr) return std::nullopt;
    const std::optional<SemanticTextSelection> selection = (*node).text_selection;
    return selection;
}

void single_line_layout_and_words() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("field"));
    (*field).set_requested_bounds({20.0, 10.0, 200.0, 26.0});
    (*root).add_child(field);
    Window window(root, {300.0, 100.0});
    window.perform_layout();
    (*field).text = "ab cd";
    SemanticTextLayout layout;
    const SemanticTextResult result = window.semantic_text_layout((*field).runtime_id(), layout);
    require(result == SemanticTextResult::done, "a text box answers a layout query");
    require(layout.lines.size() == 1U && layout.lines[0].range.start == Utf8Offset(0U) &&
                layout.lines[0].range.end == Utf8Offset(5U),
            "a single-line box has one line over the whole value");
    require(layout.graphemes.size() == 5U, "one grapheme per character of plain text");
    for (std::size_t index = 0U; index < layout.graphemes.size(); ++index) {
        const SemanticTextGrapheme& grapheme = layout.graphemes[index];
        require(grapheme.range.start == Utf8Offset(index) && grapheme.range.end == Utf8Offset(index + 1U) &&
                    grapheme.line == LineIndex(0U),
                "graphemes are in text order on line 0");
        // Client coordinates: the box lies inside the field's top and bottom.
        require(grapheme.bounds.y >= 10.0 && grapheme.bounds.y + grapheme.bounds.height <= 36.0 &&
                    grapheme.bounds.x >= 20.0,
                "grapheme boxes are in window client coordinates");
        if (index > 0U) {
            require(grapheme.bounds.x >= layout.graphemes[index - 1U].bounds.x, "graphemes run left to right");
        }
    }
    require(layout.word_starts.size() == 3U && layout.word_starts[0] == Utf8Offset(0U) &&
                layout.word_starts[1] == Utf8Offset(3U) && layout.word_starts[2] == Utf8Offset(5U),
            "word starts follow Ctrl+Left and Ctrl+Right: 0, each word, the end");
    const std::size_t lines_capacity = layout.lines.capacity();
    const SemanticTextResult again = window.semantic_text_layout((*field).runtime_id(), layout);
    require(again == SemanticTextResult::done && layout.lines.size() == 1U && layout.graphemes.size() == 5U &&
                layout.lines.capacity() == lines_capacity,
            "a second query empties and reuses the caller's storage");
    (*field).text = "";
    const SemanticTextResult empty = window.semantic_text_layout((*field).runtime_id(), layout);
    require(empty == SemanticTextResult::done && layout.lines.size() == 1U && layout.graphemes.empty() &&
                layout.word_starts.empty(),
            "empty text has one empty line, no graphemes and no words");
}

void multiline_layout() {
    const std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("notes"));
    Metrics metrics;
    // The window borrows the metrics provider, so it is destroyed first.
    Window window(field, {200.0, 100.0});
    (*field).set_requested_bounds({0.0, 0.0, 200.0, 100.0});
    (*field).multiline = true;
    window.set_text_metrics_provider(&metrics);
    window.perform_layout();
    (*field).text = "ab\ncd";
    SemanticTextLayout layout;
    const SemanticTextResult result = window.semantic_text_layout((*field).runtime_id(), layout);
    require(result == SemanticTextResult::done, "a multiline box answers a layout query");
    require(layout.lines.size() == 2U, "two logical lines give two visual lines");
    require(layout.lines[0].range.start == Utf8Offset(0U) && layout.lines[0].range.end == Utf8Offset(2U) &&
                layout.lines[1].range.start == Utf8Offset(3U) && layout.lines[1].range.end == Utf8Offset(5U),
            "a line's range leaves out the line break");
    require(layout.graphemes.size() == 4U && layout.graphemes[1].line == LineIndex(0U) &&
                layout.graphemes[2].line == LineIndex(1U) &&
                layout.graphemes[2].range.start == Utf8Offset(3U),
            "the line break is no grapheme; graphemes name their line");
    require(layout.graphemes[1].bounds.x - layout.graphemes[0].bounds.x == 10.0 &&
                layout.graphemes[0].bounds.width == 10.0,
            "grapheme boxes use the measured advances");
    require(layout.lines[1].bounds.y - layout.lines[0].bounds.y == 20.0 &&
                layout.graphemes[2].bounds.y == layout.lines[1].bounds.y,
            "lines are a line height apart");
}

// Whether `boundaries` holds exactly `expected`, in order.
bool boundaries_are(const std::vector<Utf8Offset>& boundaries, const std::vector<std::size_t>& expected) {
    if (boundaries.size() != expected.size()) return false;
    for (std::size_t index = 0U; index < expected.size(); ++index) {
        if (boundaries[index].value() != expected[index]) return false;
    }
    return true;
}

void unit_boundaries() {
    const std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("notes"));
    Metrics metrics;
    // The window borrows the metrics provider, so it is destroyed first.
    Window window(field, {200.0, 100.0});
    (*field).set_requested_bounds({0.0, 0.0, 200.0, 100.0});
    (*field).multiline = true;
    window.set_text_metrics_provider(&metrics);
    window.perform_layout();
    // "e" and U+0301 are one grapheme, bytes 3 to 6.
    const std::string value = "Cafe\xCC\x81 x\nyz";
    (*field).text = value;
    SemanticTextLayout layout;
    require(window.semantic_text_layout((*field).runtime_id(), layout) == SemanticTextResult::done,
            "the box answers a layout query");
    std::vector<Utf8Offset> boundaries;
    fill_semantic_text_boundaries(layout, value, SemanticTextUnit::character, boundaries);
    require(boundaries_are(boundaries, {0U, 1U, 2U, 3U, 6U, 7U, 8U, 9U, 10U, 11U}),
            "characters are graphemes, and the line break between them is one character");
    fill_semantic_text_boundaries(layout, value, SemanticTextUnit::word, boundaries);
    require(boundaries.front().value() == 0U && boundaries.back().value() == 11U && boundaries.size() >= 3U,
            "words run from 0 to the end through the word starts");
    fill_semantic_text_boundaries(layout, value, SemanticTextUnit::line, boundaries);
    require(boundaries_are(boundaries, {0U, 9U, 11U}), "a line runs to the next line's start");
    fill_semantic_text_boundaries(layout, value, SemanticTextUnit::paragraph, boundaries);
    require(boundaries_are(boundaries, {0U, 9U, 11U}), "a paragraph ends after its line feed");
    fill_semantic_text_boundaries(layout, value, SemanticTextUnit::document, boundaries);
    require(boundaries_are(boundaries, {0U, 11U}), "the document is one unit");
    // A layout of longer text than the value: offsets past its end are no
    // boundaries of it.
    fill_semantic_text_boundaries(layout, std::string_view(value).substr(0U, 7U), SemanticTextUnit::character,
                                  boundaries);
    require(boundaries_are(boundaries, {0U, 1U, 2U, 3U, 6U, 7U}), "boundaries stop at the value's end");
    (*field).text = "";
    require(window.semantic_text_layout((*field).runtime_id(), layout) == SemanticTextResult::done,
            "an empty box answers a layout query");
    fill_semantic_text_boundaries(layout, "", SemanticTextUnit::word, boundaries);
    require(boundaries_are(boundaries, {0U}), "empty text has one boundary");
    require(std::string_view(semantic_text_unit_name(SemanticTextUnit::paragraph)) == "paragraph",
            "units have names");
}

void selection_and_boundaries() {
    const std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("field"));
    Window window(field, {200.0, 30.0});
    (*field).set_requested_bounds({0.0, 0.0, 200.0, 30.0});
    window.perform_layout();
    // "e" and a combining acute accent are one grapheme of three bytes.
    (*field).text = "e\xCC\x81x";
    SemanticTextLayout layout;
    const SemanticTextResult read = window.semantic_text_layout((*field).runtime_id(), layout);
    require(read == SemanticTextResult::done && layout.graphemes.size() == 2U &&
                layout.graphemes[0].range.end == Utf8Offset(3U),
            "a combining sequence is one grapheme");
    const TextSelection before = (*field).selection();
    const SemanticTextResult inside =
        window.set_semantic_text_selection((*field).runtime_id(), {Utf8Offset(0U), Utf8Offset(1U)});
    require(inside == SemanticTextResult::off_boundary && (*field).selection() == before,
            "a caret inside a combining sequence is refused and changes nothing");
    const SemanticTextResult past =
        window.set_semantic_text_selection((*field).runtime_id(), {Utf8Offset(0U), Utf8Offset(9U)});
    require(past == SemanticTextResult::off_boundary && (*field).selection() == before,
            "an offset past the value is refused");
    const SemanticTextResult moved =
        window.set_semantic_text_selection((*field).runtime_id(), {Utf8Offset(0U), Utf8Offset(3U)});
    require(moved == SemanticTextResult::done && (*field).selection().anchor == Utf8Offset(0U) &&
                (*field).selection().caret == Utf8Offset(3U),
            "a selection on boundaries is applied");
    require(window.focused_control() == nullptr, "selecting through a host does not move focus");
    const std::optional<SemanticTextSelection> published = published_selection(window, "field");
    require(published.has_value() && (*published).anchor == Utf8Offset(0U) && (*published).caret == Utf8Offset(3U),
            "the snapshot publishes the new selection");
}

void refusals() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<TextBox> secret = make_control<TextBox>(StableId("secret"));
    const std::shared_ptr<Label> caption = make_control<Label>(StableId("caption"));
    const std::shared_ptr<Panel> scope = make_control<Panel>(StableId("scope"));
    const std::shared_ptr<TextBox> outside = make_control<TextBox>(StableId("outside"));
    const std::shared_ptr<TextBox> inside = make_control<TextBox>(StableId("inside"));
    (*secret).set_requested_bounds({0.0, 0.0, 100.0, 24.0});
    (*caption).set_requested_bounds({0.0, 30.0, 100.0, 20.0});
    (*outside).set_requested_bounds({0.0, 60.0, 100.0, 24.0});
    (*scope).set_requested_bounds({0.0, 90.0, 200.0, 40.0});
    (*inside).set_requested_bounds({0.0, 0.0, 100.0, 24.0});
    (*root).add_child(secret);
    (*root).add_child(caption);
    (*root).add_child(outside);
    (*scope).add_child(inside);
    (*root).add_child(scope);
    Window window(root, {300.0, 200.0});
    window.perform_layout();
    (*secret).text = "hunter";
    (*secret).password_character = U'*';
    (*caption).set_text("Name");
    (*outside).text = "out";
    (*inside).text = "in";
    SemanticTextLayout layout;
    layout.word_starts.push_back(Utf8Offset(7U));
    require(window.semantic_text_layout((*secret).runtime_id(), layout) == SemanticTextResult::no_text &&
                layout.word_starts.empty(),
            "a password box publishes no layout, and the storage is left empty");
    require(window.set_semantic_text_selection((*secret).runtime_id(), {Utf8Offset(0U), Utf8Offset(1U)}) ==
                SemanticTextResult::no_text,
            "a password box takes no selection");
    require(window.semantic_text_layout((*caption).runtime_id(), layout) == SemanticTextResult::no_text,
            "a control without editable text answers no_text");
    require(window.semantic_text_layout(RuntimeId{}, layout) == SemanticTextResult::stale_identity,
            "runtime identity zero names no control");

    const FocusScopeId scope_id = window.begin_focus_scope(scope, inside);
    const TextSelection outside_before = (*outside).selection();
    require(window.set_semantic_text_selection((*outside).runtime_id(), {Utf8Offset(0U), Utf8Offset(1U)}) ==
                SemanticTextResult::outside_focus_scope &&
                (*outside).selection() == outside_before,
            "a selection outside the active focus scope is refused and changes nothing");
    require(window.semantic_text_layout((*outside).runtime_id(), layout) == SemanticTextResult::done,
            "a layout may be read outside the focus scope");
    require(window.set_semantic_text_selection((*inside).runtime_id(), {Utf8Offset(0U), Utf8Offset(2U)}) ==
                SemanticTextResult::done,
            "a selection inside the focus scope is applied");
    const bool ended = window.end_focus_scope(scope_id);
    require(ended, "the focus scope ends");

    const RuntimeId removed = (*outside).runtime_id();
    const Control::Ptr detached = (*root).remove_child(removed);
    require(detached == outside, "the control is removed");
    require(window.semantic_text_layout(removed, layout) == SemanticTextResult::stale_identity &&
                window.set_semantic_text_selection(removed, {Utf8Offset(0U), Utf8Offset(0U)}) ==
                    SemanticTextResult::stale_identity,
            "a removed control's runtime identity is stale");
}

// Calls both queries from inside its own paint.
class PaintProbe final : public Control {
public:
    explicit PaintProbe(StableId stable_id) : Control(std::move(stable_id)) {}
    void on_paint(Painter&, Rect) override {
        Window* const owner = window();
        if (owner == nullptr) return;
        layout_result = (*owner).semantic_text_layout(target, layout);
        selection_result = (*owner).set_semantic_text_selection(target, {Utf8Offset(0U), Utf8Offset(1U)});
        painted = true;
    }
    RuntimeId target{};
    SemanticTextLayout layout;
    SemanticTextResult layout_result{SemanticTextResult::done};
    SemanticTextResult selection_result{SemanticTextResult::done};
    bool painted{};
};

void during_paint() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    const std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("field"));
    const std::shared_ptr<PaintProbe> probe = make_control<PaintProbe>(StableId("probe"));
    (*field).set_requested_bounds({0.0, 0.0, 100.0, 24.0});
    (*probe).set_requested_bounds({0.0, 30.0, 20.0, 20.0});
    (*root).add_child(field);
    (*root).add_child(probe);
    Window window(root, {200.0, 60.0});
    window.perform_layout();
    (*field).text = "abc";
    (*probe).target = (*field).runtime_id();
    const TextSelection before = (*field).selection();
    test_support::TypographyPainter painter{};
    const DamageRegion damage = window.take_damage();
    const std::optional<PaintReceipt> receipt = window.paint(painter, damage.bounds());
    require(receipt.has_value() && (*probe).painted, "the probe paints");
    require((*probe).layout_result == SemanticTextResult::during_paint &&
                (*probe).selection_result == SemanticTextResult::during_paint &&
                (*field).selection() == before,
            "both queries are refused during paint, not deferred");
}

void masked_text_box_forwards() {
    const std::shared_ptr<MaskedTextBox> box = make_control<MaskedTextBox>(StableId("masked"), std::string("00-00"));
    Window window(box, {200.0, 30.0});
    (*box).set_requested_bounds({0.0, 0.0, 200.0, 30.0});
    window.perform_layout();
    const MaskEditResult typed = (*box).set_text("1234");
    require(typed.succeeded(), "the mask takes four digits");
    SemanticTextLayout layout;
    const SemanticTextResult result = window.semantic_text_layout((*box).runtime_id(), layout);
    require(result == SemanticTextResult::done, "a masked text box forwards the layout to its editor");
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* node = find_node(snapshot.roots, "masked");
    require(node != nullptr && layout.lines.size() == 1U &&
                layout.lines[0].range.end == Utf8Offset((*node).value.size()),
            "the forwarded layout indexes the published value");
    const SemanticTextResult selected =
        window.set_semantic_text_selection((*box).runtime_id(), {Utf8Offset(0U), Utf8Offset(2U)});
    require(selected == SemanticTextResult::done && (*(*box).editor()).selection().caret == Utf8Offset(2U),
            "a masked text box forwards the selection to its editor");
}

} // namespace

int main() {
    try {
        single_line_layout_and_words();
        multiline_layout();
        selection_and_boundaries();
        unit_boundaries();
        refusals();
        during_paint();
        masked_text_box_forwards();
        std::cout << "gui_forms_semantic_text_tests: passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
